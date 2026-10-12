// The X-MASS card's IDE disc: what it says of itself, sectors read and
// written by number and by cylinder, head and sector, the file behind
// them, and the machine's addresses for it.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "core/cpc.h"
#include "core/files.h"
#include "core/ide.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

constexpr uint8_t kIdle = IdeDisc::kReady | IdeDisc::kSeekDone;
constexpr uint8_t kData = kIdle | IdeDisc::kDataRequest;

std::vector<uint8_t> pattern(int seed)
{
    std::vector<uint8_t> data(IdeDisc::kSectorSize);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(seed * 13 + i * 3 + (i >> 7));
    return data;
}

// The registers set for a sector by its number, and a command given.
void byNumber(IdeDisc& disc, uint32_t sector, int count, uint8_t command)
{
    disc.write(6, static_cast<uint8_t>(0xE0 | (sector >> 24 & 0x0F)));
    disc.write(2, static_cast<uint8_t>(count));
    disc.write(3, static_cast<uint8_t>(sector));
    disc.write(4, static_cast<uint8_t>(sector >> 8));
    disc.write(5, static_cast<uint8_t>(sector >> 16));
    disc.write(7, command);
}

std::vector<uint8_t> take(IdeDisc& disc)
{
    std::vector<uint8_t> data;
    for (int i = 0; i < IdeDisc::kSectorSize; ++i)
        data.push_back(disc.read(0));
    return data;
}

void give(IdeDisc& disc, const std::vector<uint8_t>& data)
{
    for (const uint8_t byte : data)
        disc.write(0, byte);
}

void testDisc(const std::filesystem::path& dir)
{
    const std::string file = (dir / "cards" / "xmass.img").string();
    IdeDisc disc(file, 8 * 1024 * 1024);
    CHECK(disc.good() && !std::filesystem::exists(file));
    CHECK_EQ(disc.sectors(), 16384u);
    CHECK_EQ(disc.cylinders(), 16);
    // After its own check: ready, no error, and the signature of a disc.
    CHECK(disc.read(7) == kIdle && disc.read(1) == 1 && disc.read(2) == 1 && disc.read(3) == 1);
    CHECK(disc.read(4) == 0 && disc.read(5) == 0);
    // Nobody on the cable's other plug.
    disc.write(6, 0xB0);
    CHECK(disc.read(7) == 0 && disc.readStatus() == 0);
    disc.write(7, 0xEC);
    CHECK_EQ(disc.read(7), 0);
    disc.write(6, 0xA0);

    // Who it is: 512 bytes, the text two characters to a word and the first
    // in the high byte, which read a byte at a time comes out swapped.
    disc.write(7, 0xEC);
    CHECK_EQ(disc.read(7), kData);
    const std::vector<uint8_t> id = take(disc);
    CHECK_EQ(disc.read(7), kIdle);
    const auto word = [&](int n) { return id[static_cast<size_t>(n) * 2] | id[static_cast<size_t>(n) * 2 + 1] << 8; };
    CHECK(word(1) == 16 && word(3) == 16 && word(6) == 63);
    CHECK(word(49) & 0x0200);
    CHECK_EQ(word(60) | word(61) << 16, 16384);
    std::string model;
    for (int i = 0; i < 40; i += 2) {
        model += static_cast<char>(id[54 + static_cast<size_t>(i) + 1]);
        model += static_cast<char>(id[54 + static_cast<size_t>(i)]);
    }
    CHECK(model.rfind("X-MASS DOM", 0) == 0);
    CHECK_EQ(disc.read(0), 0);  // nothing more to take

    // Nothing written: zeros, and still no file.
    byNumber(disc, 100, 1, 0x20);
    CHECK_EQ(disc.read(7), kData);
    CHECK(take(disc) == std::vector<uint8_t>(512, 0));
    CHECK(disc.read(7) == kIdle && !std::filesystem::exists(file));

    // Three sectors written in one command, the registers moving on; the
    // file is made, as long as what is in it.
    byNumber(disc, 100, 3, 0x30);
    for (int n = 0; n < 3; ++n) {
        CHECK_EQ(disc.read(7), kData);
        CHECK_EQ(disc.read(3), 100 + n);
        give(disc, pattern(n));
    }
    CHECK(disc.read(7) == kIdle && disc.read(2) == 0);
    CHECK(std::filesystem::exists(file));
    CHECK_EQ(std::filesystem::file_size(file), 103u * 512);
    byNumber(disc, 101, 2, 0x20);
    CHECK(take(disc) == pattern(1));
    CHECK_EQ(disc.read(7), kData);
    CHECK(take(disc) == pattern(2));
    CHECK_EQ(disc.read(7), kIdle);
    // The same sector by cylinder, head and sector: 101 = 1 * 63 + 38.
    disc.write(6, 0xA1);
    disc.write(2, 1);
    disc.write(3, 39);
    disc.write(4, 0);
    disc.write(5, 0);
    disc.write(7, 0x20);
    CHECK(take(disc) == pattern(1));
    // Told another shape, it counts by it: 4 heads of 20 sectors.
    disc.write(6, 0xA3);
    disc.write(2, 20);
    disc.write(7, 0x91);
    CHECK_EQ(disc.read(7), kIdle);
    disc.write(6, 0xA1);  // cylinder 1, head 1, sector 2: (1 * 4 + 1) * 20 + 1 = 101
    disc.write(2, 1);
    disc.write(3, 2);
    disc.write(4, 1);
    disc.write(5, 0);
    disc.write(7, 0x20);
    CHECK(take(disc) == pattern(1));
    disc.write(3, 21);  // no such sector on a track of 20
    disc.write(7, 0x20);
    CHECK(disc.read(7) == (kIdle | IdeDisc::kError) && disc.read(1) == IdeDisc::kNotFound);

    // Past the disc's end, and a command it does not know.
    byNumber(disc, 16384, 1, 0x20);
    CHECK(disc.read(7) == (kIdle | IdeDisc::kError) && disc.read(1) == IdeDisc::kNotFound);
    byNumber(disc, 16383, 2, 0x30);
    give(disc, pattern(9));
    CHECK(disc.read(7) == (kIdle | IdeDisc::kError) && disc.read(1) == IdeDisc::kNotFound);
    disc.write(7, 0xA1);
    CHECK(disc.read(7) == (kIdle | IdeDisc::kError) && disc.read(1) == IdeDisc::kAborted);
    // Those that only ask it to be ready are done at once.
    for (const uint8_t command : {0x10, 0x70, 0xEF, 0xE7, 0xE0, 0x40, 0x90}) {
        byNumber(disc, 5, 1, command);
        CHECK_EQ(disc.read(7), kIdle);
    }
    // A reset in the middle of a transfer.
    byNumber(disc, 100, 1, 0x20);
    disc.read(0);
    disc.writeControl(0x04);
    disc.writeControl(0x00);
    CHECK(disc.read(7) == kIdle && disc.read(1) == 1 && disc.read(0) == 0);

    // Another disc on the same file finds what was written; a file longer
    // than the size asked for is a bigger disc (its last sector was
    // written above: the file is all 8 MB by now).
    IdeDisc again(file, 1024);
    CHECK_EQ(again.sectors(), 16384u);
    byNumber(again, 102, 1, 0x20);
    CHECK(take(again) == pattern(2));
}

