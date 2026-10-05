// The files of a disc, read and written without the machine: directory,
// sizes, files of several extents, the larger formats, AMSDOS headers.
// (tests/disc_amsdos.cpp checks the same against the real AMSDOS ROM.)

#include <algorithm>
#include <string>
#include <vector>

#include "check.h"
#include "core/disc.h"
#include "core/discfiles.h"

namespace {

using namespace tuxape;
using Bytes = std::vector<uint8_t>;

const DiscFormat& formatNamed(const char* name)
{
    for (const DiscFormat& format : discFormats())
        if (std::string(format.name) == name)
            return format;
    std::printf("no format %s\n", name);
    ++g_failures;
    return defaultDiscFormat();
}

Bytes pattern(size_t size, unsigned seed)
{
    Bytes data(size);
    for (size_t i = 0; i < size; ++i)
        data[i] = static_cast<uint8_t>(i * 7 + (i >> 8) * 13 + seed);
    return data;
}

const DiscFile* find(const std::vector<DiscFile>& files, const char* name)
{
    for (const DiscFile& file : files)
        if (file.name == name)
            return &file;
    return nullptr;
}

// What was written comes back, with the last record filled out.
bool holds(const DiscFiles& files, const char* name, const Bytes& data)
{
    const std::vector<DiscFile> list = files.list();
    const DiscFile* file = find(list, name);
    if (!file || file->size != static_cast<int>((data.size() + 127) / 128 * 128))
        return false;
    const auto read = files.read(*file);
    return read && read->size() == static_cast<size_t>(file->size) && std::equal(data.begin(), data.end(), read->begin());
}

void testDataDisc()
{
    Disc disc;
    CHECK(!DiscFiles(disc).valid());  // not formatted
    disc.format(defaultDiscFormat());
    disc.modified = false;
    DiscFiles files(disc);
    CHECK(files.valid());
    CHECK(files.valid() && std::string(files.format()->name) == "DATA (SS 40)");
    CHECK(files.list().empty());
    CHECK_EQ(files.freeBytes(), 178 * 1024);

    const Bytes hello = {'H', 'e', 'l', 'l', 'o', 13, 10};
    CHECK(files.write("hello.txt", hello));
    CHECK(disc.modified);
    CHECK_EQ(files.list().size(), 1);
    CHECK(files.list()[0] == (DiscFile{"HELLO.TXT", 0, 128, false, false}));
    CHECK(holds(files, "HELLO.TXT", hello));
    CHECK_EQ(files.read(files.list()[0])->at(7), 0x1A);  // the end-of-file mark after it
    CHECK_EQ(files.freeBytes(), 177 * 1024);

    // Files of several directory entries, and those that end exactly on
    // an entry's last record.
    const Bytes big = pattern(40000, 1), exact = pattern(16384, 2), over = pattern(16385, 3);
    CHECK(files.write("BIG.BIN", big));
    CHECK(files.write("EXACT", exact));
    CHECK(files.write("OVER.B", over));
    CHECK(files.write("EMPTY.$$$", {}));
    CHECK(holds(files, "BIG.BIN", big));
    CHECK(holds(files, "EXACT", exact));
    CHECK(holds(files, "OVER.B", over));
    CHECK(holds(files, "EMPTY.$$$", {}));
    CHECK(holds(files, "HELLO.TXT", hello));
    CHECK_EQ(files.list().size(), 5);
    CHECK_EQ(files.freeBytes(), (177 - 40 - 16 - 17) * 1024);

    // A file written again takes the place of the one there.
    const Bytes small = pattern(300, 4);
    CHECK(files.write("big.bin", small));
    CHECK(holds(files, "BIG.BIN", small));
    CHECK_EQ(files.list().size(), 5);
    CHECK_EQ(files.freeBytes(), (177 - 1 - 16 - 17) * 1024);

    // Names, attributes, deletion.
    CHECK(files.rename(*find(files.list(), "EXACT"), "new.dat"));
    CHECK(holds(files, "NEW.DAT", exact));
    CHECK(find(files.list(), "EXACT") == nullptr);
    CHECK(!files.rename(*find(files.list(), "NEW.DAT"), "HELLO.TXT"));  // taken
    CHECK(!files.rename(*find(files.list(), "NEW.DAT"), "TOOLONGNAME.X"));
    CHECK(files.setAttributes(*find(files.list(), "NEW.DAT"), true, true));
    CHECK(find(files.list(), "NEW.DAT")->readOnly && find(files.list(), "NEW.DAT")->system);
    CHECK(files.rename(*find(files.list(), "NEW.DAT"), "KEPT.DAT"));
    CHECK(find(files.list(), "KEPT.DAT")->readOnly && find(files.list(), "KEPT.DAT")->system);
    CHECK(holds(files, "KEPT.DAT", exact));
    CHECK(files.setAttributes(*find(files.list(), "KEPT.DAT"), false, false));
    CHECK(!find(files.list(), "KEPT.DAT")->readOnly);
    CHECK(files.remove(*find(files.list(), "OVER.B")));
    CHECK(find(files.list(), "OVER.B") == nullptr);
    CHECK(!files.remove(DiscFile{"OVER.B", 0, 0, false, false}));
    CHECK(!files.read(DiscFile{"NOSUCH", 0, 0, false, false}));
    CHECK_EQ(files.freeBytes(), (177 - 1 - 16) * 1024);
    // The same name under another user is another file.
    CHECK(files.write("HELLO.TXT", small, 3));
    CHECK_EQ(files.list().size(), 5);
    CHECK(holds(files, "HELLO.TXT", hello));

    // No room: the disc stays as it was.
    const int free = files.freeBytes();
    CHECK(!files.write("HUGE", Bytes(static_cast<size_t>(free) + 1, 0)));
    CHECK_EQ(files.freeBytes(), free);
    CHECK(find(files.list(), "HUGE") == nullptr);
    CHECK(files.write("FITS", Bytes(static_cast<size_t>(free), 0x55)));
    CHECK_EQ(files.freeBytes(), 0);
    CHECK(holds(files, "KEPT.DAT", exact));

    // Seen afresh from the disc image: nothing is kept anywhere else.
    const auto again = Disc::fromDsk(disc.toDsk());
    CHECK(again.has_value());
    if (again) {
        Disc copy = *again;
        DiscFiles reread(copy);
        CHECK(reread.valid());
        CHECK(holds(reread, "KEPT.DAT", exact));
        CHECK(reread.list() == files.list());
    }
}

// The directory holds 64 files and no more.
void testFullDirectory()
{
    Disc disc;
    disc.format(defaultDiscFormat());
    DiscFiles files(disc);
    for (int i = 0; i < 64; ++i)
        CHECK(files.write("F" + std::to_string(i), Bytes{static_cast<uint8_t>(i)}));
    CHECK(!files.write("ONEMORE", Bytes{1}));
    CHECK_EQ(files.list().size(), 64);
    CHECK(holds(files, "F63", Bytes{63}));
}

void testOtherFormats()
{
    // SYSTEM: two tracks are kept for CP/M, and stay as they were.
    {
        Disc disc;
        disc.format(formatNamed("SYSTEM (SS 40)"));
        DiscFiles files(disc);
        CHECK(files.valid() && std::string(files.format()->name) == "SYSTEM (SS 40)");
        CHECK_EQ(files.freeBytes(), 169 * 1024);
        const Bytes data = pattern(20000, 5);
        CHECK(files.write("PROG.COM", data));
        CHECK(holds(files, "PROG.COM", data));
        for (int cylinder = 0; cylinder < 2; ++cylinder)
            for (const DiscSector& sector : disc.track(cylinder, 0)->sectors)
                CHECK(std::all_of(sector.data.begin(), sector.data.end(), [](uint8_t b) { return b == 0xE5; }));
    }
    // Blocks of 4K, four 16K to a directory entry, two sides.
    {
        Disc disc;
        disc.format(formatNamed("DATA (DS 80)"));
        DiscFiles files(disc);
        CHECK(files.valid() && std::string(files.format()->name) == "DATA (DS 80)");
        const Bytes large = pattern(150000, 6), little = pattern(5000, 7), edge = pattern(65536, 8);
        CHECK(files.write("LARGE.BIN", large));
        CHECK(files.write("LITTLE", little));
        CHECK(files.write("EDGE", edge));
        CHECK(holds(files, "LARGE.BIN", large));
        CHECK(holds(files, "LITTLE", little));
        CHECK(holds(files, "EDGE", edge));
        CHECK(files.remove(*find(files.list(), "LARGE.BIN")));
        CHECK(holds(files, "EDGE", edge));
    }
    // More than 255 blocks: their numbers take two bytes in the directory.
    {
        Disc disc;
        disc.format(formatNamed("ROMDOS D10"));
        DiscFiles files(disc);
        CHECK(files.valid() && std::string(files.format()->name) == "ROMDOS D10");
        const Bytes first = pattern(600000, 9), second = pattern(40000, 10);
        CHECK(files.write("FIRST", first));
        CHECK(files.write("SECOND", second));
        CHECK(holds(files, "FIRST", first));
        CHECK(holds(files, "SECOND", second));
    }
    // IBM: eight sectors to a track, numbered from 1.
    {
        Disc disc;
        disc.format(formatNamed("IBM (SS 40)"));
        DiscFiles files(disc);
        CHECK(files.valid() && std::string(files.format()->name) == "IBM (SS 40)");
        const Bytes data = pattern(30000, 11);
        CHECK(files.write("A.B", data));
        CHECK(holds(files, "A.B", data));
    }
}

void testNames()
{
    CHECK(DiscFiles::directoryName("game.bas") == std::string("GAME.BAS"));
    CHECK(DiscFiles::directoryName("DISC") == std::string("DISC"));
    CHECK(DiscFiles::directoryName("12345678.123") == std::string("12345678.123"));
    CHECK(DiscFiles::directoryName("-R#D2!.$$$") == std::string("-R#D2!.$$$"));
    CHECK(!DiscFiles::directoryName(""));
    CHECK(!DiscFiles::directoryName(".BAS"));
    CHECK(!DiscFiles::directoryName("123456789"));
    CHECK(!DiscFiles::directoryName("A.BASX"));
    CHECK(!DiscFiles::directoryName("A B"));
    CHECK(!DiscFiles::directoryName("WHAT?.BAS"));
    CHECK(!DiscFiles::directoryName("A*"));
}

void testAmsdosHeader()
{
    AmsdosHeader header;
    header.type = 2;
    header.loadAddress = 0x8000;
    header.entryAddress = 0x8010;
    header.length = 0x1234;
    const Bytes bytes = makeAmsdosHeader("game.bin", header);
    CHECK_EQ(bytes.size(), 128);
    CHECK(std::string(bytes.begin() + 1, bytes.begin() + 12) == "GAME    BIN");
    CHECK_EQ(bytes[18], 2);
    CHECK_EQ(bytes[21] | bytes[22] << 8, 0x8000);
    CHECK_EQ(bytes[24] | bytes[25] << 8, 0x1234);
    CHECK_EQ(bytes[26] | bytes[27] << 8, 0x8010);
    CHECK_EQ(bytes[64] | bytes[65] << 8 | bytes[66] << 16, 0x1234);
    const auto read = amsdosHeader(bytes);
    CHECK(read && read->type == 2 && read->loadAddress == 0x8000 && read->entryAddress == 0x8010 && read->length == 0x1234);
    // A byte out of place, a file of zeroes, a short one: no header.
    Bytes wrong = bytes;
    wrong[30] ^= 1;
    CHECK(!amsdosHeader(wrong));
    CHECK(!amsdosHeader(Bytes(128, 0)));
    CHECK(!amsdosHeader(Bytes(bytes.begin(), bytes.begin() + 100)));
    CHECK(!amsdosHeader(pattern(500, 12)));
}

}  // namespace

int main()
{
    testDataDisc();
    testFullDirectory();
    testOtherFormats();
    testNames();
    testAmsdosHeader();
    return checkSummary("discfiles");
}
