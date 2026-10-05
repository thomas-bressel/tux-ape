// Drives the disc controller directly, command byte by command byte, with a
// hand-moved clock. Covers what a program sees on the FDC's two ports:
// results, status flags, and timing against the turning disc.

#include <initializer_list>
#include <memory>
#include <vector>

#include "check.h"
#include "core/fdc.h"

namespace {

using namespace tuxape;

constexpr uint64_t kMillisecond = 1000;

struct Rig {
    Fdc fdc;
    uint64_t now = 0;

    explicit Rig(bool fast = false)
    {
        fdc.setFast(fast);
        auto disc = std::make_unique<Disc>();
        disc->format(defaultDiscFormat());
        fdc.drive(0).disc = std::move(disc);
        fdc.writeMotor(1, now);
        command({0x03, 0xA1, 0x03});  // specify, with AMSDOS's step rate
    }

    Disc& disc() { return *fdc.drive(0).disc; }

    void command(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes) {
            fdc.writeData(b, now);
            now += 20;
        }
    }

    // Polls the status port as a loader would. Returns the bytes handed
    // over during execution; `out` supplies bytes for write commands. Stops
    // when the result phase starts, or after `limit` of waiting.
    std::vector<uint8_t> execute(const std::vector<uint8_t>& out = {}, uint64_t limit = 2000 * kMillisecond)
    {
        std::vector<uint8_t> in;
        size_t sent = 0;
        const uint64_t deadline = now + limit;
        while (now < deadline) {
            const uint8_t status = fdc.readStatus(now);
            if ((status & 0xF0) == 0xD0)
                break;  // result phase
            if ((status & 0xA0) == 0xA0) {
                now += 8;
                if (status & 0x40)
                    in.push_back(fdc.readData(now));
                else
                    fdc.writeData(sent < out.size() ? out[sent++] : 0, now);
            }
            now += 6;
        }
        return in;
    }

    std::vector<uint8_t> result()
    {
        std::vector<uint8_t> bytes;
        while ((fdc.readStatus(now) & 0xF0) == 0xD0 && bytes.size() < 8) {
            bytes.push_back(fdc.readData(now));
            now += 20;
        }
        return bytes;
    }

    std::vector<uint8_t> readId()
    {
        command({0x4A, 0x00});
        execute();
        return result();
    }

    // Read Data for sectors `first` to `last` of the current track.
    std::vector<uint8_t> read(uint8_t track, uint8_t first, uint8_t last, uint8_t code = 0x46)
    {
        command({code, 0x00, track, 0x00, first, 0x02, last, 0x2A, 0xFF});
        return execute();
    }

    void seek(uint8_t track)
    {
        command({0x0F, 0x00, track});
        while (fdc.readStatus(now) & 0x0F)
            now += 100;
        command({0x08});
        result();
    }
};

void testNotReady()
{
    Rig rig;
    rig.fdc.writeMotor(0, rig.now);
    const auto id = rig.readId();
    CHECK_EQ(id.size(), 7);
    CHECK_EQ(id[0], 0x48);  // abnormal termination, not ready
}

void testSeekAndSenseInterrupt()
{
    Rig rig;
    // Nothing pending: Sense Interrupt Status is an invalid command.
    rig.command({0x08});
    CHECK(rig.result() == std::vector<uint8_t>{0x80});

    const uint64_t start = rig.now;
    rig.command({0x0F, 0x00, 5});
    CHECK_EQ(rig.fdc.readStatus(rig.now) & 0x0F, 0x01);  // drive 0 busy seeking
    while (rig.fdc.readStatus(rig.now) & 0x0F)
        rig.now += 100;
    // Five steps at 12 ms each.
    CHECK(rig.now - start >= 60 * kMillisecond);
    CHECK(rig.now - start < 62 * kMillisecond);
    rig.command({0x08});
    CHECK(rig.result() == (std::vector<uint8_t>{0x20, 5}));
    CHECK_EQ(rig.fdc.drive(0).cylinder, 5);

    rig.command({0x04, 0x00});  // sense drive status
    CHECK_EQ(rig.result()[0], 0x20);  // ready, not on track 0, not protected

    rig.command({0x07, 0x00});  // recalibrate
    while (rig.fdc.readStatus(rig.now) & 0x0F)
        rig.now += 100;
    rig.command({0x08});
    CHECK(rig.result() == (std::vector<uint8_t>{0x20, 0}));
    rig.command({0x04, 0x00});
    CHECK_EQ(rig.result()[0], 0x30);  // ready, track 0
}

