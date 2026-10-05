// Runs Kevin Thacker's "acid tests" (hardware test programs written for the
// Arnold emulator and checked on real machines) and compares their verdicts
// with what is expected of TuxAPE today.
//
// Each program prints "<test name>-PASS" or "-FAIL". A program is listed
// here with the tests it is known to fail; the run fails if any other test
// fails, and says so if a known failure has started to pass, so that the
// list can be shortened.
//
//   acid_tests <acid test folder> [program...]
//
// Exits with 77 (skipped) if the folder or the ROM images are missing.

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/files.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

struct Program {
    const char* name;      // name used on the command line
    const char* disc;      // image, relative to the acid test folder
    const char* run;       // file to RUN
    const char* key;       // typed once the program has started
    CpcModel model;
    CrtcType crtc;
    int frames;            // emulated frames to let it run
    int expectedTests;     // verdicts it prints when it runs to the end
    std::vector<const char*> knownFailures;
};

// Notes on the entries:
// - ppi: the four failures are the 8255's strobed mode (mode 1), which
//   nothing on the CPC uses and TuxAPE does not emulate yet.
// - cpctest stops after four tests: its fifth, as shipped, jumps to an
//   address left over from the fourth and restarts the machine.
// - crtctest, as shipped, tests against the Plus ASIC's CRTC whatever key
//   is pressed, and needs a 64K machine (it writes &FF to port &0000, which
//   on a 6128 switches its own code out). Two of its tests cannot pass: the
//   I/O decode test records the wrong register, and the "htot/2" test
//   compares what it reads (0 or 1) with &FF. The other failures are in the
//   interlace modes of R8, emulated on this CRTC from the Compendium's
//   description: "R8 - count lines" gets 29 of its 32 frame lengths, and
//   the two "vsync r8" tests see the VSYNC a line away from where the
//   program expects it.
const Program kPrograms[] = {
    {"psg", "psg/psg.dsk", "PSG", " ", CpcModel::Cpc6128, CrtcType::HD6845S, 12000, 20, {}},
    {"cpu", "z80tests/cpu.dsk", "CPU", " ", CpcModel::Cpc6128, CrtcType::HD6845S, 12000, 8, {}},
    {"inout", "z80tests/cpu.dsk", "INOUT", " ", CpcModel::Cpc6128, CrtcType::HD6845S, 12000, 8, {}},
    {"itest", "z80tests/cpu.dsk", "ITEST", " ", CpcModel::Cpc6128, CrtcType::HD6845S, 40000, 490, {}},
    {"cpctest", "cpc/cpc.dsk", "CPCTEST", " ", CpcModel::Cpc6128, CrtcType::HD6845S, 6000, 4, {}},
    {"ppi", "ppi/ppi.dsk", "PPI", " ", CpcModel::Cpc6128, CrtcType::HD6845S, 40000, 23,
     {"mode 1: port A mode 1 (input), port B mode 0, port C bits output",
      "mode 1: port A mode 1 (output), port B mode 0, port C bits output",
      "mode 1: port A mode 1 (input), port B mode 0, port C bits input",
      "mode 1: port A mode 1 (output), port B mode 0, port C bits input"}},
    {"crtctest", "crtc/crtc1.dsk", "CRTCTEST", "3", CpcModel::Cpc664, CrtcType::AsicPlus, 600000, 22,
     {"I/O decode test-1111011110000010",
      "vsync r8 test (r8=1)",
      "vsync r8 test (r8=3)",
      "vsync r8 htot/2 test",
      "R8 - count lines"}},
};

struct Verdict {
    std::string test;
    bool pass;
};

