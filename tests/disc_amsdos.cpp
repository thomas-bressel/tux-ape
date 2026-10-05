// Disc drive tests through the real AMSDOS ROM: a blank disc is put in the
// drive of a CPC6128, a BASIC program is saved, listed and loaded back.
// Run once with real disc timing and once in fast mode.
//
// Needs the ROM images; exits with code 77 (skipped) without them.

#include <cstring>
#include <memory>
#include <string>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/discfiles.h"
#include "core/screen_text.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

struct Machine {
    Cpc cpc;
    AutoType keys{cpc.keyboard()};

    void frames(int count)
    {
        for (int i = 0; i < count; ++i) {
            keys.frame();
            cpc.runFrame();
        }
    }

    // Types a line, then waits for BASIC to come back to its prompt: the
    // screen then ends with "Ready" and the cursor, which reads as '?'.
    // Disc operations take seconds of emulated time.
    void command(const char* line)
    {
        keys.type(line);
        keys.type("\n");
        for (int i = 0; i < 50 * 30; ++i) {
            frames(1);
            if (keys.active())
                continue;
            std::string screen = readScreenText(cpc);
            while (!screen.empty() && screen.back() == '\n')
                screen.pop_back();
            if (screen.size() >= 7 && screen.compare(screen.size() - 7, 7, "Ready\n?") == 0)
                return;
        }
        std::printf("timed out after: %s\n", line);
        ++g_failures;
    }

    // Types a line that BASIC stores without answering.
    void enter(const char* line)
    {
        keys.type(line);
        keys.type("\n");
        while (keys.active())
            frames(1);
        frames(10);
    }
};

bool contains(const std::string& text, const char* what)
{
    return text.find(what) != std::string::npos;
}

void saveAndLoad(bool fast)
{
    Machine m;
    if (!setupStockMachine(m.cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        ++g_failures;
        return;
    }
    m.cpc.fdc().setFast(fast);
    auto blank = std::make_unique<Disc>();
    blank->format(defaultDiscFormat());
    blank->modified = false;
    m.cpc.fdc().drive(0).disc = std::move(blank);
    m.frames(150);

    m.enter("10 PRINT \"saved on disc\"");
    m.command("SAVE\"HELLO\"");
    Disc& disc = *m.cpc.fdc().drive(0).disc;
    CHECK(disc.modified);

    // The directory is the first sector of a DATA format disc.
    const DiscTrack* track0 = disc.track(0, 0);
    CHECK(track0 != nullptr);
    const DiscSector* directory = nullptr;
    for (const DiscSector& sector : track0->sectors)
        if (sector.r == 0xC1)
            directory = &sector;
    CHECK(directory != nullptr);
    if (directory)
        CHECK(std::memcmp(&directory->data[1], "HELLO   BAS", 11) == 0);

    m.command("CLS:CAT");
    std::string screen = readScreenText(m.cpc);
    CHECK(contains(screen, "Drive A: user  0"));
    CHECK(contains(screen, "HELLO   .BAS"));
    CHECK(contains(screen, "177K free"));

    m.command("NEW");
    m.command("CLS:LOAD\"HELLO\"");
    m.command("LIST");
    screen = readScreenText(m.cpc);
    CHECK(contains(screen, "10 PRINT \"saved on disc\""));

    m.command("RUN");
    CHECK(contains(readScreenText(m.cpc), "\nsaved on disc\n"));

    // The same disc seen without the machine: the file AMSDOS wrote is
    // found, with its header; and a file put there from outside is one
    // AMSDOS lists, loads and runs.
    {
        DiscFiles files(disc);
        CHECK(files.valid());
        const std::vector<DiscFile> list = files.list();
        CHECK_EQ(list.size(), 1);
        CHECK(list.size() == 1 && list[0].name == "HELLO.BAS" && list[0].user == 0 && list[0].size == 256);
        CHECK_EQ(files.freeBytes(), 177 * 1024);
        const auto saved = list.empty() ? std::nullopt : files.read(list[0]);
        const auto header = saved ? amsdosHeader(*saved) : std::nullopt;
        CHECK(header.has_value());
        CHECK(header && header->type == 0 && header->loadAddress == 0x0170);
        // A tokenised line: its length, its number, PRINT, the text.
        CHECK(header && header->length > 20 && header->length < 40);
        CHECK(saved && (*saved)[128 + 2] == 10 && (*saved)[128 + 3] == 0 && (*saved)[128 + 4] == 0xBF);

        const std::string listing = "10 PRINT \"from outside\"\r\n20 PRINT 6*7\r\n";
        CHECK(files.write("host.bas", {reinterpret_cast<const uint8_t*>(listing.data()), listing.size()}));
    }
    m.command("CLS:CAT");
    screen = readScreenText(m.cpc);
    CHECK(contains(screen, "HELLO   .BAS"));
    CHECK(contains(screen, "HOST    .BAS"));
    CHECK(contains(screen, "176K free"));
    m.command("CLS:RUN\"HOST\"");
    screen = readScreenText(m.cpc);
    CHECK(contains(screen, "from outside"));
    CHECK(contains(screen, "\n 42"));

    // The image survives being written out and read back.
    const std::vector<uint8_t> file = disc.toDsk();
    const auto reloaded = Disc::fromDsk(file);
    CHECK(reloaded.has_value());
    if (reloaded) {
        CHECK_EQ(reloaded->cylinders(), 40);
        CHECK_EQ(reloaded->sides(), 1);
        CHECK(reloaded->toDsk() == file);
    }

    if (g_failures)
        std::printf("(%s mode) screen was:\n%s\n", fast ? "fast" : "accurate", readScreenText(m.cpc).c_str());
}

void emptyDrive()
{
    // With no disc in the drive AMSDOS reports it and BASIC carries on.
    Machine m;
    if (!setupStockMachine(m.cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr))
        return;
    m.frames(150);
    m.keys.type("CAT\n");
    m.frames(50 * 6);
    CHECK(contains(readScreenText(m.cpc), "Drive A: disc missing"));
    CHECK(contains(readScreenText(m.cpc), "Retry, Ignore or Cancel?"));
}

}  // namespace

int main()
{
    Cpc probe;
    if (!setupStockMachine(probe, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        std::printf("ROM images not found; skipping\n");
        return 77;
    }
    saveAndLoad(false);
    saveAndLoad(true);
    emptyDrive();
    return checkSummary("disc_amsdos");
}