void testReadIdFollowsRotation()
{
    Rig rig;
    // AMSDOS lays its nine sectors down interleaved.
    const uint8_t order[] = {0xC1, 0xC6, 0xC2, 0xC7, 0xC3, 0xC8, 0xC4, 0xC9, 0xC5};
    auto id = rig.readId();
    CHECK_EQ(id[0], 0x00);
    int at = 0;
    while (at < 9 && order[at] != id[5])
        ++at;
    CHECK(at < 9);
    uint64_t previous = rig.now;
    for (int i = 1; i <= 9; ++i) {
        id = rig.readId();
        CHECK_EQ(id[5], order[(at + i) % 9]);
        CHECK_EQ(id[6], 2);
        // A sector with its gaps is 656 bytes long, about 21 ms. Between
        // the last sector and the first lie the end of the track and the
        // index preamble as well: 32 ms.
        const uint64_t gap = rig.now - previous;
        const bool acrossIndex = id[5] == order[0];
        CHECK(gap > 20 * kMillisecond && gap < (acrossIndex ? 33 : 22) * kMillisecond);
        previous = rig.now;
    }
}

void testReadData()
{
    Rig rig;
    rig.disc().track(0, 0)->sectors[0].data[3] = 0x42;  // sector C1

    const uint64_t start = rig.now;
    auto data = rig.read(0, 0xC1, 0xC1);
    CHECK_EQ(data.size(), 512);
    CHECK_EQ(data[0], 0xE5);
    CHECK_EQ(data[3], 0x42);
    // Finding the sector takes a fraction of a turn, reading it 16.4 ms.
    CHECK(rig.now - start > 16 * kMillisecond);
    CHECK(rig.now - start < 220 * kMillisecond);
    // No terminal count on the CPC: every read ends with "end of cylinder".
    CHECK(rig.result() == (std::vector<uint8_t>{0x40, 0x80, 0x00, 0, 0, 0xC1, 2}));

    // Several sectors in one command.
    data = rig.read(0, 0xC1, 0xC3);
    CHECK_EQ(data.size(), 3 * 512);
    CHECK_EQ(rig.result()[5], 0xC3);
}

void testSectorNotFound()
{
    Rig rig;
    const uint64_t start = rig.now;
    const auto data = rig.read(0, 0x99, 0x99);
    CHECK(data.empty());
    const auto result = rig.result();
    CHECK_EQ(result[0], 0x40);
    CHECK_EQ(result[1], 0x04);  // no data
    // The controller watches the index hole go by twice before giving up.
    CHECK(rig.now - start > 200 * kMillisecond);
    CHECK(rig.now - start < 401 * kMillisecond);

    // A track that was never formatted has no address marks at all.
    rig.seek(41);
    rig.read(41, 0xC1, 0xC1);
    CHECK_EQ(rig.result()[1], 0x01);
}

void testOverrun()
{
    Rig rig;
    rig.command({0x46, 0x00, 0, 0x00, 0xC1, 0x02, 0xC1, 0x2A, 0xFF});
    while ((rig.fdc.readStatus(rig.now) & 0xF0) != 0xF0)
        rig.now += 4;
    rig.fdc.readData(rig.now);
    // Dawdle: the next byte arrives after 32 microseconds and is overwritten
    // by the one after.
    rig.now += 100;
    const auto result = rig.result();
    CHECK_EQ(result.size(), 7);
    CHECK_EQ(result[0], 0x40);
    CHECK_EQ(result[1], 0x10);  // overrun
}

