// Runs the emulator without a window: boots a machine, optionally types
// text, then saves a screenshot and/or prints the screen as text. Meant for
// automated checks and for debugging on machines without a display.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/autotype.h"
#include "core/cpc.h"
#include "core/files.h"
#include "core/screen_text.h"
#include "core/setup.h"

namespace {

void usage(const char* program)
{
    std::fprintf(stderr,
                 "usage: %s [options]\n"
                 "  --model 464|664|6128   machine to emulate (default 6128)\n"
                 "  --crtc 0-4             CRTC type (default 0)\n"
                 "  --rom-dir DIR          folder holding the ROM images\n"
                 "  --frames N             frames to run before typing (default 150)\n"
                 "  --type TEXT            text to type, in WinAPE Auto-Type syntax\n"
                 "  --after N              frames to run after typing (default 50)\n"
                 "  --png FILE             save the final picture\n"
                 "  --text                 print the final screen as text\n",
                 program);
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
    std::string typed;
    std::string png;
    bool printText = false;

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
            model = m == "464" ? CpcModel::Cpc464 : m == "664" ? CpcModel::Cpc664 : CpcModel::Cpc6128;
        } else if (arg == "--crtc") {
            crtcType = std::atoi(value());
        } else if (arg == "--rom-dir") {
            romDir = value();
        } else if (arg == "--frames") {
            frames = std::atoi(value());
        } else if (arg == "--after") {
            after = std::atoi(value());
        } else if (arg == "--type") {
            typed = value();
        } else if (arg == "--png") {
            png = value();
        } else if (arg == "--text") {
            printText = true;
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

    for (int f = 0; f < frames; ++f)
        cpc.runFrame();

    AutoType autoType(cpc.keyboard());
    autoType.type(typed);
    while (autoType.active()) {
        autoType.frame();
        cpc.runFrame();
    }
    for (int f = 0; f < after; ++f)
        cpc.runFrame();

    if (printText)
        std::fputs(readScreenText(cpc).c_str(), stdout);

    if (!png.empty()) {
        // Double the scanlines so the picture has the right proportions.
        const uint32_t* frame = cpc.monitor().frame();
        std::vector<uint32_t> doubled(static_cast<size_t>(Monitor::kWidth) * Monitor::kHeight * 2);
        for (int y = 0; y < Monitor::kHeight * 2; ++y)
            std::memcpy(&doubled[static_cast<size_t>(y) * Monitor::kWidth], frame + (y / 2) * Monitor::kWidth,
                        Monitor::kWidth * sizeof(uint32_t));
        if (!writePng(png, doubled.data(), Monitor::kWidth, Monitor::kHeight * 2)) {
            std::fprintf(stderr, "cannot write %s\n", png.c_str());
            return 1;
        }
    }
    return 0;
}
