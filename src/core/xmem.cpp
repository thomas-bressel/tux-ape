#include "core/xmem.h"

#include <algorithm>

namespace tuxape {

namespace {

// The addresses the chip wants its command bytes at. It looks at the low
// fifteen bits only.
constexpr unsigned kCommandMask = 0x7FFF;
constexpr unsigned kFirst = 0x5555, kSecond = 0x2AAA;
constexpr unsigned kSector = 0x1000;  // what one erase command clears

}  // namespace

Xmem::Xmem()
    : flash_(kFlashSize, 0xFF)
    , name_(kRomSize)
{
    // The maker at even addresses, the device at odd ones.
    for (size_t i = 0; i < name_.size(); ++i)
        name_[i] = i & 1 ? kDevice : kMaker;
}

void Xmem::setFlash(std::span<const uint8_t> image)
{
    std::fill(flash_.begin(), flash_.end(), uint8_t{0xFF});
    std::copy_n(image.begin(), std::min(image.size(), flash_.size()), flash_.begin());
    changed_ = false;
}

void Xmem::setRom(int place, std::span<const uint8_t> image)
{
    if (place < 0 || place >= kRoms)
        return;
    const auto dest = flash_.begin() + static_cast<ptrdiff_t>(place) * kRomSize;
    std::fill_n(dest, kRomSize, uint8_t{0xFF});
    std::copy_n(image.begin(), std::min<size_t>(image.size(), kRomSize), dest);
    changed_ = true;
}

void Xmem::reset()
{
    state_ = State::Read;
    naming_ = false;
}

// Whether the chip is the one spoken to with that ROM select byte.
bool Xmem::selected(uint8_t select) const
{
    if (select & 0x80)
        return false;
    if (select & 0x40)
        return true;  // the two places kept from the machine, asked for by name
    const int rom = select & 0x1F;
    return rom != kLowerPlace && (rom != kBasicPlace || switches_.bootRom);
}

const uint8_t* Xmem::upperRom(uint8_t select) const
{
    if (!switches_.readRom || !selected(select))
        return nullptr;
    return naming_ ? name_.data() : &flash_[static_cast<size_t>(select & 0x1F) * kRomSize];
}

const uint8_t* Xmem::lowerRom() const
{
    if (!switches_.readRom || !switches_.bootRom)
        return nullptr;
    return naming_ ? name_.data() : &flash_[static_cast<size_t>(kLowerPlace) * kRomSize];
}

bool Xmem::write(uint8_t select, uint16_t offset, uint8_t value)
{
    if (!switches_.romFree || !selected(select))
        return false;
    const unsigned address = static_cast<unsigned>(select & 0x1F) * kRomSize + (offset & (kRomSize - 1));
    const unsigned command = address & kCommandMask;
    const bool wasNaming = naming_;

    switch (state_) {
    case State::Program:
        // A bit can be cleared, never set: that takes an erase.
        if ((flash_[address] & value) != flash_[address]) {
            flash_[address] &= value;
            changed_ = true;
        }
        state_ = State::Read;
        break;
    case State::Read:
        if (command == kFirst && value == 0xAA)
            state_ = State::Unlock1;
        else if (value == 0xF0)
            naming_ = false;
        break;
    case State::Unlock1:
        state_ = command == kSecond && value == 0x55 ? State::Unlock2 : State::Read;
        break;
    case State::Unlock2:
        state_ = State::Read;
        if (command != kFirst)
            break;
        if (value == 0xA0)
            state_ = State::Program;
        else if (value == 0x80)
            state_ = State::Erase;
        else if (value == 0x90)
            naming_ = true;
        else if (value == 0xF0)
            naming_ = false;
        break;
    case State::Erase:
        state_ = command == kFirst && value == 0xAA ? State::EraseUnlock1 : State::Read;
        break;
    case State::EraseUnlock1:
        state_ = command == kSecond && value == 0x55 ? State::EraseUnlock2 : State::Read;
        break;
    case State::EraseUnlock2:
        state_ = State::Read;
        if (value == 0x30) {
            // The 4K the address is in. It is done at once: a program
            // that waits for the chip finds it ready.
            std::fill_n(flash_.begin() + (address & ~(kSector - 1)), kSector, uint8_t{0xFF});
            changed_ = true;
        } else if (value == 0x10 && command == kFirst) {
            std::fill(flash_.begin(), flash_.end(), uint8_t{0xFF});
            changed_ = true;
        }
        break;
    }
    return naming_ != wasNaming;
}

}  // namespace tuxape
