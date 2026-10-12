#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tuxape {

// The X-MEM (ACME, 2014), a card for the expansion port: 512K of RAM, and
// 512K of flash memory that holds 32 ROMs of 16K and can stand for the
// machine's own firmware and BASIC. What is here is its notice ("X-MEM Get
// Started!") and what its own flashing routine does.
//
// The RAM is eight 64K banks chosen at &7Fxx with &C4 to &FF, as a
// Dk'Tronics expansion's are; see Memory::setRam.
//
// A ROM is chosen with the byte written to &DFxx, as ever: its bits 4 to 0
// say which, bit 5 is not looked at, and with bit 7 the card keeps quiet.
// Two of its 32 places are not ROMs of their numbers: ROM 7 is always the
// machine's own (its AMSDOS), and ROM 0 is the machine's BASIC unless the
// card boots. Place 7 holds the lower ROM and place 0 the BASIC the card
// boots with; bit 6 of the byte shows these two in the ROM area whatever
// the switches say, which is how they are written (&47 and &40).
//
// The flash is an SST39SF040. It is written through the ROM area: a byte
// written at &C000-&FFFF goes to the chip at the address the chosen ROM
// has there, the RAM getting it as well. The chip takes no notice of such
// bytes until it has had its three command bytes, at addresses &5555 and
// &2AAA of its first 32K: a program reaches those by choosing a ROM with
// an odd number, then an even one.
class Xmem {
public:
    static constexpr int kRomSize = 0x4000;
    static constexpr int kRoms = 32;
    static constexpr size_t kFlashSize = static_cast<size_t>(kRoms) * kRomSize;
    static constexpr int kLowerPlace = 7;  // where the lower ROM is kept
    static constexpr int kBasicPlace = 0;
    // What the chip answers when asked who it is.
    static constexpr uint8_t kMaker = 0xBF, kDevice = 0xB7;

    // The card's three switches and its jumper.
    struct Switches {
        // "CPC 464/6128": on 6128 the first of the eight banks is left to
        // the machine's own second 64K.
        bool ram6128 = true;
        // "BOOT CPC/ROM": on ROM the lower ROM and ROM 0 are the card's.
        bool bootRom = false;
        // "ROM LOCK/FREE": on FREE the flash can be written.
        bool romFree = false;
        // "READ ROM NO/YES": on NO the machine sees none of the card's
        // ROMs, which is how one that no longer boots is rescued.
        bool readRom = true;

        bool operator==(const Switches&) const = default;
    };

    Xmem();

    const Switches& switches() const { return switches_; }
    void setSwitches(const Switches& switches) { switches_ = switches; }

    // The flash, all 512K of it: erased (&FF) to begin with. An image
    // shorter than that leaves the rest erased.
    std::span<const uint8_t> flash() const { return flash_; }
    void setFlash(std::span<const uint8_t> image);
    // One of its 32 places, written as a whole: what a flasher would do.
    void setRom(int place, std::span<const uint8_t> image);
    // What one of its places holds, as a ROM tells of itself: its kind
    // (0 a foreground ROM, 1 a background one, 2 an extension, &80 the
    // BASIC), its version, and its name, the first of its table of
    // commands. A firmware (the lower ROM) has no such header and is known
    // by how it starts. Nothing there: `present` is false.
    struct RomInfo {
        bool present = false;
        bool firmware = false;
        uint8_t kind = 0, mark = 0, version = 0, modification = 0;
        std::string name;
    };
    static RomInfo romInfo(std::span<const uint8_t> rom);
    RomInfo romInfo(int place) const;

    // Whether the flash has been written since this was last cleared: for
    // whoever keeps it in a file.
    bool changed() const { return changed_; }
    void clearChanged() { changed_ = false; }

    // Power on: the chip forgets the command it was in the middle of.
    void reset();

    // The 64K banks of RAM the card brings, bit n for bank n.
    uint8_t ramBanks() const { return switches_.ram6128 ? 0xFE : 0xFF; }

    // What the card puts in the upper ROM's place for a ROM select byte,
    // and in the lower ROM's: null where it leaves the machine alone.
    const uint8_t* upperRom(uint8_t select) const;
    const uint8_t* lowerRom() const;

    // A byte written at `offset` of the ROM area with that ROM selected.
    // True when what the card shows there has changed other than by that
    // byte: the chip has started, or stopped, answering with its name.
    bool write(uint8_t select, uint16_t offset, uint8_t value);

private:
    // Where the chip is in a command.
    enum class State : uint8_t {
        Read,        // the ROMs' contents
        Unlock1,     // &AA had at &5555
        Unlock2,     // then &55 at &2AAA: the command byte is next
        Program,     // &A0: the next byte written is programmed
        Erase,       // &80: a second pair of &AA and &55 is next
        EraseUnlock1,
        EraseUnlock2,  // then &30 at a sector, or &10 for the whole chip
    };

    bool selected(uint8_t select) const;

    Switches switches_;
    std::vector<uint8_t> flash_;
    std::vector<uint8_t> name_;  // 16K of what the chip answers in place of its contents
    State state_ = State::Read;
    bool naming_ = false;  // answering with its name ("software ID")
    bool changed_ = false;
};

}  // namespace tuxape
