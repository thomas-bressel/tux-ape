// Runs the emulator without a window: boots a machine, optionally types
// text, then saves a screenshot and/or prints the screen as text. It can
// also play a CSL script, saving the pictures the script or the emulated
// program asks for. Meant for automated checks and for debugging on machines
// without a display.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "core/autotype.h"
#include "core/cpc.h"
#include "core/csl.h"
#include "core/files.h"
#include "core/screen_text.h"
#include "core/setup.h"
#include "core/snapshot.h"

namespace {

void usage(const char* program)
{
    std::fprintf(stderr,
                 "usage: %s [options]\n"
                 "  --model 464|664|6128|464plus|6128plus\n"
                 "                         machine to emulate (default 6128)\n"
                 "  --crtc 0-4             CRTC type (default 0)\n"
                 "  --rom-dir DIR          folder holding the ROM images\n"
                 "  --disc FILE            disc image for drive A:\n"
                 "  --fast-disc            no waiting for the disc drive\n"
                 "  --snapshot FILE        load this snapshot once the machine has started\n"
                 "  --save-snapshot FILE   save a snapshot at the end\n"
                 "  --frames N             frames to run before typing (default 150)\n"
                 "  --type TEXT            text to type, in WinAPE Auto-Type syntax\n"
                 "  --after N              frames to run after typing (default 50)\n"
                 "  --tap N                press SPACE every N frames while running on,\n"
                 "                         for programs that wait for a key between pages\n"
                 "  --bench SECONDS        then run that many emulated seconds, sound included,\n"
                 "                         and say how fast the emulation goes\n"
                 "  --png FILE             save the final picture\n"
                 "  --text                 print the final screen as text\n"
                 "  --capture              print everything the program printed\n"
                 "  --csl FILE             play a CSL script instead of the above\n"
                 "  --screenshot-dir DIR   where the script's pictures go (default .)\n",
                 program);
}

// Saves the monitor's picture with its scanlines doubled, which gives it
// the right proportions.
bool savePicture(tuxape::Cpc& cpc, const std::filesystem::path& path)
{
    using tuxape::Monitor;
    const uint32_t* frame = cpc.monitor().frame();
    std::vector<uint32_t> doubled(static_cast<size_t>(Monitor::kWidth) * Monitor::kHeight * 2);
    for (int y = 0; y < Monitor::kHeight * 2; ++y)
        std::memcpy(&doubled[static_cast<size_t>(y) * Monitor::kWidth], frame + (y / 2) * Monitor::kWidth,
                    Monitor::kWidth * sizeof(uint32_t));
    return tuxape::writePng(path, doubled.data(), Monitor::kWidth, Monitor::kHeight * 2);
}

}  // namespace

