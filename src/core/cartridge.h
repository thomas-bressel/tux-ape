#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tuxape {

// A cartridge of the Plus machines and of the GX4000: pages of 16K, 32 at
// the most, as a CPR file holds them.
struct Cartridge {
    static constexpr int kPageSize = 0x4000;
    static constexpr int kMaxPages = 32;

    // Whole pages, one after the other. Pages the file leaves out between
    // two others are full of FF.
    std::vector<uint8_t> data;

    int pages() const { return static_cast<int>(data.size() / kPageSize); }

    // A CPR file is a RIFF file of the kind "AMS!", with a chunk "cb00" to
    // "cb31" for each page. Nothing for a file that is not one, or that has
    // no page.
    static std::optional<Cartridge> parseCpr(std::span<const uint8_t> file);
};

}  // namespace tuxape
