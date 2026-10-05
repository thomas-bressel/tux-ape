#include "core/disc.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tuxape {

namespace {

constexpr DiscFormat kFormats[] = {
    // name                 spt  first il trk sd bsh  dsm  off dir exm gap3  fill
    {"PARADOS 80",          10, 0x91, 5, 80, 1, 4, 0x0C7, 0, 128, 1, 0x10, 0xE5},
    {"PARADOS 41",          10, 0x81, 5, 41, 1, 3, 0x0CC, 0, 64,  0, 0x10, 0xE5},
    {"PARADOS 40D",         10, 0xA1, 5, 40, 2, 4, 0x0C7, 0, 128, 1, 0x10, 0xE5},
    {"ROMDOS D1",           9,  0x01, 5, 80, 2, 4, 0x167, 0, 128, 0, 0x52, 0xE5},
    {"ROMDOS D2",           9,  0x21, 5, 80, 2, 4, 0x167, 0, 256, 0, 0x52, 0xE5},
    {"ROMDOS D10",          10, 0x11, 5, 80, 2, 4, 0x18F, 0, 128, 0, 0x10, 0xE5},
    {"ROMDOS D20",          10, 0x31, 5, 80, 2, 4, 0x18F, 0, 256, 0, 0x10, 0xE5},
    {"ROMDOS D40",          10, 0x51, 5, 40, 2, 4, 0x0C7, 0, 128, 0, 0x10, 0xE5},
    {"S-DOS (ROMDOS D80)",  10, 0x71, 5, 80, 1, 4, 0x0C7, 0, 128, 0, 0x10, 0xE5},
    {"DATA (SS 40)",        9,  0xC1, 5, 40, 1, 3, 0x0B3, 0, 64,  0, 0x52, 0xE5},
    {"DATA (DS 40)",        9,  0xC1, 5, 40, 2, 4, 0x0B3, 0, 128, 1, 0x52, 0xE5},
    {"DATA (SS 80)",        9,  0xC1, 5, 80, 1, 4, 0x0B3, 0, 128, 1, 0x52, 0xE5},
    {"DATA (DS 80)",        9,  0xC1, 5, 80, 2, 5, 0x0B3, 0, 128, 3, 0x52, 0xE5},
    {"SYSTEM (SS 40)",      9,  0x41, 5, 40, 1, 3, 0x0AA, 2, 64,  0, 0x52, 0xE5},
    {"SYSTEM (DS 40)",      9,  0x41, 5, 40, 2, 4, 0x0AE, 2, 128, 1, 0x52, 0xE5},
    {"SYSTEM (SS 80)",      9,  0x41, 5, 80, 1, 4, 0x0AE, 2, 128, 1, 0x52, 0xE5},
    {"SYSTEM (DS 80)",      9,  0x41, 5, 80, 2, 5, 0x0B0, 2, 128, 3, 0x52, 0xE5},
    {"IBM (SS 40)",         8,  0x01, 1, 40, 1, 3, 0x09B, 1, 64,  0, 0x50, 0xE5},
    {"IBM (DS 40)",         8,  0x01, 1, 40, 2, 4, 0x09D, 1, 128, 1, 0x50, 0xE5},
    {"IBM (SS 80)",         8,  0x01, 1, 80, 1, 4, 0x09D, 1, 128, 1, 0x50, 0xE5},
    {"IBM (DS 80)",         8,  0x01, 1, 80, 2, 5, 0x09E, 1, 128, 3, 0x50, 0xE5},
    {"PCW (SS 40)",         9,  0x01, 5, 40, 1, 3, 0x0AE, 1, 64,  0, 0x52, 0xE5},
    {"PCW (DS 80)",         9,  0x01, 5, 80, 2, 4, 0x164, 1, 256, 0, 0x52, 0xE5},
    {"ULTRAFORM",           10, 0x10, 5, 41, 1, 3, 0x0CC, 0, 64,  0, 0x10, 0xE5},
    {"VORTEX",              9,  0x01, 5, 80, 2, 5, 0x0B0, 2, 128, 3, 0x52, 0xE5},
};

constexpr int kDefaultFormat = 9;  // DATA (SS 40)

constexpr char kStandardMagic[] = "MV - CPC";
constexpr char kExtendedMagic[] = "EXTENDED";
constexpr size_t kHeaderSize = 0x100;
constexpr int kMaxSectorsPerTrack = 29;  // what fits in a track header

uint16_t le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

}  // namespace

std::span<const DiscFormat> discFormats()
{
    return kFormats;
}

const DiscFormat& defaultDiscFormat()
{
    return kFormats[kDefaultFormat];
}

int DiscSector::copies() const
{
    const int nominal = nominalSize();
    const int size = static_cast<int>(data.size());
    return size >= 2 * nominal && size % nominal == 0 ? size / nominal : 1;
}

