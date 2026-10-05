#include "core/zip.h"

#include <algorithm>
#include <fstream>

#include <zlib.h>

namespace tuxape {

namespace {

constexpr uint32_t kLocalFile = 0x04034B50;
constexpr uint32_t kCentralFile = 0x02014B50;
constexpr uint32_t kEndOfDirectory = 0x06054B50;
// The largest file taken out of an archive. Disc images, tapes and
// snapshots are a few megabytes at most.
constexpr uint32_t kLargest = 64u << 20;

uint32_t word(const uint8_t* at)
{
    return static_cast<uint32_t>(at[0] | at[1] << 8);
}

uint32_t quad(const uint8_t* at)
{
    return word(at) | word(at + 2) << 16;
}

// A piece of the file; shorter than asked for if the file is.
std::vector<uint8_t> piece(std::ifstream& in, uint64_t offset, size_t size)
{
    std::vector<uint8_t> bytes(size);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    bytes.resize(static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    return bytes;
}

}  // namespace

std::optional<ZipArchive> ZipArchive::open(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return std::nullopt;
    const uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    if (fileSize < 22)
        return std::nullopt;
    // The record that closes the archive is at its very end, before a
    // comment of 64K at most.
    const size_t tailSize = static_cast<size_t>(std::min<uint64_t>(fileSize, 22 + 0xFFFF));
    const std::vector<uint8_t> tail = piece(in, fileSize - tailSize, tailSize);
    size_t end = tail.size();
    for (size_t at = tail.size() < 22 ? 0 : tail.size() - 21; at-- > 0;)
        if (quad(&tail[at]) == kEndOfDirectory) {
            end = at;
            break;
        }
    if (end == tail.size())
        return std::nullopt;
    const unsigned count = word(&tail[end + 10]);
    const uint32_t directorySize = quad(&tail[end + 12]), directoryOffset = quad(&tail[end + 16]);
    if (static_cast<uint64_t>(directoryOffset) + directorySize > fileSize)
        return std::nullopt;
    const std::vector<uint8_t> directory = piece(in, directoryOffset, directorySize);

    ZipArchive archive;
    archive.path_ = path;
    size_t at = 0;
    for (unsigned i = 0; i < count && at + 46 <= directory.size() && quad(&directory[at]) == kCentralFile; ++i) {
        const uint8_t* file = &directory[at];
        const size_t nameSize = word(file + 28);
        if (at + 46 + nameSize > directory.size())
            break;
        Entry entry;
        entry.name.assign(reinterpret_cast<const char*>(file + 46), nameSize);
        entry.flags = static_cast<uint16_t>(word(file + 8));
        entry.method = static_cast<uint16_t>(word(file + 10));
        entry.crc = quad(file + 16);
        entry.compressedSize = quad(file + 20);
        entry.size = quad(file + 24);
        entry.offset = quad(file + 42);
        if (!entry.name.empty() && entry.name.back() != '/')
            archive.entries_.push_back(std::move(entry));
        at += 46 + nameSize + word(file + 30) + word(file + 32);
    }
    return archive;
}

std::optional<std::vector<uint8_t>> ZipArchive::read(const Entry& entry) const
{
    const bool encrypted = entry.flags & 1;
    if (encrypted || (entry.method != 0 && entry.method != 8) || entry.size > kLargest || entry.compressedSize > kLargest)
        return std::nullopt;
    std::ifstream in(path_, std::ios::binary);
    if (!in)
        return std::nullopt;
    // The file's own header says how far its data is.
    const std::vector<uint8_t> header = piece(in, entry.offset, 30);
    if (header.size() != 30 || quad(header.data()) != kLocalFile)
        return std::nullopt;
    const uint64_t dataOffset = static_cast<uint64_t>(entry.offset) + 30 + word(&header[26]) + word(&header[28]);
    std::vector<uint8_t> packed = piece(in, dataOffset, entry.compressedSize);
    if (packed.size() != entry.compressedSize)
        return std::nullopt;

    std::vector<uint8_t> out;
    if (entry.method == 0) {
        out = std::move(packed);
    } else if (entry.size > 0) {
        out.resize(entry.size);
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)  // deflated data without a wrapper
            return std::nullopt;
        stream.next_in = packed.data();
        stream.avail_in = static_cast<uInt>(packed.size());
        stream.next_out = out.data();
        stream.avail_out = static_cast<uInt>(out.size());
        const int result = inflate(&stream, Z_FINISH);
        const bool whole = result == Z_STREAM_END && stream.total_out == out.size();
        inflateEnd(&stream);
        if (!whole)
            return std::nullopt;
    }
    if (out.size() != entry.size || crc32(0, out.data(), static_cast<uInt>(out.size())) != entry.crc)
        return std::nullopt;
    return out;
}

std::optional<std::vector<uint8_t>> ZipArchive::read(std::string_view name) const
{
    for (const Entry& entry : entries_)
        if (entry.name == name)
            return read(entry);
    return std::nullopt;
}

}  // namespace tuxape
