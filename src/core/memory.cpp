#include "core/memory.h"

#include "core/asic.h"

#include <algorithm>

namespace tuxape {

namespace {

constexpr int kPageSize = 0x10000;
constexpr int kMaxPages = 64;  // 4M

// Which 16K block appears in each quarter of the address space for the eight
// banking configurations. 0-3 are the base RAM, 4-7 the selected page.
constexpr uint8_t kConfigs[8][4] = {
    {0, 1, 2, 3},
    {0, 1, 2, 7},
    {4, 5, 6, 7},
    {0, 3, 2, 7},
    {0, 4, 2, 3},
    {0, 5, 2, 3},
    {0, 6, 2, 3},
    {0, 7, 2, 3},
};

}  // namespace

Memory::Memory()
    : lowerRom_(kBankSize, 0xFF)
    , upperRom_(static_cast<size_t>(kRomSlots) * kBankSize, 0xFF)
{
    setRam(RamExpansion::Internal, false);
}

void Memory::setRam(RamExpansion expansion, bool siliconDisc)
{
    expansion_ = expansion;
    siliconDisc_ = siliconDisc;

    const std::vector<uint8_t> oldRam = std::move(ram_);
    const std::vector<int> oldOffset = pageOffset_;

    pageOffset_.assign(kMaxPages, -1);
    int pages = 0;
    auto fit = [&](int first, int count) {
        for (int p = first; p < first + count; ++p)
            if (pageOffset_[p] < 0)
                pageOffset_[p] = kPageSize * (1 + pages++);
    };
    switch (expansion) {
    case RamExpansion::None: break;
    case RamExpansion::Internal: fit(0, 1); break;
    case RamExpansion::Dk256: fit(0, 4); break;
    case RamExpansion::Yarek4M: fit(0, kMaxPages); break;
    }
    if (siliconDisc)
        fit(4, 4);

    // What was in the base RAM and in the pages that stay fitted is kept:
    // RAM can be added to or taken from a running machine.
    ram_.assign(static_cast<size_t>(kPageSize) * (1 + pages), 0);
    if (!oldRam.empty()) {
        std::copy_n(oldRam.begin(), kPageSize, ram_.begin());
        for (int p = 0; p < kMaxPages && p < static_cast<int>(oldOffset.size()); ++p)
            if (oldOffset[p] >= 0 && pageOffset_[p] >= 0)
                std::copy_n(oldRam.begin() + oldOffset[p], kPageSize, ram_.begin() + pageOffset_[p]);
    }
    ramConfig_ = 0xC0;
    ramPage_ = 0;
    remap();
}

int Memory::ramSizeKb(RamExpansion expansion, bool siliconDisc)
{
    switch (expansion) {
    case RamExpansion::None: return siliconDisc ? 320 : 64;
    case RamExpansion::Internal: return siliconDisc ? 384 : 128;
    case RamExpansion::Dk256: return siliconDisc ? 576 : 320;
    case RamExpansion::Yarek4M: return 4160;  // the Silicon Disc's place is part of it
    }
    return 64;
}

int Memory::ramSizeKb() const
{
    return static_cast<int>(ram_.size() / 1024);
}

void Memory::setLowerRom(std::span<const uint8_t> image)
{
    std::fill(lowerRom_.begin(), lowerRom_.end(), 0xFF);
    std::copy_n(image.begin(), std::min<size_t>(image.size(), kBankSize), lowerRom_.begin());
}

void Memory::setUpperRom(int slot, std::span<const uint8_t> image)
{
    if (slot < 0 || slot >= kRomSlots)
        return;
    const auto dest = upperRom_.begin() + static_cast<ptrdiff_t>(slot) * kBankSize;
    std::fill_n(dest, kBankSize, 0xFF);
    std::copy_n(image.begin(), std::min<size_t>(image.size(), kBankSize), dest);
    upperPresent_[slot] = !image.empty();
    remap();
}

void Memory::setCartridge(std::span<const uint8_t> pages, bool discRom)
{
    // Address lines a cartridge does not use bring its pages round again:
    // it is filled out to a power of two, with FF where it has nothing.
    size_t count = 0;
    if (!pages.empty())
        for (count = 1; count * kBankSize < pages.size();)
            count *= 2;
    cartridge_.assign(count * kBankSize, 0xFF);
    std::copy_n(pages.begin(), std::min(pages.size(), cartridge_.size()), cartridge_.begin());
    discRom_ = discRom;
    rmr2_ = 0;
    remap();
}

void Memory::setRmr2(uint8_t value)
{
    rmr2_ = value & 0x1F;
    remap();
}

void Memory::writeRegister(uint16_t addr, uint8_t value)
{
    asic_->write(addr, value);
}

void Memory::reset()
{
    lowerEnabled_ = upperEnabled_ = true;
    upperSelected_ = 0;
    rmr2_ = 0;
    ramConfig_ = 0xC0;
    ramPage_ = 0;
    remap();
}

void Memory::clearRam()
{
    std::fill(ram_.begin(), ram_.end(), 0);
}

void Memory::setRomEnables(bool lower, bool upper)
{
    if (lower == lowerEnabled_ && upper == upperEnabled_)
        return;
    lowerEnabled_ = lower;
    upperEnabled_ = upper;
    remap();
}

void Memory::selectUpperRom(uint8_t number)
{
    upperSelected_ = number;
    remap();
}

uint8_t* Memory::ramPage(int page)
{
    if (page == 0)
        return ram_.data();
    if (page < 0 || page > kMaxPages || pageOffset_[page - 1] < 0)
        return nullptr;
    return &ram_[static_cast<size_t>(pageOffset_[page - 1])];
}

void Memory::selectRamBank(uint8_t value, uint8_t portHigh)
{
    if (expansion_ == RamExpansion::None && !siliconDisc_)
        return;
    ramConfig_ = value;
    // The 128K machine's own banking ignores the page bits. Expansions use
    // them, and the 4M one also decodes ports &78xx to &7Fxx.
    int page = 0;
    if (expansion_ != RamExpansion::Internal || siliconDisc_)
        page = (value >> 3) & 7;
    if (expansion_ == RamExpansion::Yarek4M)
        page |= (~portHigh & 7) << 3;
    ramPage_ = static_cast<uint8_t>(page);
    remap();
}

void Memory::remap()
{
    // Selecting a page that is not fitted leaves the base RAM in place.
    const int offset = pageOffset_[ramPage_];
    const uint8_t* config = kConfigs[offset < 0 ? 0 : ramConfig_ & 7];
    for (int q = 0; q < 4; ++q) {
        const int block = config[q];
        uint8_t* bank = block < 4 ? &ram_[static_cast<size_t>(block) * kBankSize]
                                  : &ram_[static_cast<size_t>(offset) + static_cast<size_t>(block - 4) * kBankSize];
        readMap_[q] = writeMap_[q] = bank;
    }
    registersMapped_ = false;
    if (!cartridge_.empty()) {
        const size_t pages = cartridge_.size() / kBankSize;
        auto page = [&](unsigned number) { return &cartridge_[(number & (pages - 1)) * kBankSize]; };
        const int where = rmr2_ >> 3;
        if (lowerEnabled_)
            readMap_[where == 3 ? 0 : where] = page(rmr2_ & 7);
        if (where == 3 && asic_) {
            readMap_[1] = asic_->page();
            registersMapped_ = true;
        }
        if (upperEnabled_) {
            const int slot = upperSelected_ & romSlotMask_;
            if (upperSelected_ <= romSlotMask_ && upperPresent_[slot])
                readMap_[3] = &upperRom_[static_cast<size_t>(slot) * kBankSize];
            else
                readMap_[3] = page(upperSelected_ & 0x80 ? upperSelected_ & 0x1F : upperSelected_ == 7 && discRom_ ? 3 : 1);
        }
        return;
    }
    if (lowerEnabled_)
        readMap_[0] = lowerRom_.data();
    if (upperEnabled_) {
        // An empty slot shows BASIC, which sits in slot 0.
        int slot = upperSelected_ & romSlotMask_;
        if (upperSelected_ > romSlotMask_ || !upperPresent_[slot])
            slot = 0;
        readMap_[3] = &upperRom_[static_cast<size_t>(slot) * kBankSize];
    }
}

}  // namespace tuxape