// Boots the machine, runs the program and returns its verdicts in order.
std::vector<Verdict> run(const Program& program, const std::filesystem::path& folder)
{
    std::vector<Verdict> verdicts;
    Cpc cpc;
    cpc.crtc().setType(program.crtc);
    const auto image = readFile(folder / program.disc);
    auto disc = image ? Disc::fromDsk(*image) : std::nullopt;
    if (!disc || !setupStockMachine(cpc, program.model, defaultRomDir(), nullptr)) {
        std::printf("%s: cannot set up (disc or ROM images missing)\n", program.name);
        ++g_failures;
        return verdicts;
    }
    cpc.fdc().setFast(true);
    cpc.fdc().drive(0).disc = std::make_unique<Disc>(std::move(*disc));

    // Everything the program prints goes through the firmware's TXT OUTPUT.
    std::string line;
    cpc.watchAddress(0xBB5A);
    cpc.setExecHook([&](uint16_t) {
        const char c = static_cast<char>(cpc.cpu().reg[cpc.cpu().A]);
        if (c >= 32 && c < 127) {
            line += c;
            return;
        }
        if (c != '\n' && c != '\r')
            return;
        // Result details start with a four-digit index; skip those.
        const bool detail = line.size() > 5 && line[4] == ':';
        for (const char* suffix : {"-PASS", "-FAIL"}) {
            const size_t length = std::strlen(suffix);
            if (!detail && line.size() >= length && line.compare(line.size() - length, length, suffix) == 0)
                verdicts.push_back({line.substr(0, line.size() - length), suffix[1] == 'P'});
        }
        line.clear();
    });

    for (int frame = 0; frame < 150; ++frame)
        cpc.runFrame();
    AutoType keys(cpc.keyboard());
    keys.type(std::string("RUN\"") + program.run + "\n~PAUSE 300~" + program.key);
    while (keys.active()) {
        keys.frame();
        cpc.runFrame();
    }
    // The programs wait for a key between pages of results.
    for (int frame = 0; frame < program.frames && static_cast<int>(verdicts.size()) < program.expectedTests; ++frame) {
        cpc.keyboard().set(CpcKey::Space, frame % 40 < 3);
        cpc.runFrame();
    }
    return verdicts;
}

// The first verdict of a program is printed on the same line as its
// "press a key" prompt.
std::string cleaned(std::string test)
{
    const std::string prompt = "Press a key to start";
    if (test.compare(0, prompt.size(), prompt) == 0)
        test.erase(0, prompt.size());
    return test;
}

void check(const Program& program, const std::filesystem::path& folder)
{
    const std::vector<Verdict> verdicts = run(program, folder);
    int passed = 0;
    std::vector<bool> seen(program.knownFailures.size(), false);
    for (const Verdict& verdict : verdicts) {
        const std::string test = cleaned(verdict.test);
        size_t known = 0;
        while (known < program.knownFailures.size() && test != program.knownFailures[known])
            ++known;
        const bool isKnown = known < program.knownFailures.size();
        if (isKnown)
            seen[known] = true;
        if (verdict.pass) {
            ++passed;
            if (isKnown)
                std::printf("%s: \"%s\" now passes; take it off the list of known failures\n", program.name,
                            test.c_str());
        } else if (!isKnown) {
            std::printf("%s: \"%s\" FAILS\n", program.name, test.c_str());
            ++g_failures;
        }
    }
    if (static_cast<int>(verdicts.size()) != program.expectedTests) {
        std::printf("%s: %zu verdicts, expected %d (did the program run to its end?)\n", program.name,
                    verdicts.size(), program.expectedTests);
        ++g_failures;
    }
    std::printf("%-9s %3d passed, %zu failed (%zu known)\n", program.name, passed, verdicts.size() - passed,
                program.knownFailures.size());
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <acid test folder> [program...]\n", argv[0]);
        return 2;
    }
    const std::filesystem::path folder = argv[1];
    Cpc probe;
    if (!std::filesystem::is_directory(folder) || !setupStockMachine(probe, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        std::printf("acid tests or ROM images not found; skipping\n");
        return 77;
    }
    for (const Program& program : kPrograms) {
        bool wanted = argc == 2;
        for (int i = 2; i < argc; ++i)
            wanted = wanted || std::strcmp(argv[i], program.name) == 0;
        if (wanted)
            check(program, folder);
    }
    return checkSummary("acid_tests");
}
