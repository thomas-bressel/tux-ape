#include "core/discfiles.h"

#include <algorithm>
#include <cstring>

namespace tuxape {

namespace {

constexpr int kSectorSize = 512;
constexpr int kEntrySize = 32;
constexpr uint8_t kUnused = 0xE5;

// The name of a directory entry, without its attribute bits.
std::string entryName(const uint8_t* entry)
{
    std::string name, extension;
    for (int i = 1; i <= 8; ++i)
        name += static_cast<char>(entry[i] & 0x7F);
    for (int i = 9; i <= 11; ++i)
        extension += static_cast<char>(entry[i] & 0x7F);
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    while (!extension.empty() && extension.back() == ' ')
        extension.pop_back();
    return extension.empty() ? name : name + "." + extension;
}

}  // namespace

DiscFiles::DiscFiles(Disc& disc)
    : disc_(disc)
{
    const DiscTrack* first = disc.track(0, 0);
    if (!first || first->sectors.empty())
        return;
    uint8_t lowest = 0xFF;
    for (const DiscSector& sector : first->sectors)
        lowest = std::min(lowest, sector.r);
    const int tracks = disc.cylinders() > 45 ? 80 : 40;
    // Several formats can have the same tracks and sector numbers and
    // differ in how they lay files out (ROMDOS D1, the PCW's and Vortex's
    // are three such): the one whose directory makes most sense is taken,
    // the first of the list if none says more than another.
    int best = -1;
    for (const DiscFormat& format : discFormats()) {
        if (format.firstSectorId != lowest || format.sectorsPerTrack != first->sectors.size()
            || format.sides != disc.sides() || (format.tracks > 45 ? 80 : 40) != tracks)
            continue;
        format_ = &format;
        int score = 0;
        const std::vector<uint8_t> entries = directory();
        for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize) {
            const uint8_t* entry = &entries[at];
            if (entry[0] == kUnused)
                continue;
            bool sound = entry[0] <= 15 && entry[15] <= 0x80;
            for (int i = 1; i <= 11 && sound; ++i)
                sound = (entry[i] & 0x7F) >= ' ' && (entry[i] & 0x7F) < 0x7F;
            for (int i = 0; i < 16 && sound; i += wideBlocks() ? 2 : 1)
                sound = (wideBlocks() ? entry[16 + i] | entry[17 + i] << 8 : entry[16 + i]) <= format.lastBlock;
            score += sound ? 1 : -1;
        }
        if (score > best) {
            best = score;
            chosen_ = &format;
        }
    }
    format_ = chosen_;
}

int DiscFiles::directoryBlocks() const
{
    return (format_->directoryEntries * kEntrySize + blockSize() - 1) / blockSize();
}

// A sector of the part of the disc that holds files, counted from its
// start. Tracks of a two-sided disc go side 0, side 1, side 0...
uint8_t* DiscFiles::sector(int index) const
{
    const int logicalTrack = format_->reservedTracks + index / format_->sectorsPerTrack;
    const uint8_t id = static_cast<uint8_t>(format_->firstSectorId + index % format_->sectorsPerTrack);
    DiscTrack* track = disc_.track(logicalTrack / format_->sides, logicalTrack % format_->sides);
    if (!track)
        return nullptr;
    for (DiscSector& candidate : track->sectors)
        if (candidate.r == id && candidate.data.size() >= kSectorSize)
            return candidate.data.data();
    return nullptr;
}

std::vector<uint8_t> DiscFiles::readBlock(int block) const
{
    std::vector<uint8_t> data(static_cast<size_t>(blockSize()), kUnused);
    for (int i = 0; i < blockSize() / kSectorSize; ++i)
        if (const uint8_t* from = sector(block * (blockSize() / kSectorSize) + i))
            std::memcpy(&data[static_cast<size_t>(i) * kSectorSize], from, kSectorSize);
    return data;
}

void DiscFiles::writeBlock(int block, std::span<const uint8_t> data)
{
    for (int i = 0; i < blockSize() / kSectorSize; ++i)
        if (uint8_t* to = sector(block * (blockSize() / kSectorSize) + i))
            std::memcpy(to, &data[static_cast<size_t>(i) * kSectorSize], kSectorSize);
    disc_.modified = true;
}

