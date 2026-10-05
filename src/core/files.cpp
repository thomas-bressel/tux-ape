#include "core/files.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace tuxape {

std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return std::nullopt;
    return data;
}

bool writeFile(const std::filesystem::path& path, std::span<const uint8_t> data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out.flush());
}

namespace {

void putU32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(v >> shift));
}

uint32_t crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
    }
    return ~crc;
}

void putChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body)
{
    putU32(out, static_cast<uint32_t>(body.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    putU32(out, crc32(&out[start], out.size() - start));
}

}  // namespace

bool writePng(const std::filesystem::path& path, const uint32_t* pixels, int width, int height)
{
    // Scanlines, each with a "no filter" byte in front.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            const uint32_t p = pixels[y * width + x];
            raw.push_back(static_cast<uint8_t>(p >> 16));
            raw.push_back(static_cast<uint8_t>(p >> 8));
            raw.push_back(static_cast<uint8_t>(p));
        }
    }

    // A zlib stream made of stored (uncompressed) deflate blocks.
    std::vector<uint8_t> zlib = {0x78, 0x01};
    uint32_t a = 1, b = 0;  // Adler-32
    for (size_t pos = 0; pos < raw.size();) {
        const size_t len = std::min<size_t>(raw.size() - pos, 0xFFFF);
        zlib.push_back(pos + len == raw.size() ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(len));
        zlib.push_back(static_cast<uint8_t>(len >> 8));
        zlib.push_back(static_cast<uint8_t>(~len));
        zlib.push_back(static_cast<uint8_t>(~len >> 8));
        for (size_t i = 0; i < len; ++i) {
            a = (a + raw[pos + i]) % 65521;
            b = (b + a) % 65521;
        }
        zlib.insert(zlib.end(), raw.begin() + static_cast<ptrdiff_t>(pos),
                    raw.begin() + static_cast<ptrdiff_t>(pos + len));
        pos += len;
    }
    putU32(zlib, b << 16 | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> header;
    putU32(header, static_cast<uint32_t>(width));
    putU32(header, static_cast<uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB, no interlace
    putChunk(png, "IHDR", header);
    putChunk(png, "IDAT", zlib);
    putChunk(png, "IEND", {});
    return writeFile(path, png);
}

}  // namespace tuxape
