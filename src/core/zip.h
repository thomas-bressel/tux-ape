#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tuxape {

// A ZIP archive, for reading: the files it holds and what is in them.
// Collections of CPC programs are mostly kept one archive to a program.
class ZipArchive {
public:
    struct Entry {
        std::string name;   // as the archive has it, with '/' between folders
        uint32_t size = 0;  // once taken out
        uint32_t compressedSize = 0;
        uint32_t crc = 0;
        uint32_t offset = 0;
        uint16_t method = 0;
        uint16_t flags = 0;
    };

    // Reads the archive's list of files, and no more of it. Nothing for a
    // file that is not a ZIP archive.
    static std::optional<ZipArchive> open(const std::filesystem::path& path);

    // The files; folders are left out.
    const std::vector<Entry>& entries() const { return entries_; }

    // What a file holds. Nothing if it cannot be had: packed by a method
    // other than "stored" and "deflated", encrypted, damaged, or of a size
    // no CPC file has.
    std::optional<std::vector<uint8_t>> read(const Entry& entry) const;
    std::optional<std::vector<uint8_t>> read(std::string_view name) const;

private:
    std::filesystem::path path_;
    std::vector<Entry> entries_;
};

}  // namespace tuxape