std::vector<uint8_t> DiscFiles::directory() const
{
    std::vector<uint8_t> entries;
    for (int block = 0; block < directoryBlocks(); ++block) {
        const std::vector<uint8_t> data = readBlock(block);
        entries.insert(entries.end(), data.begin(), data.end());
    }
    entries.resize(static_cast<size_t>(format_->directoryEntries) * kEntrySize);
    return entries;
}

void DiscFiles::setDirectory(const std::vector<uint8_t>& entries)
{
    std::vector<uint8_t> padded = entries;
    padded.resize(static_cast<size_t>(directoryBlocks()) * static_cast<size_t>(blockSize()), kUnused);
    for (int block = 0; block < directoryBlocks(); ++block)
        writeBlock(block, std::span<const uint8_t>(padded).subspan(static_cast<size_t>(block) * static_cast<size_t>(blockSize())));
}

std::vector<bool> DiscFiles::usedBlocks(const std::vector<uint8_t>& entries) const
{
    std::vector<bool> used(static_cast<size_t>(format_->lastBlock) + 1, false);
    for (int block = 0; block < directoryBlocks(); ++block)
        used[static_cast<size_t>(block)] = true;
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize) {
        if (entries[at] == kUnused)
            continue;
        for (int i = 0; i < 16; i += wideBlocks() ? 2 : 1) {
            const size_t block = wideBlocks() ? entries[at + 16 + i] | entries[at + 17 + i] << 8 : entries[at + 16 + i];
            if (block != 0 && block < used.size())
                used[block] = true;
        }
    }
    return used;
}

std::vector<DiscFile> DiscFiles::list() const
{
    std::vector<DiscFile> files;
    if (!valid())
        return files;
    const std::vector<uint8_t> entries = directory();
    // An entry holds so many records at the most; a file's size is where
    // its last entry ends.
    const int extentRecords = 128 * (format_->extentMask + 1);
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize) {
        const uint8_t* entry = &entries[at];
        if (entry[0] > 15)
            continue;
        DiscFile file;
        file.name = entryName(entry);
        file.user = entry[0];
        file.readOnly = entry[9] & 0x80;
        file.system = entry[10] & 0x80;
        const int extent = (entry[14] & 0x3F) << 5 | (entry[12] & 0x1F);
        file.size = ((extent / (format_->extentMask + 1)) * extentRecords + (extent & format_->extentMask) * 128 + entry[15]) * 128;
        auto known = std::find_if(files.begin(), files.end(),
                                  [&](const DiscFile& other) { return other.name == file.name && other.user == file.user; });
        if (known == files.end())
            files.push_back(file);
        else
            known->size = std::max(known->size, file.size);
    }
    return files;
}

int DiscFiles::freeBytes() const
{
    if (!valid())
        return 0;
    const std::vector<bool> used = usedBlocks(directory());
    return static_cast<int>(std::count(used.begin(), used.end(), false)) * blockSize();
}

std::optional<std::vector<uint8_t>> DiscFiles::read(const DiscFile& file) const
{
    if (!valid())
        return std::nullopt;
    const std::vector<uint8_t> entries = directory();
    const int perEntry = wideBlocks() ? 8 : 16;
    // The file's blocks, in the order of its entries' extent numbers.
    std::vector<int> blocks;
    bool found = false;
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize) {
        const uint8_t* entry = &entries[at];
        if (entry[0] != file.user || entryName(entry) != file.name)
            continue;
        found = true;
        const size_t index = static_cast<size_t>(((entry[14] & 0x3F) << 5 | (entry[12] & 0x1F)) / (format_->extentMask + 1));
        if (blocks.size() < (index + 1) * static_cast<size_t>(perEntry))
            blocks.resize((index + 1) * static_cast<size_t>(perEntry), 0);
        for (int i = 0; i < perEntry; ++i)
            blocks[index * static_cast<size_t>(perEntry) + static_cast<size_t>(i)] =
                wideBlocks() ? entry[16 + i * 2] | entry[17 + i * 2] << 8 : entry[16 + i];
    }
    if (!found)
        return std::nullopt;
    std::vector<uint8_t> data;
    for (const int block : blocks) {
        if (data.size() >= static_cast<size_t>(file.size))
            break;
        const std::vector<uint8_t> content = block > 0 && block <= format_->lastBlock
                                                 ? readBlock(block)
                                                 : std::vector<uint8_t>(static_cast<size_t>(blockSize()), 0);
        data.insert(data.end(), content.begin(), content.end());
    }
    data.resize(static_cast<size_t>(file.size));
    return data;
}

