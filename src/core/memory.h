#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace tuxape {

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
    void setRam(RamExpansion expansion, bool siliconDisc);
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
    void write(uint16_t addr, uint8_t value) { writeMap_[addr >> 14][addr & 0x3FFF] = value; }

    // The first 64K, which is what the video circuitry always reads.
    const uint8_t* baseRam() const { return ram_.data(); }
    uint8_t* baseRam() { return ram_.data(); }

    // State for the debugger.
    bool lowerRomEnabled() const { return lowerEnabled_; }
    bool upperRomEnabled() const { return upperEnabled_; }
    uint8_t selectedUpperRom() const { return upperSelected_; }
    uint8_t ramBank() const { return ramConfig_; }  // as written, e.g. 0xC4

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
    uint8_t ramConfig_ = 0xC0;
    uint8_t ramPage_ = 0;

    const uint8_t* readMap_[4] = {};
    uint8_t* writeMap_[4] = {};

    void remap();
};

}  // namespace tuxape