void DiscTrack::updateLayout()
{
    // Field lengths of the MFM track format, in bytes.
    constexpr int kPreamble = 80 + 12 + 4 + 50;  // gap 4a, sync, index mark, gap 1
    constexpr int kIdLead = 12 + 4;              // sync, ID address mark
    constexpr int kIdToData = 2 + 22 + 12 + 4;   // CRC, gap 2, sync, data address mark

    idPosition.resize(sectors.size());
    dataPosition.resize(sectors.size());
    int pos = kPreamble;
    for (size_t i = 0; i < sectors.size(); ++i) {
        idPosition[i] = pos + kIdLead + 4;
        dataPosition[i] = idPosition[i] + kIdToData;
        pos = dataPosition[i] + sectors[i].copySize() + 2 + gap3;
    }
    // More than fits in one revolution: the real disc had shorter gaps or
    // overlapping sectors. Squeeze everything in, keeping the order.
    if (pos > kLength) {
        for (size_t i = 0; i < sectors.size(); ++i) {
            idPosition[i] = static_cast<int>(static_cast<int64_t>(idPosition[i]) * kLength / pos);
            dataPosition[i] = static_cast<int>(static_cast<int64_t>(dataPosition[i]) * kLength / pos);
        }
    }
}

DiscTrack* Disc::track(int cylinder, int side)
{
    if (cylinder < 0 || cylinder >= cylinders_ || side < 0 || side >= sides_)
        return nullptr;
    return &tracks_[static_cast<size_t>(cylinder * sides_ + side)];
}

const DiscTrack* Disc::track(int cylinder, int side) const
{
    return const_cast<Disc*>(this)->track(cylinder, side);
}

DiscTrack& Disc::ensureTrack(int cylinder, int side)
{
    cylinder = std::clamp(cylinder, 0, kMaxCylinders - 1);
    side = std::clamp(side, 0, 1);
    resize(std::max(cylinders_, cylinder + 1), std::max(sides_, side + 1));
    return *track(cylinder, side);
}

void Disc::resize(int cylinders, int sides)
{
    if (cylinders == cylinders_ && sides == sides_)
        return;
    std::vector<DiscTrack> tracks(static_cast<size_t>(cylinders * sides));
    for (int c = 0; c < std::min(cylinders, cylinders_); ++c)
        for (int s = 0; s < std::min(sides, sides_); ++s)
            tracks[static_cast<size_t>(c * sides + s)] = std::move(tracks_[static_cast<size_t>(c * sides_ + s)]);
    tracks_ = std::move(tracks);
    cylinders_ = cylinders;
    sides_ = sides;
}

void Disc::format(const DiscFormat& format, bool clearUnused)
{
    if (clearUnused) {
        tracks_.clear();
        cylinders_ = 0;
        sides_ = 1;
    }
    resize(std::max(cylinders_, static_cast<int>(format.tracks)), std::max(sides_, static_cast<int>(format.sides)));

    // The order in which sector IDs are laid around the track. With an
    // interleave of 5, nine sectors come out as 1,6,2,7,3,8,4,9,5.
    std::vector<uint8_t> order;
    int id = 0;
    for (int slot = 0; slot < format.sectorsPerTrack; ++slot) {
        order.push_back(static_cast<uint8_t>(format.firstSectorId + id));
        id += format.interleave;
        if (id >= format.sectorsPerTrack)
            id -= 2 * format.interleave - 1;
    }

    std::vector<uint8_t> ids;
    for (int c = 0; c < format.tracks; ++c) {
        for (int s = 0; s < format.sides; ++s) {
            ids.clear();
            for (uint8_t r : order)
                ids.insert(ids.end(), {static_cast<uint8_t>(c), static_cast<uint8_t>(s), r, 2});
            formatTrack(c, s, ids, format.gap3, format.filler);
        }
    }
    modified = true;
}

void Disc::formatTrack(int cylinder, int side, std::span<const uint8_t> ids, uint8_t gap3, uint8_t filler)
{
    DiscTrack& track = ensureTrack(cylinder, side);
    track.sectors.clear();
    track.gap3 = gap3;
    track.filler = filler;
    for (size_t i = 0; i + 4 <= ids.size() && track.sectors.size() < kMaxSectorsPerTrack; i += 4) {
        DiscSector sector;
        sector.c = ids[i];
        sector.h = ids[i + 1];
        sector.r = ids[i + 2];
        sector.n = ids[i + 3];
        sector.data.assign(static_cast<size_t>(sector.nominalSize()), filler);
        track.sectors.push_back(std::move(sector));
    }
    track.updateLayout();
    modified = true;
}

