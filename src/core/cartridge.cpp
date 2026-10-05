#include "core/cartridge.h"

#include <algorithm>
#include <cstring>

namespace tuxape {

std::optional<Cartridge> Cartridge::parseCpr(std::span<const uint8_t> file)
{
    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 || std::memcmp(file.data() + 8, "AMS!", 4) != 0)
        return std::nullopt;
    Cartridge cartridge;
    size_t pos = 12;
    while (pos + 8 <= file.size()) {
        const uint8_t* chunk = &file[pos];
        const size_t size = static_cast<size_t>(chunk[4] | chunk[5] << 8 | chunk[6] << 16) | static_cast<size_t>(chunk[7]) << 24;
        const size_t held = std::min(size, file.size() - pos - 8);
        const bool page = chunk[0] == 'c' && chunk[1] == 'b' && chunk[2] >= '0' && chunk[2] <= '9' && chunk[3] >= '0'
                          && chunk[3] <= '9';
        const int number = (chunk[2] - '0') * 10 + (chunk[3] - '0');
        if (page && number < kMaxPages) {
            const size_t start = static_cast<size_t>(number) * kPageSize;
            if (cartridge.data.size() < start + kPageSize)
                cartridge.data.resize(start + kPageSize, 0xFF);
            // A page given short ends in zeroes; one given long is cut.
            std::fill_n(cartridge.data.begin() + static_cast<ptrdiff_t>(start), kPageSize, 0);
            std::copy_n(chunk + 8, std::min<size_t>(held, kPageSize), cartridge.data.begin() + static_cast<ptrdiff_t>(start));
        }
        // Chunks start on even addresses.
        pos += 8 + size + (size & 1);
    }
    if (cartridge.data.empty())
        return std::nullopt;
    return cartridge;
}

}  // namespace tuxape
