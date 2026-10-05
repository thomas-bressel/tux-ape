// The interlace modes of R8, as the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System describes them (chapter 19) and as test 1 of the
// Logon System "Shaker", module B, measures them on real machines.

#include <vector>

#include "check.h"
#include "crtc_rig.h"

namespace {

using namespace tuxape;

// Leaves the chip at the start of a frame of the parity asked for, with the
// parity frozen there: R6 is put out of C4's reach once the parity of the
// frames to come has been settled (19.5.2).
void freezeParity(Rig& rig, bool odd)
{
    rig.seek(0, 0);
    if (rig.crtc.oddFrame() == odd)
        rig.seek(0, 0);
    rig.seek(26, 0);  // past R6 = 25: the next frame will be of the other parity
    rig.set(6, 0x7F);
    rig.seek(0, 0);
    CHECK_EQ(rig.crtc.oddFrame(), odd);
}

// The frame parity lives its life whatever R8 holds.
void parity0()
{
    Rig rig;
    const bool first = rig.crtc.oddFrame();
    rig.seek(0, 0);
    CHECK_EQ(rig.crtc.oddFrame(), !first);
    rig.seek(0, 0);
    CHECK_EQ(rig.crtc.oddFrame(), first);

    // It is settled as C4 meets R6: out of reach, it stays where it was.
    rig.seek(26, 0);
    rig.set(6, 0x7F);
    rig.seek(0, 0);
    CHECK_EQ(rig.crtc.oddFrame(), !first);
    rig.seek(0, 0);
    CHECK_EQ(rig.crtc.oddFrame(), !first);
}

// Either interlace mode gives even frames one more line, and holds their
// VSYNC back to the middle of the line.
void interlaceSync0()
{
    Rig rig;
    rig.set(8, 1);
    rig.seek(0, 0);
    if (rig.crtc.oddFrame())
        rig.seek(0, 0);
    CHECK_EQ(rig.linesToFrameStart(), 313);
    CHECK(rig.crtc.oddFrame());
    CHECK_EQ(rig.linesToFrameStart(), 312);
    CHECK(!rig.crtc.oddFrame());

    // The line added is counted like one asked for through R5: C4 goes one
    // past R4, C9 back to 0.
    rig.seek(38, 7);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 39);
    CHECK_EQ(rig.crtc.vlc(), 0);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 0);
    CHECK(rig.crtc.oddFrame());

    // Odd frame: the VSYNC starts with row R7.
    rig.seek(30, 0);
    CHECK(rig.crtc.vsync());
    // Even frame: at C0 = R0 / 2.
    rig.seek(30, 0);
    CHECK(!rig.crtc.oddFrame());
    CHECK(!rig.crtc.vsync());
    rig.to(30);
    CHECK(!rig.crtc.vsync());
    rig.to(31);
    CHECK(rig.crtc.vsync());

    // With R5, the line comes after the adjustment lines.
    rig.set(5, 3);
    rig.seek(0, 0);
    if (rig.crtc.oddFrame())
        rig.seek(0, 0);
    CHECK_EQ(rig.linesToFrameStart(), 316);
    CHECK_EQ(rig.linesToFrameStart(), 315);
}

// "Sync & video": each frame shows the lines of its parity, so that rows
// take half as many lines.
void interlaceVideo0()
{
    Rig rig;
    rig.set(9, 6);
    rig.set(8, 3);
    rig.seek(0, 0);
    if (rig.crtc.oddFrame())
        rig.seek(0, 0);
    for (int line = 0; line < 12; ++line) {
        CHECK_EQ(rig.crtc.vcc(), line / 4);
        CHECK_EQ(rig.crtc.ra(), (line % 4) * 2);
        rig.nextLine();
    }
    rig.seek(0, 0);
    CHECK(rig.crtc.oddFrame());
    for (int line = 0; line < 12; ++line) {
        CHECK_EQ(rig.crtc.vcc(), line / 4);
        CHECK_EQ(rig.crtc.ra(), (line % 4) * 2 + 1);
        rig.nextLine();
    }
    // 39 rows of 4 lines, and the line added to even frames.
    rig.seek(0, 0);
    CHECK_EQ(rig.linesToFrameStart(), 39 * 4 + 1);
    CHECK_EQ(rig.linesToFrameStart(), 39 * 4);
}

