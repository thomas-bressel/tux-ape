#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace tuxape {

class Asic;

// RAM fitted beyond the first 64K, as offered by WinAPE's memory settings.
enum class RamExpansion : uint8_t {
    None,       // 64K machine (CPC464, CPC664, 464 Plus)
    Internal,   // 128K machine (CPC6128, 6128 Plus)
    Dk256,      // 64K + Dk'Tronics 256K RAM expansion
    Yarek4M,    // 64K + 4M expansion
};

// RAM, ROMs and the mapping of both into the Z80's 64K address space.
class Memory {
public:
    static constexpr int kBankSize = 0x4000;
    static constexpr int kRomSlots = 32;

    Memory();

    // Fitted RAM. The Silicon Disc adds the top 256K of the first 512K.
    // The base RAM and the pages fitted before and after keep their
    // contents; the banking register goes back to its reset state.
    void setRam(RamExpansion expansion, bool siliconDisc);
    // What a choice of RAM comes to, in K.
    static int ramSizeKb(RamExpansion expansion, bool siliconDisc);
    RamExpansion ramExpansion() const { return expansion_; }
    bool siliconDisc() const { return siliconDisc_; }
    int ramSizeKb() const;

    // ROM images. An empty image removes the ROM. Images shorter than 16K
    // are padded with FF, longer ones are cut.
    void setLowerRom(std::span<const uint8_t> image);
    void setUpperRom(int slot, std::span<const uint8_t> image);
    bool hasUpperRom(int slot) const { return upperPresent_[slot & (kRomSlots - 1)]; }
    // Highest ROM number the select latch can reach: 15, or 31 with a
    // 32-slot ROM board.
    void setRomSlotLimit(int count) { romSlotMask_ = count > 16 ? 31 : 15; }

    // The Plus: a cartridge takes the place of the ROMs. Its page 0 is the
    // lower ROM, or the page RMR2 names, shown where RMR2 says; the upper
    // ROM is page 1 (BASIC), page 3 for ROM 7 on a machine with a disc
    // drive (AMSDOS), or any page: ROM numbers 128 and up name it. A ROM
    // fitted on an expansion board still shows in its slot. No pages takes
    // the cartridge out.
    //
    // After a reset the upper ROM is not seen until one has been chosen
    // (a write to &DFxx): the top 16K is RAM till then, though the Gate
    // Array's bit says ROM. No Exit calls a subroutine before it has set
    // its stack, and finds its way back from &FFFD; Epyx World of Sports
    // and Eerie Forest choose a page and read it without ever having
    // touched that bit.
    void setCartridge(std::span<const uint8_t> pages, bool discRom);
    bool hasCartridge() const { return !cartridge_.empty(); }
    void setRmr2(uint8_t value);
    // The ASIC, whose page of registers RMR2 can show at &4000-&7FFF.
    void setAsic(Asic* asic) { asic_ = asic; }

    // The Multiface II: 8K of ROM at &0000 and 8K of RAM at &2000, which
    // take the place of what is there while it is paged in. What is
    // written under its ROM goes to the machine's RAM as ever. An empty
    // image takes the Multiface out; a shorter one is padded with FF.
    void setMultiface(std::span<const uint8_t> rom);
    bool hasMultiface() const { return !multiface_.empty(); }
    void pageMultiface(bool in);
    bool multifacePaged() const { return multifacePaged_; }
    // Its 8K of RAM; null without one.
    uint8_t* multifaceRam() { return multiface_.empty() ? nullptr : &multiface_[0x2000]; }

    void reset();
    void clearRam();

    // Gate Array ROM enables.
    void setRomEnables(bool lower, bool upper);
    // Upper ROM select latch (port &DFxx).
    void selectUpperRom(uint8_t number);
    // RAM banking register (port &7Fxx with the top two data bits set).
    // `portHigh` is the high byte of the port, which the 4M expansion decodes.
    void selectRamBank(uint8_t value, uint8_t portHigh);

