#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace tuxape {

// The CPC Dandanator mini (Dandare, 2018): a cartridge for the expansion port
// with 512K of flash memory (an SST39SF040), 32 slots of 16K. It has no port
// and no ROM number: its CPLD watches the Z80's opcode fetches, and a slot it
// is told to show replaces whatever the machine would read in a quarter of
// memory, ROM or RAM; writes still go to the RAM. What is here follows the
// project's "Development & Emulation documentation" (v1.0, August 2018) and,
// where that says less, the VHDL of its CPLD.
//
// A command is three or more &FD prefixes then LD (IY+n),B, LD (IY+n),C or
// LD (IY+n),A (opcodes &70, &71, &77 after the last &FD), the register's
// value being what the instruction writes:
// - B: zone 0 (at &0000 or &8000): bits 4-0 its slot, bit 5 set to hide it;
// - C: zone 1 (at &4000 or &C000), the same;
// - A with bit 7 clear: bit 0 the serial line read by LD A,(HL), bit 1 the
//   flash written to, bit 2 the line sent to the USB chip, bits 4-3 the slot
//   (28 to 31) zone 0 takes when following the ROM enable;
// - A with bit 7 set: bit 6 waits for a RET (&C9 fetched) to act, bit 5
//   takes no more commands until a reset, bit 4 (on a RET only) shows the
//   zones in the ROM areas only while the machine reads ROM there, bits 3-2
//   the zones' halves (zone 1 at &C000, zone 0 at &8000), bits 1-0 hide
//   zone 1 and zone 0.
// Any command in between cancels one that waits for a RET.
//
// The cartridge's three buttons: reset, and two read as the machine starts,
// one to start from slot 31 (where test ROMs go), one to leave the machine's
// own firmware and BASIC in place.
//
// The USB link (a CH340G driven by bit-banging) is not emulated: what is
// sent goes nowhere, and the line read is always idle.
class Dandanator {
public:
    static constexpr int kSlotSize = 0x4000;
    static constexpr int kSlots = 32;
    static constexpr size_t kSize = static_cast<size_t>(kSlots) * kSlotSize;

    // What the machine starts on: the cartridge's slot 0, its slot 31 (left
    // button held), or the machine's own ROMs (middle button held).
    enum class Boot : uint8_t { Cartridge, Slot31, Machine };

    Dandanator();

    // The flash, all 512K: an image shorter than that leaves the rest
    // erased (&FF). A file of a cartridge (.des, .rom) is just that.
    std::span<const uint8_t> flash() const { return flash_; }
    void setFlash(std::span<const uint8_t> image);
    // Whether the machine has written to the flash since it was set.
    bool changed() const { return changed_; }
    void clearChanged() { changed_ = false; }

    Boot boot() const { return boot_; }
    void setBoot(Boot boot) { boot_ = boot; }

    // The machine is reset: the cartridge starts again, as its buttons say.
    void reset();

    // An opcode fetched (an M1 cycle), from wherever it was read. True when
    // what the cartridge shows has changed.
    bool fetched(uint8_t opcode);
    // A byte written. True when what the cartridge shows has changed.
    bool written(uint16_t address, uint8_t value);

    // The slot shown in a quarter of memory (0 to 3) for a read, or -1 for
    // none: the machine's own memory is read there.
    int shownSlot(int quarter, bool lowerRom, bool upperRom) const;
    const uint8_t* slot(int number) const { return &flash_[static_cast<size_t>(number & 31) * kSlotSize]; }

    // What its CPLD holds, for the debugger.
    struct State {
        uint8_t slot[2] = {};
        bool shown[2] = {};      // the zone's chip enable
        bool high[2] = {};       // zone 0 at &8000, zone 1 at &C000
        bool followRom = false;  // zones in the ROM areas only while ROM is read
        uint8_t followSlot = 28; // the slot zone 0 takes then
        bool commandsOff = false;
        bool waitingRet = false;
        bool flashWrites = false;
        bool serialIn = false;
        bool serialOut = true;   // the line to the USB chip, idle high
    };
    State state() const;

private:
    std::vector<uint8_t> flash_;
    bool changed_ = false;
    Boot boot_ = Boot::Cartridge;

    uint8_t slot_[2] = {};
    bool hidden_[2] = {false, true};
    bool high_[2] = {};
    bool followRom_ = false;
    uint8_t followZone_ = 0;  // bits 1-0 of zone 0's slot when following
    bool commandsOff_ = false;
    bool flashWrites_ = false;
    bool serialIn_ = false;
    bool serialOut_ = true;

    // A configuration waiting for a RET.
    bool retWait_ = false;
    bool delayedOff_ = false;
    bool delayedFollow_ = false;
    bool delayedHigh_[2] = {};
    bool delayedHidden_[2] = {};

    int prefixes_ = 0;       // &FD fetched in a row
    uint8_t pending_ = 0;    // a command's opcode, its value still to come

    int flashStep_ = 0;      // where the flash chip is in a command sequence

    void command(uint8_t opcode, uint8_t value);
    void writeFlash(uint32_t address, uint8_t value);
};

}  // namespace tuxape
