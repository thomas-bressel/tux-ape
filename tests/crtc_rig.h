#pragma once

// A CRTC on its own, set up for the firmware's 50 Hz screen, for the tests
// that drive the chip register by register. A write made after the tick
// that brings character N is a write "during character N".

#include "check.h"
#include "core/crtc.h"

struct Rig {
    tuxape::Crtc crtc;
    explicit Rig(tuxape::CrtcType type = tuxape::CrtcType::HD6845S)
    {
        crtc.setType(type);
        crtc.reset();
        // The firmware's 50 Hz screen.
        static const uint8_t standard[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7};
        for (int r = 0; r < 10; ++r)
            set(r, standard[r]);
        seek(0, 0);
    }

    void set(int reg, int value)
    {
        crtc.select(static_cast<uint8_t>(reg));
        crtc.write(static_cast<uint8_t>(value));
    }

    // Moves on to the next time the counters C4, C9 and C0 have these values.
    void seek(int c4, int c9, int c0 = 0)
    {
        for (int guard = 0; guard < 400000; ++guard) {
            crtc.tick();
            if (crtc.vcc() == c4 && crtc.vlc() == c9 && crtc.hcc() == c0)
                return;
        }
        std::printf("seek(%d, %d, %d): never reached\n", c4, c9, c0);
        ++g_failures;
    }

    // Moves on to a character further along the current line.
    void to(int c0)
    {
        while (crtc.hcc() != c0)
            crtc.tick();
    }

    void nextLine()
    {
        do
            crtc.tick();
        while (crtc.hcc() != 0);
    }

    // Characters from here to the next start of a frame.
    int ticksToFrameStart()
    {
        int ticks = 0;
        do {
            crtc.tick();
            ++ticks;
        } while (!(crtc.vcc() == 0 && crtc.vlc() == 0 && crtc.hcc() == 0) && ticks < 400000);
        return ticks;
    }

    // Whether a VSYNC is seen anywhere on the next `lines` lines.
    bool vsyncWithin(int lines)
    {
        bool seen = crtc.vsync();
        for (int i = 0; i < lines * 64; ++i) {
            crtc.tick();
            seen = seen || crtc.vsync();
        }
        return seen;
    }

    // Lines from here to the next start of a frame (the chip is at the
    // start of a line).
    int linesToFrameStart()
    {
        int lines = 0;
        do {
            nextLine();
            ++lines;
        } while (!(crtc.vcc() == 0 && crtc.vlc() == 0) && lines < 5000);
        return lines;
    }
};
