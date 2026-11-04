#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tuxape {

// A cartridge of the Plus machines and of the GX4000: pages of 16K, as a
// CPR file holds them. The ASIC reaches 32 (512K); beyond, up to the
// hundred a CPR file can name (cb00 to cb99), they are in slots of 32
// (see Memory::setCartridge).
struct Cartridge {
    static constexpr int kPageSize = 0x4000;
    static constexpr int kMaxPages = 32;
    static constexpr int kMaxLargePages = 100;

    // Whole pages, one after the other. Pages the file leaves out between
    // two others are full of FF.
    std::vector<uint8_t> data;

    int pages() const { return static_cast<int>(data.size() / kPageSize); }

    // A CPR file is a RIFF file of the kind "AMS!", with a chunk "cb00" to
    // "cb31" for each page, or to "cb99" for a larger one. Nothing for a
    // file that is not one, or that has no page.
    static std::optional<Cartridge> parseCpr(std::span<const uint8_t> file);
};

}  // namespace tuxape