void testWrite()
{
    Rig rig;
    std::vector<uint8_t> payload(512);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<uint8_t>(i * 7);

    rig.command({0x45, 0x00, 0, 0x00, 0xC4, 0x02, 0xC4, 0x2A, 0xFF});
    rig.execute(payload);
    CHECK(rig.result() == (std::vector<uint8_t>{0x40, 0x80, 0x00, 0, 0, 0xC4, 2}));
    CHECK(rig.disc().modified);
    CHECK(rig.read(0, 0xC4, 0xC4) == payload);
    rig.result();

    // Write Deleted Data marks the sector; a plain read then reports the
    // control mark, and skips the sector when asked to.
    rig.command({0x49, 0x00, 0, 0x00, 0xC4, 0x02, 0xC4, 0x2A, 0xFF});
    rig.execute(payload);
    rig.result();
    CHECK_EQ(rig.read(0, 0xC4, 0xC4).size(), 512);
    CHECK_EQ(rig.result()[2], 0x40);
    CHECK_EQ(rig.read(0, 0xC4, 0xC4, 0x66).size(), 0);  // SK set
    rig.result();
    CHECK_EQ(rig.read(0, 0xC4, 0xC4, 0x4C).size(), 512);  // Read Deleted Data
    CHECK_EQ(rig.result()[2], 0x00);

    rig.disc().writeProtected = true;
    rig.command({0x45, 0x00, 0, 0x00, 0xC4, 0x02, 0xC4, 0x2A, 0xFF});
    CHECK(rig.execute(payload).empty());
    const auto result = rig.result();
    CHECK_EQ(result[0], 0x40);
    CHECK_EQ(result[1], 0x02);  // not writable
    rig.command({0x04, 0x00});
    CHECK_EQ(rig.result()[0] & 0x40, 0x40);
}

void testFormat()
{
    Rig rig;
    rig.seek(3);
    // Three sectors of 256 bytes filled with AA.
    rig.command({0x4D, 0x00, 0x01, 3, 0x20, 0xAA});
    rig.execute({3, 0, 1, 1, 3, 0, 2, 1, 3, 0, 3, 1});
    CHECK_EQ(rig.result()[0], 0x00);

    const DiscTrack* track = rig.disc().track(3, 0);
    CHECK_EQ(track->sectors.size(), 3);
    CHECK_EQ(track->sectors[2].r, 3);
    CHECK_EQ(track->sectors[2].data.size(), 256);
    CHECK_EQ(track->sectors[2].data[100], 0xAA);
    CHECK_EQ(track->gap3, 0x20);

    rig.command({0x46, 0x00, 3, 0x00, 2, 0x01, 2, 0x2A, 0xFF});
    const auto data = rig.execute();
    CHECK_EQ(data.size(), 256);
    CHECK_EQ(data[0], 0xAA);
    rig.result();
}

void testWeakSector()
{
    Rig rig;
    // Two recorded copies of sector C1: successive reads alternate.
    DiscSector& sector = rig.disc().track(0, 0)->sectors[0];
    sector.data.assign(1024, 0x11);
    std::fill(sector.data.begin() + 512, sector.data.end(), 0x22);
    CHECK_EQ(sector.copies(), 2);

    const uint8_t a = rig.read(0, 0xC1, 0xC1)[0];
    rig.result();
    const uint8_t b = rig.read(0, 0xC1, 0xC1)[0];
    rig.result();
    CHECK(a != b);

    // A sector recorded with a CRC error is read in full, then reported.
    sector.st1 = 0x20;
    sector.st2 = 0x20;
    CHECK_EQ(rig.read(0, 0xC1, 0xC1).size(), 512);
    const auto result = rig.result();
    CHECK_EQ(result[1], 0x20);
    CHECK_EQ(result[2], 0x20);
}

