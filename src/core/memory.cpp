#include "core/memory.h"

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

    ram_.assign(static_cast<size_t>(kPageSize) * (1 + pages), 0);
    ramConfig_ = 0xC0;
    ramPage_ = 0;
    remap();
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

void Memory::reset()
{
    lowerEnabled_ = upperEnabled_ = true;
    upperSelected_ = 0;
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
