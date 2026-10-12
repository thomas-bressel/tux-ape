// HFE disc images (HxC Floppy Emulator): a track written here as the bit
// cells a drive's head would read, sync marks, CRCs and all, and read back
// as sectors.
//
//   disc_hfe [folder]   with xmem/xmem_side-A.hfe and .dsk in the folder (a
//                       real image and the same disc made into a DSK by
//                       another program), the two are compared too.

#include <cstdio>
#include <filesystem>
#include <vector>

#include "check.h"
#include "core/disc.h"
#include "core/files.h"

namespace {

using namespace tuxape;

// MFM: each bit of data after a clock cell, which is set between two zeros.
// A sync byte is &A1 with one clock left out: &4489.
struct Track {
    std::vector<uint8_t> cells;  // one cell to a byte, for simplicity
    int last = 0;
    uint16_t crc = 0xFFFF;
    void bit(int value, bool clock)
    {
        cells.push_back(static_cast<uint8_t>(clock && !last && !value));
        cells.push_back(static_cast<uint8_t>(value));
        last = value;
    }
    void byte(uint8_t value)
    {
        for (int i = 7; i >= 0; --i)
            bit(value >> i & 1, true);
        crc ^= static_cast<uint16_t>(value << 8);
        for (int i = 0; i < 8; ++i)
            crc = static_cast<uint16_t>(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    void fill(uint8_t value, int count)
    {
        for (int i = 0; i < count; ++i)
            byte(value);
    }
    void sync()
    {
        fill(0x00, 12);
        crc = 0xFFFF;
        for (int n = 0; n < 3; ++n) {
            for (int i = 15; i >= 0; --i)
                cells.push_back(static_cast<uint8_t>(0x4489 >> i & 1));
            last = 1;
            crc ^= 0xA100;
            for (int i = 0; i < 8; ++i)
                crc = static_cast<uint16_t>(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    void sector(uint8_t c, uint8_t h, uint8_t r, uint8_t n, const std::vector<uint8_t>& data, bool deleted = false,
                bool badData = false)
    {
        sync();
        byte(0xFE);
        for (const uint8_t value : {c, h, r, n})
            byte(value);
        uint16_t check = crc;
        byte(static_cast<uint8_t>(check >> 8));
        byte(static_cast<uint8_t>(check));
        fill(0x4E, 22);
        sync();
        byte(deleted ? 0xF8 : 0xFB);
        for (const uint8_t value : data)
            byte(value);
        check = static_cast<uint16_t>(badData ? crc ^ 0x0101 : crc);
        byte(static_cast<uint8_t>(check >> 8));
        byte(static_cast<uint8_t>(check));
        fill(0x4E, 24);
    }
};

// The file: a header, the list of where each track is, and the tracks, the
// two sides in turn 256 bytes at a time, a byte's first cell in its lowest
// bit.
std::vector<uint8_t> hfe(const std::vector<std::vector<Track>>& tracks, int sides)
{
    std::vector<uint8_t> file(1024, 0xFF);
    const char magic[] = "HXCPICFE";
    std::copy(magic, magic + 8, file.begin());
    file[8] = 0;
    file[9] = static_cast<uint8_t>(tracks.size());
    file[10] = static_cast<uint8_t>(sides);
    file[11] = 0;
    file[12] = 250;
    file[13] = 0;
    file[18] = 1;
    file[19] = 0;
    for (size_t t = 0; t < tracks.size(); ++t) {
        std::vector<uint8_t> packed[2];
        for (int side = 0; side < 2; ++side) {
            const std::vector<uint8_t> none;
            const std::vector<uint8_t>& cells = side < static_cast<int>(tracks[t].size()) ? tracks[t][side].cells : none;
            for (size_t i = 0; i < cells.size(); i += 8) {
                uint8_t value = 0;
                for (size_t b = 0; b < 8 && i + b < cells.size(); ++b)
                    value = static_cast<uint8_t>(value | cells[i + b] << b);
                packed[side].push_back(value);
            }
        }
        const size_t each = std::max(packed[0].size(), packed[1].size());
        const size_t start = file.size();
        file[512 + t * 4] = static_cast<uint8_t>(start / 512);
        file[512 + t * 4 + 1] = static_cast<uint8_t>(start / 512 >> 8);
        file[512 + t * 4 + 2] = static_cast<uint8_t>(each * 2);
        file[512 + t * 4 + 3] = static_cast<uint8_t>(each * 2 >> 8);
        for (size_t at = 0; at < each; at += 256)
            for (int side = 0; side < 2; ++side)
                for (size_t i = at; i < at + 256; ++i)
                    file.push_back(i < packed[side].size() ? packed[side][i] : 0xAA);
        file.resize((file.size() + 511) / 512 * 512, 0xFF);
    }
    return file;
}

std::vector<uint8_t> bytes(int size, int seed)
{
    std::vector<uint8_t> data(static_cast<size_t>(size));
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(seed * 31 + i * 7 + (i >> 8));
    return data;
}

void testTracks()
{
    // Two sides of two tracks: nine sectors of 512 as AMSDOS has them, then
    // odd ones: a deleted mark, data that does not check, a sector of 256,
    // one whose data runs over the index hole.
    std::vector<std::vector<Track>> tracks(2, std::vector<Track>(2));
    for (int side = 0; side < 2; ++side) {
        Track& track = tracks[0][side];
        track.fill(0x4E, 80);
        for (int i = 0; i < 9; ++i)
            track.sector(0, static_cast<uint8_t>(side), static_cast<uint8_t>(0xC1 + (i * 5) % 9), 2,
                         bytes(512, side * 16 + i));
        track.fill(0x4E, 200);
    }
    Track& odd = tracks[1][0];
    odd.fill(0x4E, 40);
    odd.sector(1, 0, 0x01, 2, bytes(512, 40), true);
    odd.sector(1, 0, 0x02, 2, bytes(512, 41), false, true);
    odd.sector(1, 0, 0x03, 1, bytes(256, 42));
    odd.sector(77, 1, 0x04, 2, bytes(512, 43));
    // The last one started again from its middle: what the head reads from
    // the index is the end of a field whose start is at the end.
    const size_t cut = odd.cells.size() - 300 * 16;
    std::vector<uint8_t> turned(odd.cells.begin() + static_cast<ptrdiff_t>(cut), odd.cells.end());
    turned.insert(turned.end(), odd.cells.begin(), odd.cells.begin() + static_cast<ptrdiff_t>(cut));
    odd.cells = turned;

    const auto disc = Disc::fromHfe(hfe(tracks, 2));
    CHECK(disc.has_value());
    if (!disc)
        return;
    CHECK(disc->cylinders() == 2 && disc->sides() == 2 && !disc->modified);
    for (int side = 0; side < 2; ++side) {
        const DiscTrack* track = disc->track(0, side);
        CHECK(track && track->sectors.size() == 9);
        for (size_t i = 0; track && i < track->sectors.size(); ++i) {
            const DiscSector& sector = track->sectors[i];
            // In the order they pass under the head.
            CHECK_EQ(sector.r, 0xC1 + (static_cast<int>(i) * 5) % 9);
            CHECK(sector.c == 0 && sector.h == side && sector.n == 2 && sector.st1 == 0 && sector.st2 == 0);
            CHECK(sector.data == bytes(512, side * 16 + static_cast<int>(i)));
        }
    }
    const DiscTrack* track = disc->track(1, 0);
    CHECK(track && track->sectors.size() == 4);
    if (track && track->sectors.size() == 4) {
        const auto find = [&](int r) {
            for (const DiscSector& sector : track->sectors)
                if (sector.r == r)
                    return &sector;
            return static_cast<const DiscSector*>(nullptr);
        };
        const DiscSector* deleted = find(1);
        const DiscSector* bad = find(2);
        const DiscSector* small = find(3);
        const DiscSector* over = find(4);
        CHECK(deleted && bad && small && over);
        if (deleted && bad && small && over) {
            CHECK(deleted->deleted() && deleted->st1 == 0 && deleted->data == bytes(512, 40));
            CHECK(bad->st1 == 0x20 && bad->st2 == 0x20 && bad->data == bytes(512, 41));
            CHECK(small->n == 1 && small->data == bytes(256, 42) && small->st1 == 0);
            CHECK(over->c == 77 && over->h == 1 && over->data == bytes(512, 43) && over->st1 == 0);
        }
    }
    CHECK(disc->track(1, 1) && !disc->track(1, 1)->formatted());

    // Told from a DSK by how the file starts; anything else is neither.
    CHECK(Disc::fromImage(hfe(tracks, 2)).has_value());
    Disc blank;
    blank.format(defaultDiscFormat());
    const auto dsk = Disc::fromImage(blank.toDsk());
    CHECK(dsk && dsk->cylinders() == 40);
    CHECK(!Disc::fromHfe(blank.toDsk()));
    std::vector<uint8_t> other = hfe(tracks, 2);
    other[0] = 'X';
    CHECK(!Disc::fromImage(other));
    other = hfe(tracks, 2);
    other.resize(600);
    CHECK(!Disc::fromHfe(other));
    other = hfe(tracks, 2);
    other[11] = 2;  // FM: not a CPC's
    CHECK(!Disc::fromHfe(other));
}

// A real image, and the same disc as another program made a DSK of it.
void testReal(const std::filesystem::path& folder)
{
    const auto image = readFile(folder / "xmem" / "xmem_side-A.hfe");
    const auto same = readFile(folder / "xmem" / "xmem_side-A.dsk");
    const auto disc = image ? Disc::fromHfe(*image) : std::nullopt;
    const auto wanted = same ? Disc::fromDsk(*same) : std::nullopt;
    if (!disc || !wanted) {
        std::printf("no real HFE image to read: that part is left out\n");
        return;
    }
    CHECK_EQ(disc->cylinders(), wanted->cylinders());
    int sectors = 0, differing = 0;
    for (int cylinder = 0; cylinder < wanted->cylinders(); ++cylinder) {
        const DiscTrack* got = disc->track(cylinder, 0);
        const DiscTrack* want = wanted->track(cylinder, 0);
        if (!got || !want || got->sectors.size() != want->sectors.size()) {
            ++differing;
            continue;
        }
        for (const DiscSector& sector : want->sectors) {
            ++sectors;
            bool found = false;
            for (const DiscSector& other : got->sectors)
                found = found || (other.r == sector.r && other.c == sector.c && other.data == sector.data
                                  && other.st1 == sector.st1 && other.st2 == sector.st2);
            differing += !found;
        }
    }
    CHECK_EQ(sectors, 360);
    CHECK_EQ(differing, 0);
}

}  // namespace

int main(int argc, char* argv[])
{
    testTracks();
    if (argc > 1)
        testReal(argv[1]);
    return checkSummary("disc_hfe");
}