std::optional<Disc> Disc::fromDsk(std::span<const uint8_t> file)
{
    if (file.size() < kHeaderSize)
        return std::nullopt;
    const bool extended = std::memcmp(file.data(), kExtendedMagic, 8) == 0;
    if (!extended && std::memcmp(file.data(), kStandardMagic, 8) != 0)
        return std::nullopt;

    const int cylinders = file[0x30];
    const int sides = file[0x31];
    if (cylinders == 0 || cylinders > kMaxCylinders || sides < 1 || sides > 2)
        return std::nullopt;

    Disc disc;
    disc.resize(cylinders, sides);
    size_t offset = kHeaderSize;
    for (int index = 0; index < cylinders * sides; ++index) {
        // Standard images give every track the same size; extended ones
        // have a table, where 0 means the track is not formatted.
        const size_t blockSize = extended ? static_cast<size_t>(file[0x34 + index]) * 256 : le16(&file[0x32]);
        if (blockSize == 0)
            continue;
        if (offset + kHeaderSize > file.size())
            break;  // truncated image: keep what there is
        const uint8_t* header = &file[offset];
        if (std::memcmp(header, "Track-Info", 10) != 0)
            break;

        DiscTrack& track = disc.tracks_[static_cast<size_t>(index)];
        const int count = std::min<int>(header[0x15], kMaxSectorsPerTrack);
        track.gap3 = header[0x16];
        track.filler = header[0x17];
        size_t dataOffset = offset + kHeaderSize;
        for (int i = 0; i < count; ++i) {
            const uint8_t* info = &header[0x18 + i * 8];
            DiscSector sector;
            sector.c = info[0];
            sector.h = info[1];
            sector.r = info[2];
            sector.n = info[3];
            sector.st1 = info[4];
            sector.st2 = info[5];
            // Standard images store every sector at the track's sector size.
            size_t length = extended ? le16(&info[6]) : static_cast<size_t>(128) << std::min<int>(header[0x14], 8);
            if (dataOffset >= file.size())
                length = 0;
            else
                length = std::min(length, file.size() - dataOffset);
            sector.data.assign(file.begin() + static_cast<ptrdiff_t>(dataOffset),
                               file.begin() + static_cast<ptrdiff_t>(dataOffset + length));
            dataOffset += length;
            track.sectors.push_back(std::move(sector));
        }
        track.updateLayout();
        offset += blockSize;
    }
    return disc;
}

std::vector<uint8_t> Disc::toDsk() const
{
    std::vector<uint8_t> file(kHeaderSize, 0);
    std::memcpy(file.data(), "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
    std::memcpy(&file[0x22], "TuxAPE", 6);
    file[0x30] = static_cast<uint8_t>(cylinders_);
    file[0x31] = static_cast<uint8_t>(sides_);

    for (size_t index = 0; index < tracks_.size(); ++index) {
        const DiscTrack& track = tracks_[index];
        if (!track.formatted())
            continue;
        const size_t start = file.size();
        file.resize(start + kHeaderSize, 0);
        std::memcpy(&file[start], "Track-Info\r\n", 12);
        file[start + 0x10] = static_cast<uint8_t>(index / static_cast<size_t>(sides_));
        file[start + 0x11] = static_cast<uint8_t>(index % static_cast<size_t>(sides_));
        file[start + 0x14] = track.sectors[0].n;
        file[start + 0x15] = static_cast<uint8_t>(track.sectors.size());
        file[start + 0x16] = track.gap3;
        file[start + 0x17] = track.filler;
        for (size_t i = 0; i < track.sectors.size(); ++i) {
            const DiscSector& sector = track.sectors[i];
            const size_t length = std::min<size_t>(sector.data.size(), 0xFFFF);
            uint8_t* info = &file[start + 0x18 + i * 8];
            info[0] = sector.c;
            info[1] = sector.h;
            info[2] = sector.r;
            info[3] = sector.n;
            info[4] = sector.st1;
            info[5] = sector.st2;
            info[6] = static_cast<uint8_t>(length);
            info[7] = static_cast<uint8_t>(length >> 8);
            file.insert(file.end(), sector.data.begin(), sector.data.begin() + static_cast<ptrdiff_t>(length));
        }
        // Track blocks are a whole number of 256-byte pages.
        file.resize(start + (file.size() - start + 255) / 256 * 256, 0);
        file[0x34 + index] = static_cast<uint8_t>((file.size() - start) / 256);
    }
    return file;
}

std::string Disc::describe() const
{
    int formatted = 0;
    for (const DiscTrack& track : tracks_)
        formatted += track.formatted();
    if (formatted == 0)
        return "Unformatted disc";

    char text[160];
    const DiscTrack* first = track(0, 0);
    if (first && first->formatted()) {
        uint8_t low = 0xFF, high = 0x00;
        for (const DiscSector& sector : first->sectors) {
            low = std::min(low, sector.r);
            high = std::max(high, sector.r);
        }
        const char* name = nullptr;
        for (const DiscFormat& format : kFormats) {
            if (format.firstSectorId == low && format.sectorsPerTrack == first->sectors.size()
                && format.sides == sides_ && format.tracks >= cylinders_ - 3 && format.tracks <= cylinders_) {
                name = format.name;
                break;
            }
        }
        std::snprintf(text, sizeof text, "%d tracks, %s, %zu sectors per track (#%02X-#%02X)%s%s", cylinders_,
                      sides_ == 2 ? "double sided" : "single sided", first->sectors.size(), low, high,
                      name ? "\nFormat: " : "", name ? name : "");
    } else {
        std::snprintf(text, sizeof text, "%d tracks, %s", cylinders_, sides_ == 2 ? "double sided" : "single sided");
    }
    return text;
}

}  // namespace tuxape
