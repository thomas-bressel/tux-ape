#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/disc.h"

namespace tuxape {

// A file of a disc's directory.
struct DiscFile {
    std::string name;  // "NAME.EXT", in capitals, without the padding
    int user = 0;
    int size = 0;      // in bytes, as the directory counts it: whole records of 128
    bool readOnly = false;
    bool system = false;  // hidden from CAT

    bool operator==(const DiscFile&) const = default;
};

// The files on a disc in one of the CP/M formats the CPC uses (AMSDOS's
// DATA, SYSTEM and IBM, and the larger ones of ROMDOS, ParaDOS and others):
// its directory read, and files put on, taken off, renamed and deleted,
// without the machine having a hand in it.
class DiscFiles {
public:
    // The format is told from the disc's first track. valid() is false for
    // a disc that is not in a format known here, or not formatted.
    explicit DiscFiles(Disc& disc);
    bool valid() const { return format_ != nullptr; }
    const DiscFormat* format() const { return format_; }

    // The files, in the order of the directory. Files of all users.
    std::vector<DiscFile> list() const;
    int freeBytes() const;

    // What a file holds: all its records, to the last one.
    std::optional<std::vector<uint8_t>> read(const DiscFile& file) const;
    // Puts a file on the disc, in place of one of the same name and user.
    // False, with the disc as it was, if the name will not do or there is
    // no room.
    bool write(const std::string& name, std::span<const uint8_t> data, int user = 0);
    bool remove(const DiscFile& file);
    bool rename(const DiscFile& file, const std::string& name);
    bool setAttributes(const DiscFile& file, bool readOnly, bool system);

    // A name as the directory holds it: eight characters and three, in
    // capitals. Nothing for a name that cannot be one.
    static std::optional<std::string> directoryName(const std::string& name);

private:
    Disc& disc_;
    const DiscFormat* format_ = nullptr;
    const DiscFormat* chosen_ = nullptr;

    int blockSize() const { return 128 << format_->blockShift; }
    int directoryBlocks() const;
    bool wideBlocks() const { return format_->lastBlock > 255; }
    uint8_t* sector(int index) const;
    std::vector<uint8_t> readBlock(int block) const;
    void writeBlock(int block, std::span<const uint8_t> data);
    std::vector<uint8_t> directory() const;
    void setDirectory(const std::vector<uint8_t>& entries);
    std::vector<bool> usedBlocks(const std::vector<uint8_t>& entries) const;
};

// The 128 bytes AMSDOS puts ahead of the files it writes (all but ASCII
// ones): the file's type, where it loads and its true length.
struct AmsdosHeader {
    uint8_t type = 2;  // 0 BASIC, 1 protected BASIC, 2 binary
    uint16_t loadAddress = 0;
    uint16_t entryAddress = 0;
    int length = 0;
};
// Whether a file starts with such a header: its checksum tells.
std::optional<AmsdosHeader> amsdosHeader(std::span<const uint8_t> file);
std::vector<uint8_t> makeAmsdosHeader(const std::string& name, const AmsdosHeader& header);

}  // namespace tuxape