std::optional<std::string> DiscFiles::directoryName(const std::string& name)
{
    const size_t dot = name.rfind('.');
    std::string stem = name.substr(0, dot), extension = dot == std::string::npos ? "" : name.substr(dot + 1);
    if (stem.empty() || stem.size() > 8 || extension.size() > 3)
        return std::nullopt;
    for (std::string* part : {&stem, &extension})
        for (char& c : *part) {
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
            // What CP/M keeps for itself.
            if (c <= ' ' || c > '~' || std::strchr("<>.,;:=?*[]", c))
                return std::nullopt;
        }
    return extension.empty() ? stem : stem + "." + extension;
}

bool DiscFiles::write(const std::string& name, std::span<const uint8_t> data, int user)
{
    const auto proper = directoryName(name);
    if (!valid() || !proper || user < 0 || user > 15)
        return false;
    std::vector<uint8_t> entries = directory();
    // One of the same name goes; its blocks are free for the new one.
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize)
        if (entries[at] == user && entryName(&entries[at]) == *proper)
            entries[at] = kUnused;

    const int perEntry = wideBlocks() ? 8 : 16;
    const size_t size = static_cast<size_t>(blockSize());
    const size_t blocksNeeded = (data.size() + size - 1) / size;
    const size_t entriesNeeded = std::max<size_t>(1, (blocksNeeded + static_cast<size_t>(perEntry) - 1) / static_cast<size_t>(perEntry));
    std::vector<bool> used = usedBlocks(entries);
    std::vector<int> blocks;
    for (size_t block = 0; block < used.size() && blocks.size() < blocksNeeded; ++block)
        if (!used[block])
            blocks.push_back(static_cast<int>(block));
    std::vector<size_t> slots;
    for (size_t at = 0; at + kEntrySize <= entries.size() && slots.size() < entriesNeeded; at += kEntrySize)
        if (entries[at] == kUnused)
            slots.push_back(at);
    if (blocks.size() < blocksNeeded || slots.size() < entriesNeeded)
        return false;

    const size_t dot = proper->find('.');
    const std::string stem = proper->substr(0, dot), extension = dot == std::string::npos ? "" : proper->substr(dot + 1);
    const int records = static_cast<int>((data.size() + 127) / 128);
    const int entryRecords = 128 * (format_->extentMask + 1);
    for (size_t e = 0; e < entriesNeeded; ++e) {
        uint8_t* entry = &entries[slots[e]];
        std::memset(entry, 0, kEntrySize);
        entry[0] = static_cast<uint8_t>(user);
        std::memset(entry + 1, ' ', 11);
        std::memcpy(entry + 1, stem.data(), stem.size());
        std::memcpy(entry + 9, extension.data(), extension.size());
        // The records this entry holds, and the number of the last 16K
        // they reach into.
        const int here = std::min(entryRecords, records - static_cast<int>(e) * entryRecords);
        const int extent = static_cast<int>(e) * (format_->extentMask + 1) + (here > 0 ? (here - 1) / 128 : 0);
        entry[12] = static_cast<uint8_t>(extent & 0x1F);
        entry[14] = static_cast<uint8_t>(extent >> 5);
        entry[15] = static_cast<uint8_t>(here <= 0 ? 0 : here - ((here - 1) / 128) * 128);
        for (int i = 0; i < perEntry; ++i) {
            const size_t index = e * static_cast<size_t>(perEntry) + static_cast<size_t>(i);
            const int block = index < blocks.size() ? blocks[index] : 0;
            if (wideBlocks()) {
                entry[16 + i * 2] = static_cast<uint8_t>(block);
                entry[17 + i * 2] = static_cast<uint8_t>(block >> 8);
            } else {
                entry[16 + i] = static_cast<uint8_t>(block);
            }
        }
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
        // The last record is filled out with the end-of-file mark.
        std::vector<uint8_t> content(size, 0x1A);
        const size_t from = i * size;
        std::memcpy(content.data(), &data[from], std::min(size, data.size() - from));
        writeBlock(blocks[i], content);
    }
    setDirectory(entries);
    return true;
}

bool DiscFiles::remove(const DiscFile& file)
{
    if (!valid())
        return false;
    std::vector<uint8_t> entries = directory();
    bool found = false;
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize)
        if (entries[at] == file.user && entryName(&entries[at]) == file.name) {
            entries[at] = kUnused;
            found = true;
        }
    if (found)
        setDirectory(entries);
    return found;
}