// With R9 odd a row has an odd number of lines to share between two frames.
// Rows of even lines get the line more, and from one row to the next the
// two kinds alternate (19.5.2).
void oddR9()
{
    Rig rig;
    rig.set(8, 3);  // R9 = 7: nine lines
    rig.seek(0, 0);
    if (rig.crtc.oddFrame())
        rig.seek(0, 0);
    static const int even[] = {0, 2, 4, 6, 8, 1, 3, 5, 7, 0, 2, 4, 6, 8, 1};
    for (int line = 0; line < 15; ++line) {
        CHECK_EQ(rig.crtc.vcc(), line < 5 ? 0 : line < 9 ? 1 : line < 14 ? 2 : 3);
        CHECK_EQ(rig.crtc.ra(), even[line]);
        rig.nextLine();
    }
    rig.seek(0, 0);
    CHECK(rig.crtc.oddFrame());
    static const int odd[] = {1, 3, 5, 7, 0, 2, 4, 6, 8, 1, 3, 5, 7, 0};
    for (int line = 0; line < 14; ++line) {
        CHECK_EQ(rig.crtc.vcc(), line < 4 ? 0 : line < 9 ? 1 : line < 13 ? 2 : 3);
        CHECK_EQ(rig.crtc.ra(), odd[line]);
        rig.nextLine();
    }

    // On an odd frame, a VSYNC on an odd row waits for that row's second
    // line, which keeps it in step with the even frame's (16.5.1).
    rig.set(7, 3);
    rig.seek(0, 0);
    if (!rig.crtc.oddFrame())
        rig.seek(0, 0);
    rig.seek(3, 0);
    CHECK(!rig.crtc.vsync());
    rig.nextLine();
    CHECK(rig.crtc.vsync());
    rig.set(7, 2);
    rig.seek(0, 0);
    if (!rig.crtc.oddFrame())
        rig.seek(0, 0);
    rig.seek(2, 0);
    CHECK(rig.crtc.vsync());
}

// The mode can be set and dropped anywhere. C9 only takes its doubled form
// on the line after the write, and keeps it for the line of the write that
// ends the mode, so that these two lines compare the wrong value with R9
// (19.8.1, tables "switching to IVM mode" and "exit of IVM mode").
void midFrame0()
{
    struct Case {
        bool odd;
        int line;                 // C9 of the line R8 is written on
        std::vector<int> c9, out;  // what follows on row 0
    };
    const std::vector<Case> entering = {
        {false, 0, {1, 2, 3}, {2, 4, 6}},
        {true, 0, {1, 2, 3}, {3, 5, 7}},
        {false, 2, {3}, {6}},
        {true, 2, {3}, {7}},
        // C9 has gone past the place where the row ends: it runs on until
        // its doubled form, on 5 bits, comes round to R9.
        {false, 3, {4, 5, 15, 16, 19}, {8, 10, 30, 0, 6}},
        {true, 3, {4, 5, 15, 16, 19}, {9, 11, 31, 1, 7}},
        // C9 = R9: the row ends on an even frame; on an odd one R9 counts
        // as R9 + 1 from the moment of the write.
        {false, 6, {}, {}},
        {true, 6, {7, 8, 19}, {15, 17, 7}},
    };
    for (const Case& test : entering) {
        Rig rig;
        rig.set(9, 6);
        freezeParity(rig, test.odd);
        rig.seek(0, test.line, 20);
        CHECK_EQ(rig.crtc.ra(), test.line);
        rig.set(8, 3);
        CHECK_EQ(rig.crtc.ra(), test.line);  // not during the line
        rig.nextLine();
        size_t at = 0;
        while (rig.crtc.vcc() == 0) {
            if (at < test.c9.size() && rig.crtc.vlc() == test.c9[at]) {
                CHECK_EQ(rig.crtc.ra(), test.out[at]);
                ++at;
            }
            rig.nextLine();
        }
        CHECK_EQ(at, test.c9.size());
        CHECK_EQ(rig.crtc.vcc(), 1);
        CHECK_EQ(rig.crtc.vlc(), 0);
        CHECK_EQ(rig.crtc.ra(), test.odd ? 1 : 0);
    }

    // Leaving: (parity, C9 of the line of the write, lines row 1 then has).
    struct Leave {
        bool odd;
        int line, rowLines;
    };
    const Leave leaving[] = {
        {false, 0, 7}, {false, 1, 7}, {false, 2, 7},
        {false, 3, 4},  // C9 doubled is 6 = R9: the row ends there
        {true, 0, 7},  {true, 1, 7},  {true, 2, 7},
        {true, 3, 7},  // C9 doubled is 7: not R9, and C9 goes on from 4
    };
    for (const Leave& test : leaving) {
        Rig rig;
        rig.set(9, 6);
        freezeParity(rig, test.odd);
        rig.set(8, 3);
        rig.seek(1, test.line, 20);
        rig.set(8, 0);
        int lines = 0;
        while (rig.crtc.vcc() == 1) {
            rig.nextLine();
            ++lines;
            if (rig.crtc.vcc() == 1)
                CHECK_EQ(rig.crtc.ra(), rig.crtc.vlc());
        }
        CHECK_EQ(lines + test.line, test.rowLines);
    }
}