    uint8_t read(uint16_t addr) const { return readMap_[addr >> 14][addr & 0x3FFF]; }
    void write(uint16_t addr, uint8_t value)
    {
        if (registersMapped_ && (addr & 0xC000) == 0x4000)
            writeRegister(addr, value);
        else if (multifacePaged_ && addr >= 0x2000 && addr < 0x4000)
            multiface_[addr] = value;
        else
            writeMap_[addr >> 14][addr & 0x3FFF] = value;
    }

    // The RAM at an address, whatever ROM or register is shown over it.
    uint8_t readRam(uint16_t addr) const { return writeMap_[addr >> 14][addr & 0x3FFF]; }

    // The 64K of one RAM page: 0 is the base RAM, 1 and up the expansion
    // pages as the banking register numbers them. Null where none is fitted.
    static constexpr int kRamPages = 65;
    uint8_t* ramPage(int page);
    const uint8_t* ramPage(int page) const { return const_cast<Memory*>(this)->ramPage(page); }

    // The first 64K, which is what the video circuitry always reads.
    const uint8_t* baseRam() const { return ram_.data(); }
    uint8_t* baseRam() { return ram_.data(); }

    // State for the debugger.
    bool lowerRomEnabled() const { return lowerEnabled_; }
    bool upperRomEnabled() const { return upperEnabled_; }
    uint8_t selectedUpperRom() const { return upperSelected_; }
    uint8_t ramBank() const { return ramConfig_; }  // as written, e.g. 0xC4
    uint8_t rmr2() const { return rmr2_; }
    // Whether the ASIC's page of registers is shown at &4000-&7FFF.
    bool registersMapped() const { return registersMapped_; }
    int romSlotCount() const { return romSlotMask_ + 1; }
    // Whether ROM 7 of the cartridge is its page 3 (a Plus with a drive).
    bool cartridgeDiscRom() const { return discRom_; }

    // Everything that decides what is where in the 64K, to look at memory
    // through another mapping for a moment and put the machine's own back
    // exactly: the RAM page of a large expansion is not all in the value
    // written to the banking register.
    struct Mapping {
        bool lowerRom = true, upperRom = true;
        uint8_t upperSelected = 0;
        uint8_t ramConfig = 0xC0, ramPage = 0;
        uint8_t rmr2 = 0;
        bool upperChosen = true;  // a cartridge's upper ROM, once one has been chosen
    };
    Mapping mapping() const { return {lowerEnabled_, upperEnabled_, upperSelected_, ramConfig_, ramPage_, rmr2_, upperChosen_}; }
    void setMapping(const Mapping& mapping);

private:
    RamExpansion expansion_ = RamExpansion::Internal;
    bool siliconDisc_ = false;

    std::vector<uint8_t> ram_;      // base 64K followed by the expansion pages
    std::vector<int> pageOffset_;   // offset in ram_ of each 64K expansion page, -1 if absent
    std::vector<uint8_t> lowerRom_;
    std::vector<uint8_t> upperRom_; // kRomSlots images of 16K
    bool upperPresent_[kRomSlots] = {};
    int romSlotMask_ = 15;

    bool lowerEnabled_ = true;
    bool upperEnabled_ = true;
    uint8_t upperSelected_ = 0;
    bool upperChosen_ = false;  // since the last reset (see setCartridge)
    uint8_t ramConfig_ = 0xC0;
    uint8_t ramPage_ = 0;

    const uint8_t* readMap_[4] = {};
    uint8_t* writeMap_[4] = {};

    std::vector<uint8_t> cartridge_;  // whole pages, a power of two of them
    bool discRom_ = false;
    uint8_t rmr2_ = 0;
    Asic* asic_ = nullptr;
    bool registersMapped_ = false;

    std::vector<uint8_t> multiface_;  // its ROM, then its RAM: 16K, or none
    bool multifacePaged_ = false;

    void remap();
    void remapMachine();
    void writeRegister(uint16_t addr, uint8_t value);
};

}  // namespace tuxape
