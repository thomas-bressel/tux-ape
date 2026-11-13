#include "core/dandanator.h"

#include <algorithm>

namespace tuxape {

namespace {

constexpr uint8_t kPrefix = 0xFD;
constexpr uint8_t kZone0 = 0x70;   // LD (IY+n),B
constexpr uint8_t kZone1 = 0x71;   // LD (IY+n),C
constexpr uint8_t kConfig = 0x77;  // LD (IY+n),A
constexpr uint8_t kRet = 0xC9;

}  // namespace

Dandanator::Dandanator()
    : flash_(kSize, 0xFF)
{
    reset();
}

void Dandanator::setFlash(std::span<const uint8_t> image)
{
    std::fill(flash_.begin(), flash_.end(), uint8_t{0xFF});
    std::copy_n(image.begin(), std::min(image.size(), flash_.size()), flash_.begin());
    changed_ = false;
    flashStep_ = 0;
}

// At a reset (the CPLD's own, wired to the machine's): zone 0 shown on slot
// 0 at &0000, zone 1 hidden on slot 0 at &4000, no serial input, the USB
// line idle, the flash not written, the ROM enable not followed. The left
// button puts zone 0 on slot 31, the middle one hides it, and wins.
void Dandanator::reset()
{
    slot_[0] = boot_ == Boot::Slot31 ? 31 : 0;
    slot_[1] = 0;
    hidden_[0] = boot_ == Boot::Machine;
    hidden_[1] = true;
    high_[0] = high_[1] = false;
    followRom_ = false;
    commandsOff_ = false;
    flashWrites_ = false;
    serialIn_ = false;
    serialOut_ = true;
    retWait_ = delayedOff_ = delayedFollow_ = false;
    delayedHigh_[0] = delayedHigh_[1] = false;
    delayedHidden_[0] = delayedHidden_[1] = false;
    prefixes_ = 0;
    pending_ = 0;
    flashStep_ = 0;
}

bool Dandanator::fetched(uint8_t opcode)
{
    // A RET, after a configuration that waits for one with no command since:
    // the configuration takes effect now, the stack read already from it.
    // (The CPLD looks at the opcode alone: SET 1,C, &CB &C9, will do.)
    if (opcode == kRet && retWait_) {
        retWait_ = false;
        hidden_[0] = delayedHidden_[0];
        hidden_[1] = delayedHidden_[1];
        high_[0] = delayedHigh_[0];
        high_[1] = delayedHigh_[1];
        commandsOff_ = delayedOff_;
        followRom_ = delayedFollow_;
        if (followRom_)
            slot_[0] = static_cast<uint8_t>(0x1C | followZone_);
        prefixes_ = 0;
        return true;
    }
    if (commandsOff_) {
        prefixes_ = 0;
        return false;
    }
    if (opcode == kPrefix) {
        ++prefixes_;
        return false;
    }
    // The prefixes the CPLD needs are three, the last being the command's
    // own (&FD &70 and so on): two before it, then.
    if ((opcode == kZone0 || opcode == kZone1 || opcode == kConfig) && prefixes_ >= 3)
        pending_ = opcode;
    prefixes_ = 0;
    return false;
}

bool Dandanator::written(uint16_t address, uint8_t value)
{
    bool remap = false;
    // The command's value is the byte its instruction writes.
    if (pending_) {
        const uint8_t opcode = pending_;
        pending_ = 0;
        command(opcode, value);
        remap = true;
    }
    // The flash, written through a zone it is shown in. (Reads only are
    // the ROM enable's: a zone that follows it is not written.)
    if (flashWrites_) {
        const int quarter = address >> 14;
        const int zone = quarter & 1;
        const bool romArea = quarter == 0 || quarter == 3;
        if (!hidden_[zone] && high_[zone] == (quarter >> 1) && !(followRom_ && romArea))
            writeFlash(static_cast<uint32_t>(slot_[zone]) << 14 | (address & 0x3FFF), value);
    }
    return remap;
}

void Dandanator::command(uint8_t opcode, uint8_t value)
{
    // Any command cancels a configuration waiting for a RET.
    retWait_ = false;
    switch (opcode) {
    case kZone0:
    case kZone1: {
        const int zone = opcode == kZone1;
        hidden_[zone] = value & 0x20;
        slot_[zone] = value & 0x1F;
        break;
    }
    case kConfig:
        if (!(value & 0x80)) {
            serialIn_ = value & 0x01;
            flashWrites_ = value & 0x02;
            serialOut_ = value & 0x04;
            followZone_ = (value >> 3) & 3;
        } else if (value & 0x40) {
            retWait_ = true;
            delayedOff_ = value & 0x20;
            delayedFollow_ = value & 0x10;
            delayedHigh_[1] = value & 0x08;
            delayedHigh_[0] = value & 0x04;
            delayedHidden_[1] = value & 0x02;
            delayedHidden_[0] = value & 0x01;
        } else {
            commandsOff_ = value & 0x20;
            high_[1] = value & 0x08;
            high_[0] = value & 0x04;
            hidden_[1] = value & 0x02;
            hidden_[0] = value & 0x01;
        }
        break;
    default: break;
    }
}

int Dandanator::shownSlot(int quarter, bool lowerRom, bool upperRom) const
{
    const int zone = quarter & 1;
    if (hidden_[zone] || high_[zone] != (quarter >> 1))
        return -1;
    // Following the ROM enable: in the ROM areas, only while the machine
    // reads its ROM there.
    if (followRom_ && ((quarter == 0 && !lowerRom) || (quarter == 3 && !upperRom)))
        return -1;
    return slot_[zone];
}

// The SST39SF040's commands, its addresses taken on 15 bits: &AA at &5555,
// &55 at &2AAA, then &A0 at &5555 for a byte to program (bits can only go
// from 1 to 0), or &80 at &5555, &AA, &55 and &30 at a sector (4K) to erase
// it, &10 at &5555 for the whole chip.
void Dandanator::writeFlash(uint32_t address, uint8_t value)
{
    const uint32_t command = address & 0x7FFF;
    switch (flashStep_) {
    case 0: flashStep_ = value == 0xAA && command == 0x5555 ? 1 : 0; break;
    case 1: flashStep_ = value == 0x55 && command == 0x2AAA ? 2 : 0; break;
    case 2:
        flashStep_ = command != 0x5555 ? 0 : value == 0xA0 ? 3 : value == 0x80 ? 4 : 0;
        break;
    case 3:
        flash_[address] &= value;
        changed_ = true;
        flashStep_ = 0;
        break;
    case 4: flashStep_ = value == 0xAA && command == 0x5555 ? 5 : 0; break;
    case 5: flashStep_ = value == 0x55 && command == 0x2AAA ? 6 : 0; break;
    case 6:
        if (value == 0x30) {
            std::fill_n(flash_.begin() + (address & ~0xFFFu), 0x1000, uint8_t{0xFF});
            changed_ = true;
        } else if (value == 0x10 && command == 0x5555) {
            std::fill(flash_.begin(), flash_.end(), uint8_t{0xFF});
            changed_ = true;
        }
        flashStep_ = 0;
        break;
    default: flashStep_ = 0; break;
    }
}

Dandanator::State Dandanator::state() const
{
    State s;
    for (int zone = 0; zone < 2; ++zone) {
        s.slot[zone] = slot_[zone];
        s.shown[zone] = !hidden_[zone];
        s.high[zone] = high_[zone];
    }
    s.followRom = followRom_;
    s.followSlot = static_cast<uint8_t>(0x1C | followZone_);
    s.commandsOff = commandsOff_;
    s.waitingRet = retWait_;
    s.flashWrites = flashWrites_;
    s.serialIn = serialIn_;
    s.serialOut = serialOut_;
    return s;
}

}  // namespace tuxape