// In the machine: the SYMBiFACE II's addresses, with the card and without.
void testMachine(const std::filesystem::path& dir)
{
    const std::string file = (dir / "machine.img").string();
    Cpc cpc;
    MachineConfig config;
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK(cpc.ide() == nullptr);
    CHECK_EQ(cpc.in(0xFD0F), 0xFF);

    config.cards[1] = Card::Xmass;
    config.xmassDisc = file;
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK(cpc.ide() != nullptr && cpc.ide()->sectors() == IdeDisc::kDefaultBytes / 512);
    CHECK_EQ(cpc.in(0xFD0F), kIdle);
    CHECK_EQ(cpc.in(0xFD06), kIdle);
    cpc.out(0xFD0E, 0xE0);
    cpc.out(0xFD0A, 1);
    cpc.out(0xFD0B, 7);
    cpc.out(0xFD0C, 0);
    cpc.out(0xFD0D, 0);
    cpc.out(0xFD0F, 0x30);
    CHECK_EQ(cpc.in(0xFD0F), kData);
    for (const uint8_t byte : pattern(4))
        cpc.out(0xFD08, byte);
    CHECK_EQ(cpc.in(0xFD0F), kIdle);
    const auto written = readFile(file);
    CHECK(written && written->size() == 8 * 512 && (*written)[7 * 512 + 1] == pattern(4)[1]);
    // The machine's reset is the disc's; the card stays through another
    // fitting of the same machine, and goes when its place is emptied.
    cpc.out(0xFD0B, 9);
    cpc.reset();
    CHECK_EQ(cpc.in(0xFD0B), 1);
    IdeDisc* before = cpc.ide();
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK(cpc.ide() == before);
    // A card without a disc is as good as out.
    config.xmassDisc.clear();
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK(cpc.ide() == nullptr);
    config.xmassDisc = file;
    config.cards[1] = Card::None;
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK(cpc.ide() == nullptr && cpc.in(0xFD0F) == 0xFF);
}

}  // namespace

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "tuxape_ide_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    testDisc(dir);
    testMachine(dir);
    std::filesystem::remove_all(dir);
    return checkSummary("ide");
}