// Test 1 of Shaker module B. With the parity frozen, it sets R9 and R8 = 3
// on one line of row 6, R8 = 0 on a line 32 or so further down and R9 = 7 on
// the line after, then works out the R4 and R5 that bring the frame back to
// 312 lines. The figures below are what it displays on a real CRTC 0
// (photos at shaker.logonsystem.eu, test B1, CRTC 0), turned into the
// number of lines the frame had lost.
void shakerB1()
{
    struct Series {
        int r9;
        bool odd;
        int entering[8];  // R8 = 3 on C9 = 0..7 of row 6, R8 = 0 on line 80
        int leaving[2];   // R8 = 3 on C9 = 0, R8 = 0 on line 81 and 82
    };
    static const Series series[] = {
        {6, true, {32, 32, 32, 0, 0, 0, 0, 24}, {32, 32}},
        {6, false, {32, 32, 32, 0, 0, 0, 25, 0}, {32, 32}},
        {5, true, {41, 41, 7, 7, 7, 33, 7, 7}, {41, 45}},
        {5, false, {40, 40, 2, 2, 34, 2, 2, 2}, {40, 45}},
    };
    for (const Series& s : series) {
        for (int test = 0; test < 10; ++test) {
            const int enter = test < 8 ? test : 0;
            const int leave = test < 8 ? 0 : test - 7;
            Rig rig;
            freezeParity(rig, s.odd);
            int line = 0;
            do {
                if (line == 48 + enter) {
                    rig.to(18);
                    rig.set(9, s.r9);
                    rig.to(32);
                    rig.set(8, 3);
                } else if (line == 80 + leave) {
                    rig.to(30);
                    rig.set(8, 0);
                } else if (line == 81 + leave) {
                    rig.to(12);
                    rig.set(9, 7);
                }
                rig.nextLine();
                ++line;
            } while (!(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0) && line < 1000);
            const int lost = test < 8 ? s.entering[test] : s.leaving[test - 8];
            if (line != 312 - lost) {
                std::printf("Shaker B1, R9=%d, %s frame, R8=3 on C9=%d, R8=0 on line %d: %d lines, want %d\n", s.r9,
                            s.odd ? "odd" : "even", enter, 80 + leave, line, 312 - lost);
                ++g_failures;
            }
        }
    }
}

}  // namespace

int main()
{
    parity0();
    interlaceSync0();
    interlaceVideo0();
    oddR9();
    midFrame0();
    shakerB1();
    return checkSummary("crtc_interlace");
}