int main(int argc, char* argv[])
{
    using namespace tuxape;

    CpcModel model = CpcModel::Cpc6128;
    int crtcType = 0;
    std::filesystem::path romDir = defaultRomDir();
    int frames = 150;
    int after = 50;
    int tap = 0;
    int benchSeconds = 0;
    std::string typed;
    std::string png;
    std::string discFile;
    std::string snapshotFile;
    std::string saveSnapshotFile;
    bool fastDisc = false;
    bool printText = false;
    bool capture = false;
    std::string cslFile;
    std::filesystem::path screenshotDir = ".";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage(argv[0]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--model") {
            const std::string m = value();
            model = m == "464"        ? CpcModel::Cpc464
                    : m == "664"      ? CpcModel::Cpc664
                    : m == "464plus"  ? CpcModel::Plus464
                    : m == "6128plus" ? CpcModel::Plus6128
                                      : CpcModel::Cpc6128;
        } else if (arg == "--crtc") {
            crtcType = std::atoi(value());
        } else if (arg == "--rom-dir") {
            romDir = value();
        } else if (arg == "--frames") {
            frames = std::atoi(value());
        } else if (arg == "--after") {
            after = std::atoi(value());
        } else if (arg == "--tap") {
            tap = std::atoi(value());
        } else if (arg == "--bench") {
            benchSeconds = std::atoi(value());
        } else if (arg == "--type") {
            typed = value();
        } else if (arg == "--disc") {
            discFile = value();
        } else if (arg == "--snapshot") {
            snapshotFile = value();
        } else if (arg == "--save-snapshot") {
            saveSnapshotFile = value();
        } else if (arg == "--fast-disc") {
            fastDisc = true;
        } else if (arg == "--png") {
            png = value();
        } else if (arg == "--text") {
            printText = true;
        } else if (arg == "--capture") {
            capture = true;
        } else if (arg == "--csl") {
            cslFile = value();
        } else if (arg == "--screenshot-dir") {
            screenshotDir = value();
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    Cpc cpc;
    cpc.crtc().setType(static_cast<CrtcType>(crtcType));
    std::string error;
    if (!setupStockMachine(cpc, model, romDir, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    if (!cslFile.empty()) {
        CslRunner runner(cpc);
        runner.setRomDir(romDir);
        if (!discFile.empty())
            runner.setFallbackDisc(discFile);
        std::error_code ec;
        std::filesystem::create_directories(screenshotDir, ec);
        runner.setScreenshotSink([&](const std::string& name) {
            // The time helps to see where a script and the program it
            // drives have drifted apart.
            std::printf("%9.3f s  %s\n", static_cast<double>(cpc.microseconds()) / 1e6, name.c_str());
            return savePicture(cpc, screenshotDir / (name + ".png"));
        });
        runner.setSnapshotSink([&](const std::string& name, const std::vector<uint8_t>& data) {
            return writeFile(screenshotDir / (name + ".sna"), data);
        });
        const bool ok = runner.run(cslFile);
        std::printf("%d picture(s) saved in %s\n", runner.screenshotCount(), screenshotDir.string().c_str());
        if (!ok)
            std::fprintf(stderr, "%s\n", runner.error().c_str());
        return ok ? 0 : 1;
    }

    // Everything printed goes through the firmware's TXT OUTPUT entry.
    std::string printed;
    if (capture) {
        constexpr uint16_t kTxtOutput = 0xBB5A;
        cpc.watchAddress(kTxtOutput);
        cpc.setExecHook([&](uint16_t) {
            const char c = static_cast<char>(cpc.cpu().reg[cpc.cpu().A]);
            if (c == '\n' || (c >= 32 && c < 127))
                printed += c;
        });
    }

    cpc.fdc().setFast(fastDisc);
    if (!discFile.empty()) {
        const auto file = readFile(discFile);
        auto disc = file ? Disc::fromDsk(*file) : std::nullopt;
        if (!disc) {
            std::fprintf(stderr, "cannot read disc image %s\n", discFile.c_str());
            return 1;
        }
        cpc.fdc().drive(0).disc = std::make_unique<Disc>(std::move(*disc));
    }

    for (int f = 0; f < frames; ++f)
        cpc.runFrame();
    if (!snapshotFile.empty()) {
        const auto file = readFile(snapshotFile);
        std::string why = "cannot read the file";
        if (!file || !loadSnapshot(cpc, *file, &why)) {
            std::fprintf(stderr, "%s: %s\n", snapshotFile.c_str(), why.c_str());
            return 1;
        }
    }

    AutoType autoType(cpc.keyboard());
    autoType.type(typed);
    while (autoType.active()) {
        autoType.frame();
        cpc.runFrame();
    }
    for (int f = 0; f < after; ++f) {
        if (tap > 0)
            cpc.keyboard().set(CpcKey::Space, f % tap < 3);
        cpc.runFrame();
    }

    if (benchSeconds > 0) {
        // Processor time, not time on the clock: what else the host is
        // doing does not count.
        cpc.audio().setSampleRate(44100);
        const uint64_t emulatedStart = cpc.microseconds();
        const std::clock_t cpuStart = std::clock();
        while (cpc.microseconds() - emulatedStart < static_cast<uint64_t>(benchSeconds) * 1000000) {
            cpc.runFrame();
            cpc.audio().samples().clear();
        }
        const double cpu = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
        const double emulated = static_cast<double>(cpc.microseconds() - emulatedStart) / 1e6;
        std::printf("%.1f emulated seconds in %.2f s of processor time: %.1f times the speed of a CPC,\n"
                    "or %.1f %% of one core at normal speed\n",
                    emulated, cpu, emulated / cpu, 100.0 * cpu / emulated);
    }

    if (capture)
        std::fputs(printed.c_str(), stdout);
    if (printText)
        std::fputs(readScreenText(cpc).c_str(), stdout);

    if (!png.empty() && !savePicture(cpc, png)) {
        std::fprintf(stderr, "cannot write %s\n", png.c_str());
        return 1;
    }
    if (!saveSnapshotFile.empty()) {
        const SnapshotMachine machine = model == CpcModel::Cpc464     ? SnapshotMachine::Cpc464
                                        : model == CpcModel::Cpc664   ? SnapshotMachine::Cpc664
                                        : model == CpcModel::Plus464  ? SnapshotMachine::Plus464
                                        : model == CpcModel::Plus6128 ? SnapshotMachine::Plus6128
                                                                      : SnapshotMachine::Cpc6128;
        if (!writeFile(saveSnapshotFile, saveSnapshot(cpc, machine))) {
            std::fprintf(stderr, "cannot write %s\n", saveSnapshotFile.c_str());
            return 1;
        }
    }
    return 0;
}