bool DiscFiles::rename(const DiscFile& file, const std::string& name)
{
    const auto proper = directoryName(name);
    if (!valid() || !proper)
        return false;
    std::vector<uint8_t> entries = directory();
    // Not onto another file.
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize)
        if (entries[at] == file.user && entryName(&entries[at]) == *proper && *proper != file.name)
            return false;
    const size_t dot = proper->find('.');
    const std::string stem = proper->substr(0, dot), extension = dot == std::string::npos ? "" : proper->substr(dot + 1);
    bool found = false;
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize) {
        uint8_t* entry = &entries[at];
        if (entry[0] != file.user || entryName(entry) != file.name)
            continue;
        const uint8_t attributes[3] = {static_cast<uint8_t>(entry[9] & 0x80), static_cast<uint8_t>(entry[10] & 0x80),
                                       static_cast<uint8_t>(entry[11] & 0x80)};
        std::memset(entry + 1, ' ', 11);
        std::memcpy(entry + 1, stem.data(), stem.size());
        std::memcpy(entry + 9, extension.data(), extension.size());
        for (int i = 0; i < 3; ++i)
            entry[9 + i] |= attributes[i];
        found = true;
    }
    if (found)
        setDirectory(entries);
    return found;
}

bool DiscFiles::setAttributes(const DiscFile& file, bool readOnly, bool system)
{
    if (!valid())
        return false;
    std::vector<uint8_t> entries = directory();
    bool found = false;
    for (size_t at = 0; at + kEntrySize <= entries.size(); at += kEntrySize) {
        uint8_t* entry = &entries[at];
        if (entry[0] != file.user || entryName(entry) != file.name)
            continue;
        entry[9] = static_cast<uint8_t>((entry[9] & 0x7F) | (readOnly ? 0x80 : 0));
        entry[10] = static_cast<uint8_t>((entry[10] & 0x7F) | (system ? 0x80 : 0));
        found = true;
    }
    if (found)
        setDirectory(entries);
    return found;
}

// ---- AMSDOS headers -----------------------------------------------------------
//
//   0      user          1-11   name and extension, padded with spaces
//   18     type          21-22  where it loads     24-25  its length
//   26-27  its entry     64-66  its length again, on three bytes
//   67-68  the sum of bytes 0 to 66

std::optional<AmsdosHeader> amsdosHeader(std::span<const uint8_t> file)
{
    if (file.size() < 128)
        return std::nullopt;
    unsigned sum = 0;
    for (int i = 0; i < 67; ++i)
        sum += file[static_cast<size_t>(i)];
    // A file of zeroes adds up to zero too, and has no header.
    if (sum == 0 || sum != static_cast<unsigned>(file[67] | file[68] << 8))
        return std::nullopt;
    AmsdosHeader header;
    header.type = file[18];
    header.loadAddress = static_cast<uint16_t>(file[21] | file[22] << 8);
    header.entryAddress = static_cast<uint16_t>(file[26] | file[27] << 8);
    header.length = file[64] | file[65] << 8 | file[66] << 16;
    return header;
}

std::vector<uint8_t> makeAmsdosHeader(const std::string& name, const AmsdosHeader& header)
{
    std::vector<uint8_t> out(128, 0);
    const std::string proper = DiscFiles::directoryName(name).value_or("FILE");
    const size_t dot = proper.find('.');
    const std::string stem = proper.substr(0, dot), extension = dot == std::string::npos ? "" : proper.substr(dot + 1);
    std::memset(&out[1], ' ', 11);
    std::memcpy(&out[1], stem.data(), stem.size());
    std::memcpy(&out[9], extension.data(), extension.size());
    out[18] = header.type;
    out[21] = static_cast<uint8_t>(header.loadAddress);
    out[22] = static_cast<uint8_t>(header.loadAddress >> 8);
    out[24] = out[64] = static_cast<uint8_t>(header.length);
    out[25] = out[65] = static_cast<uint8_t>(header.length >> 8);
    out[66] = static_cast<uint8_t>(header.length >> 16);
    out[26] = static_cast<uint8_t>(header.entryAddress);
    out[27] = static_cast<uint8_t>(header.entryAddress >> 8);
    unsigned sum = 0;
    for (int i = 0; i < 67; ++i)
        sum += out[static_cast<size_t>(i)];
    out[67] = static_cast<uint8_t>(sum);
    out[68] = static_cast<uint8_t>(sum >> 8);
    return out;
}

}  // namespace tuxape