void testFastMode()
{
    Rig rig(true);
    const uint64_t start = rig.now;
    CHECK_EQ(rig.read(0, 0xC1, 0xC9).size(), 9 * 512);
    // Nothing to wait for but the polling loop itself.
    CHECK(rig.now - start < 100 * kMillisecond);
    rig.result();

    // Read ID still walks round the track.
    const uint8_t first = rig.readId()[5];
    CHECK(rig.readId()[5] != first);

    // Dawdling loses nothing.
    rig.command({0x46, 0x00, 0, 0x00, 0xC1, 0x02, 0xC1, 0x2A, 0xFF});
    rig.fdc.readData(rig.now);
    rig.now += 10 * kMillisecond;
    CHECK_EQ(rig.fdc.readStatus(rig.now) & 0xF0, 0xF0);
}

void testDskStandardImage()
{
    // A hand-made standard (non-extended) image: one track, two sectors.
    std::vector<uint8_t> file(0x100 + 0x100 + 2 * 512, 0);
    const char magic[] = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
    std::copy(magic, magic + sizeof magic - 1, file.begin());
    file[0x30] = 1;     // tracks
    file[0x31] = 1;     // sides
    file[0x32] = 0x00;  // track size 0x500
    file[0x33] = 0x05;
    uint8_t* track = &file[0x100];
    const char trackMagic[] = "Track-Info\r\n";
    std::copy(trackMagic, trackMagic + sizeof trackMagic - 1, track);
    track[0x14] = 2;
    track[0x15] = 2;
    track[0x16] = 0x4E;
    track[0x17] = 0xE5;
    const uint8_t ids[2][4] = {{0, 0, 0x41, 2}, {0, 0, 0x42, 2}};
    for (int i = 0; i < 2; ++i)
        std::copy(ids[i], ids[i] + 4, &track[0x18 + i * 8]);
    file[0x200] = 0x12;
    file[0x400] = 0x34;

    const auto disc = Disc::fromDsk(file);
    CHECK(disc.has_value());
    if (disc) {
        CHECK_EQ(disc->cylinders(), 1);
        const DiscTrack* t = disc->track(0, 0);
        CHECK_EQ(t->sectors.size(), 2);
        CHECK_EQ(t->sectors[0].r, 0x41);
        CHECK_EQ(t->sectors[0].data[0], 0x12);
        CHECK_EQ(t->sectors[1].data[0], 0x34);
        CHECK_EQ(t->sectors[1].data.size(), 512);
    }
    CHECK(!Disc::fromDsk(std::vector<uint8_t>(0x200, 0)).has_value());
}

void testFormatTable()
{
    CHECK_EQ(discFormats().size(), 25);
    CHECK_EQ(defaultDiscFormat().firstSectorId, 0xC1);
    // Every format lays down each sector ID exactly once per track.
    for (const DiscFormat& format : discFormats()) {
        Disc disc;
        disc.format(format);
        CHECK_EQ(disc.cylinders(), format.tracks);
        CHECK_EQ(disc.sides(), format.sides);
        const DiscTrack* track = disc.track(format.tracks - 1, format.sides - 1);
        CHECK_EQ(track->sectors.size(), format.sectorsPerTrack);
        unsigned seen = 0;
        for (const DiscSector& sector : track->sectors)
            seen |= 1u << (sector.r - format.firstSectorId);
        CHECK_EQ(seen, (1u << format.sectorsPerTrack) - 1);
    }
}

}  // namespace

int main()
{
    testNotReady();
    testSeekAndSenseInterrupt();
    testReadIdFollowsRotation();
    testReadData();
    testSectorNotFound();
    testOverrun();
    testWrite();
    testFormat();
    testWeakSector();
    testFastMode();
    testDskStandardImage();
    testFormatTable();
    return checkSummary("fdc_unit");
}
