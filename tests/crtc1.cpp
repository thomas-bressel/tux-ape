// What sets CRTC 1 (UM6845R) apart, as the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System describes it and as the Logon System "Shaker"
// shows it on real machines (module A, tests "R1 stories" and "VSYNC
// conditions").

#include <initializer_list>

#include "check.h"
#include "core/cpc.h"
#include "crtc_rig.h"

namespace {

using namespace tuxape;

// The address a line starts at: the refresh address on its first character.
int lineAddress(Rig& rig, int c4, int c9)
{
    rig.seek(c4, c9);
    return rig.crtc.ma();
}

// On its first row the UM6845R reads R12/R13 for every line. The address
// kept for the rows that follow only moves when C0 meets R1 on the last
// line of a row (17.4.2).
void videoPointer()
{
    Rig rig(CrtcType::UM6845R);
    rig.set(12, 0x30);
    rig.set(13, 0x00);
    CHECK_EQ(lineAddress(rig, 0, 0), 0x3000);
    CHECK_EQ(lineAddress(rig, 1, 0), 0x3000 + 40);
    CHECK_EQ(lineAddress(rig, 24, 7), 0x3000 + 24 * 40);

    // R12/R13 changed in the middle of the first row show from the next
    // line; on the other types they wait for the next frame.
    rig.seek(0, 3, 10);
    rig.set(13, 0x10);
    rig.nextLine();
    CHECK_EQ(rig.crtc.ma(), 0x3010);
    CHECK_EQ(lineAddress(rig, 1, 0), 0x3010 + 40);
    {
        Rig other(CrtcType::MC6845);
        other.set(12, 0x30);
        other.seek(0, 0);
        other.seek(0, 3, 10);
        other.set(13, 0x10);
        other.nextLine();
        CHECK_EQ(other.crtc.ma(), 0x3000);
        CHECK_EQ(lineAddress(other, 0, 0), 0x3010);
    }

    // With R1 out of C0's reach nothing is kept any more: the first row
    // shows R12/R13, every other row the address kept last ("R1 stories",
    // first screen: R1 raised on the last line of row 24, before C0 = R1).
    rig.set(13, 0x00);
    rig.seek(0, 0);
    rig.seek(24, 7, 39);
    rig.set(1, 127);
    const int kept = 0x3000 + 24 * 40;  // what row 23 left
    CHECK_EQ(lineAddress(rig, 25, 0), kept);
    CHECK_EQ(lineAddress(rig, 38, 7), kept);
    CHECK_EQ(lineAddress(rig, 0, 0), 0x3000);
    CHECK_EQ(lineAddress(rig, 0, 7), 0x3000);
    CHECK_EQ(lineAddress(rig, 1, 0), kept);
    CHECK_EQ(lineAddress(rig, 30, 0), kept);
    // Raised after C0 = R1, that line's address has been kept.
    rig.set(1, 40);
    rig.seek(0, 0);
    rig.seek(24, 7, 41);
    rig.set(1, 127);
    CHECK_EQ(lineAddress(rig, 25, 0), 0x3000 + 25 * 40);
    CHECK_EQ(lineAddress(rig, 0, 0), 0x3000);
    CHECK_EQ(lineAddress(rig, 1, 0), 0x3000 + 25 * 40);
    // Raised on C0 = R1 itself, it has not: the chip keeps the address as
    // that character ends, if R1 is still there ("R1 stories", second
    // screen: 80 x 24 on a real chip, not 80 x 25).
    rig.set(1, 40);
    rig.seek(0, 0);
    rig.seek(24, 7, 40);
    rig.set(1, 127);
    CHECK_EQ(lineAddress(rig, 25, 0), kept);
    CHECK_EQ(lineAddress(rig, 1, 0), kept);
}

// Lines from here to the moment C4 next has the value given.
int linesTo(Rig& rig, int c4)
{
    int ticks = 0;
    while (rig.crtc.vcc() != c4 && ticks < 400000) {
        rig.crtc.tick();
        ++ticks;
    }
    return (ticks + 63) / 64;
}

// The lines of R5 (11.2.3, 11.2.4, 11.3.2). The figures are those of the
// Shaker's module E, "R5 stories 2", which prints beside each what a real
// chip gives; it counts in quarters of a line.
void linesOfR5()
{
    // Plainly: 39 rows of 8 lines, then 20 lines more, during which C4
    // and C9 go on counting.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(5, 20);
        rig.seek(38, 7, 63);
        CHECK_EQ(linesTo(rig, 39), 1);
        CHECK(rig.crtc.inVerticalAdjust());
        CHECK_EQ(linesTo(rig, 41), 16);
        CHECK_EQ(linesTo(rig, 0), 4);
        CHECK(!rig.crtc.inVerticalAdjust());
    }
    // The frame's end is noted as the last line's last character begins.
    // R4 or R9 moved away before that put it off: the frame goes on to the
    // new R4 (12 rows and the 20 lines), or the row on to the new R9 (5
    // lines and the 20).
    for (const int c0 : {61, 62}) {
        Rig rig(CrtcType::UM6845R);
        rig.set(5, 20);
        rig.seek(38, 7, c0);
        rig.set(4, 50);
        CHECK_EQ(linesTo(rig, 0), 1 + 12 * 8 + 20);
        rig.set(4, 38);
        rig.seek(38, 7, c0);
        rig.set(9, 12);
        CHECK_EQ(linesTo(rig, 39), 1 + 5);
        CHECK_EQ(linesTo(rig, 0), 20);
    }
    // Moved during that last character, they are too late: the 20 lines
    // follow at once, counted by the new values. C4 still reaches 39, on
    // the next line with R4 changed, five lines on with R9 changed.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(5, 20);
        rig.seek(38, 7, 63);
        rig.set(4, 50);
        CHECK_EQ(linesTo(rig, 39), 1);
        CHECK_EQ(linesTo(rig, 0), 20);
        rig.set(4, 38);
        rig.seek(38, 7, 63);
        rig.set(9, 12);
        CHECK_EQ(linesTo(rig, 39), 1 + 5);
        CHECK_EQ(linesTo(rig, 0), 15);
    }
    // R5 itself is looked at as the line ends: brought to 0 on the last
    // character, there are no lines of R5.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(5, 20);
        rig.seek(38, 7, 63);
        rig.set(5, 0);
        CHECK_EQ(linesTo(rig, 0), 1);
        CHECK(!rig.crtc.inVerticalAdjust());
    }
    // Brought to 0 during its lines, it does not end them: C5 goes round,
    // and the frame ends on the line where R5 is given its number...
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(5, 20);
        rig.seek(39, 3, 10);  // C5 = 3
        rig.set(5, 0);
        rig.seek(42, 0, 10);  // 21 lines on: C5 = 24, C4 past 41
        CHECK(rig.crtc.inVerticalAdjust());
        rig.set(5, 27);       // the line of C5 = 26 is the last
        CHECK_EQ(linesTo(rig, 0), 3);
        CHECK(!rig.crtc.inVerticalAdjust());
    }
    // ...or, R5 staying at 0, when C4 meets R4 again after going all the
    // way round.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(5, 20);
        rig.seek(39, 1, 10);
        rig.set(5, 0);
        CHECK_EQ(linesTo(rig, 0), 7 + (127 - 39) * 8);
        CHECK(rig.crtc.inVerticalAdjust());
        CHECK_EQ(linesTo(rig, 38), 38 * 8);
        rig.seek(38, 7, 63);
        CHECK_EQ(linesTo(rig, 0), 1);
        CHECK(!rig.crtc.inVerticalAdjust());
    }
}

// With R4 = 0 the row that follows the frame's only one is the first of
// the lines of R5, and it too starts every line at R12/R13; the next ones
// start at the address kept (11.2.4, 17.4.2; Shaker "UPD OFF ADD LINE" and
// module E, "VMA update on spec adj", on a real chip).
void addressInLinesOfR5()
{
    const auto frame = [](Rig& rig) {
        rig.set(12, 0x30);
        rig.set(13, 0x00);
        rig.seek(0, 0, 10);
        rig.set(4, 0);
        rig.set(5, 16);
    };
    {
        Rig rig(CrtcType::UM6845R);
        frame(rig);
        CHECK_EQ(lineAddress(rig, 0, 7), 0x3000);
        CHECK_EQ(lineAddress(rig, 1, 0), 0x3000);
        rig.set(13, 0xC8);
        CHECK_EQ(lineAddress(rig, 1, 1), 0x30C8);
        CHECK_EQ(lineAddress(rig, 1, 7), 0x30C8);
        // The second row of those lines: what the first one left.
        rig.set(13, 0x28);
        CHECK_EQ(lineAddress(rig, 2, 0), 0x30C8 + 40);
        CHECK_EQ(lineAddress(rig, 2, 7), 0x30C8 + 40);
        CHECK_EQ(lineAddress(rig, 0, 0), 0x3028);
    }
    // R4 raised on the last character of the frame's last line: the lines
    // of R5 follow all the same, but C4 = 1 is then a row like any other.
    {
        Rig rig(CrtcType::UM6845R);
        frame(rig);
        rig.seek(0, 7, 63);
        rig.set(4, 1);
        CHECK_EQ(lineAddress(rig, 1, 0), 0x3000 + 40);
        CHECK(rig.crtc.inVerticalAdjust());
        rig.set(13, 0xC8);
        CHECK_EQ(lineAddress(rig, 1, 5), 0x3000 + 40);
    }
    // R9 raised there: the row goes on, with C4 = 0, and C4 = 1 still
    // starts its lines at R12/R13.
    {
        Rig rig(CrtcType::UM6845R);
        frame(rig);
        rig.seek(0, 7, 63);
        rig.set(9, 15);
        CHECK_EQ(lineAddress(rig, 0, 8), 0x3000);
        CHECK(rig.crtc.inVerticalAdjust());
        rig.set(9, 7);
        rig.seek(1, 0, 10);
        rig.set(13, 0xC8);
        CHECK_EQ(lineAddress(rig, 1, 1), 0x30C8);
    }
}

// The VSYNC lasts 16 lines whatever R3 says. Lines of a single character
// (R0 = 0) do not count among them on this chip, though C9 and C4 go on
// counting; the MC6845 counts them like any other ("VSYNC conditions",
// first screen, on real chips of both kinds).
void vsyncAndShortLines()
{
    for (const CrtcType type : {CrtcType::UM6845R, CrtcType::MC6845}) {
        Rig rig(type);
        rig.seek(30, 0, 5);
        CHECK(rig.crtc.vsync());
        int lines = 0;
        while (rig.crtc.vsync()) {
            rig.nextLine();
            ++lines;
        }
        CHECK_EQ(lines, 16);

        // The Shaker's sequence: R0 = 0 on character 0 of the last line of
        // row 29, eighteen characters of it, then lines of 46 characters.
        rig.seek(29, 7);
        rig.set(0, 0);
        rig.crtc.tick();
        CHECK_EQ(rig.crtc.vcc(), 30);
        CHECK(rig.crtc.vsync());
        for (int i = 0; i < 17; ++i)
            rig.crtc.tick();
        CHECK_EQ(rig.crtc.vcc(), 32);  // 18 lines went by
        CHECK_EQ(rig.crtc.vlc(), 1);
        rig.set(0, 45);
        const bool stillUp = type == CrtcType::UM6845R;
        CHECK_EQ(rig.crtc.vsync(), stillUp);
        for (int i = 0; i < 46; ++i)
            rig.crtc.tick();
        CHECK_EQ(rig.crtc.vsync(), stillUp);
        if (stillUp) {
            // ... and it then runs its 16 lines.
            lines = 0;
            while (rig.crtc.vsync()) {
                rig.nextLine();
                ++lines;
            }
            CHECK_EQ(lines, 15);
        }
    }
}

// An OUTI's write comes a quarter of a microsecond sooner than an OUT
// (C),r's. Falling on the character after a line's last one, it is too late
// on the other chips: the line has ended. This one makes up its mind about
// C0 a little way into the character, and R0 raised there keeps the line
// going (13.3, 13.6.2, 13.7.1.1; Shaker "OUTI story" on a real chip).
void earlyWriteToR0()
{
    for (const CrtcType type : {CrtcType::UM6845R, CrtcType::HD6845S, CrtcType::MC6845}) {
        Cpc cpc;
        cpc.crtc().setType(type);
        static const uint8_t screen[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7};
        for (int r = 0; r < 10; ++r) {
            cpc.out(0xBC00, static_cast<uint8_t>(r));
            cpc.out(0xBD00, screen[r]);
        }
        cpc.out(0xBC00, 0);
        while (cpc.clock() & 3)
            cpc.tick(1);
        while (cpc.crtc().hcc() != 63)
            cpc.tick(4);
        cpc.tick(3);  // the I/O cycle begins a T-state before the next character
        cpc.out(0xBD00, 127);
        const int after = cpc.crtc().hcc();
        if (type == CrtcType::UM6845R)
            CHECK(after >= 64 && after <= 66);
        else
            CHECK(after <= 2);
    }
    // The same write a T-state later, as an OUT (C),r makes it: too late
    // on this chip as well.
    Cpc cpc;
    cpc.crtc().setType(CrtcType::UM6845R);
    cpc.out(0xBC00, 0);
    cpc.out(0xBD00, 63);
    while (cpc.clock() & 3)
        cpc.tick(1);
    while (cpc.crtc().hcc() != 63)
        cpc.tick(4);
    cpc.tick(4);
    cpc.out(0xBD00, 127);
    CHECK(cpc.crtc().hcc() <= 2);
}

// Leaves the chip at the start of a frame of the parity asked for.
void toFrame(Rig& rig, bool odd)
{
    rig.seek(0, 0);
    while (rig.crtc.vcc() != 0 || rig.crtc.vlc() > 1 || rig.crtc.hcc() != 0 || rig.crtc.oddFrame() != odd)
        rig.crtc.tick();
}

// Lines of the frame in progress, from its first character.
int frameLines(Rig& rig)
{
    int lines = 0;
    int row = rig.crtc.vcc();
    while (lines < 2000) {
        rig.nextLine();
        ++lines;
        if (rig.crtc.vcc() == 0 && row != 0)
            break;
        row = rig.crtc.vcc();
    }
    return lines;
}

// Interlace on type 1 (19.5.3, 19.6.2, 19.7.2, 19.8.2; what the Shaker's
// "interlace C4/C9 counters", "IVM delays" and "interlace VSYNC nightmare"
// measure, each figure of which is now the real chip's).
void interlace()
{
    // The parity of the frame turns over with every frame, whatever R8
    // holds.
    {
        Rig rig(CrtcType::UM6845R);
        rig.seek(0, 0);
        const bool odd = rig.crtc.oddFrame();
        CHECK_EQ(frameLines(rig), 312);
        CHECK(rig.crtc.oddFrame() != odd);
        CHECK_EQ(frameLines(rig), 312);
        CHECK(rig.crtc.oddFrame() == odd);
    }
    // Either interlace mode: an even frame has one line more, counted as
    // one more line of R5, and its VSYNC waits for the middle of the line.
    for (const int r5 : {0, 3}) {
        Rig rig(CrtcType::UM6845R);
        rig.set(8, 1);
        rig.set(5, r5);
        toFrame(rig, false);
        rig.seek(30, 0);
        CHECK(!rig.crtc.vsync());
        rig.to(31);
        CHECK(rig.crtc.vsync());
        toFrame(rig, false);
        CHECK_EQ(frameLines(rig), 313 + r5);
        CHECK(rig.crtc.oddFrame());
        CHECK_EQ(frameLines(rig), 312 + r5);
        // An odd frame's VSYNC starts with its row.
        toFrame(rig, true);
        rig.seek(30, 0);
        CHECK(rig.crtc.vsync());
    }
    // "Sync & video" with R9 = 7: rows of four lines, the even ones on an
    // even frame, the odd ones on an odd frame.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(4, 77);
        rig.set(6, 50);
        rig.set(7, 60);
        rig.set(8, 3);
        for (const bool odd : {false, true}) {
            toFrame(rig, odd);
            for (int line = 0; line < 12; ++line) {
                CHECK_EQ(rig.crtc.vcc(), line / 4);
                CHECK_EQ(rig.crtc.vlc(), line % 4 * 2 + (odd ? 1 : 0));
                rig.nextLine();
            }
            toFrame(rig, odd);
            CHECK_EQ(frameLines(rig), odd ? 312 : 313);
        }
    }
    // With R9 = 8 a row has nine lines to share between two frames: rows
    // of five even lines and rows of four odd lines follow one another,
    // an even frame starting with the five and an odd one with the four.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(9, 8);
        rig.set(8, 3);
        static const int kEven[] = {0, 2, 4, 6, 8, 1, 3, 5, 7, 0, 2, 4, 6, 8, 1};
        static const int kOdd[] = {1, 3, 5, 7, 0, 2, 4, 6, 8, 1, 3, 5, 7, 0, 2};
        for (const bool odd : {false, true}) {
            toFrame(rig, odd);
            for (int line = 0; line < 15; ++line) {
                CHECK_EQ(rig.crtc.vlc(), (odd ? kOdd : kEven)[line]);
                rig.nextLine();
            }
        }
    }
}

// "Sync & video" coming on and going off in the middle of a line, as the
// Shaker's thirty "IVM on/off" tests have it (19.5.3, the sixteen diagrams).
// C9's lowest bit and the two parities are worked out in two steps, on the
// character of the write and on the next one.
void interlaceOnOff()
{
    struct Case {
        bool oddFrame, oddR9, oddRow, oddLine;
        int on, onNext, off;       // C9's lowest bit after each step
        bool frameOn, frameOff;    // the frame is odd once the mode is on, and once it is off again
    };
    static const Case kCases[] = {
        {false, false, false, false, 0, 0, 0, false, false},
        {false, false, false, true, 1, 0, 0, false, false},
        {false, false, true, false, 1, 1, 0, false, false},
        {false, false, true, true, 0, 1, 0, false, false},
        {false, true, false, false, 0, 0, 0, false, false},
        {false, true, false, true, 1, 0, 0, false, false},
        {false, true, true, false, 0, 0, 0, false, false},
        {false, true, true, true, 1, 0, 0, false, false},
        {true, false, false, false, 0, 0, 0, false, false},
        {true, false, false, true, 1, 1, 1, true, true},
        {true, false, true, false, 1, 1, 0, false, false},
        {true, false, true, true, 0, 0, 1, true, true},
        {true, true, false, false, 0, 0, 0, false, false},
        {true, true, false, true, 1, 1, 1, true, true},
        {true, true, true, false, 0, 0, 0, false, false},
        {true, true, true, true, 1, 1, 1, true, true},
    };
    for (const Case& c : kCases) {
        Rig rig(CrtcType::UM6845R);
        rig.set(9, c.oddR9 ? 7 : 6);
        toFrame(rig, c.oddFrame);
        rig.seek(c.oddRow ? 3 : 2, c.oddLine ? 3 : 2, 5);
        rig.set(8, 3);
        CHECK_EQ(rig.crtc.vlc() & 1, c.on);
        rig.crtc.tick();
        CHECK_EQ(rig.crtc.vlc() & 1, c.onNext);
        CHECK(rig.crtc.oddFrame() == c.frameOn);
        rig.to(9);
        rig.set(8, 0);
        CHECK_EQ(rig.crtc.vlc() & 1, c.off);
        rig.crtc.tick();
        CHECK(rig.crtc.oddFrame() == c.frameOff);
    }
}

// The "rupture for dummies" (11.6, 13.7.1.2): R5 brought from 0 to another
// value during a line's last character makes every line from there start
// at R12/R13, as on a frame's first row, until a row ends on lines of odd
// parity. The parity turning over with each frame, that is the end of the
// row on one frame and never on the next (what the Shaker's "CRTC 1
// identifier", "RFD R5 other tests" and "RFD round 2" show on a real chip).
void ruptureForDummies()
{
    for (const bool odd : {true, false}) {
        Rig rig(CrtcType::UM6845R);
        rig.set(12, 0x30);
        rig.set(13, 0x00);
        toFrame(rig, odd);
        rig.seek(5, 2, 63);
        rig.set(5, 1);
        rig.nextLine();
        rig.set(5, 0);
        // The rest of the row, at R12/R13 as it is at each line's start.
        CHECK_EQ(rig.crtc.ma(), 0x3000);
        rig.set(13, 0x10);
        CHECK_EQ(lineAddress(rig, 5, 7), 0x3010);
        if (odd) {
            // The row ends, the address it reached is kept, and the rows
            // that follow are rows like any other again.
            CHECK_EQ(lineAddress(rig, 6, 0), 0x3010 + 40);
            CHECK_EQ(lineAddress(rig, 7, 0), 0x3010 + 80);
        } else {
            CHECK_EQ(lineAddress(rig, 6, 0), 0x3010);
            CHECK_EQ(lineAddress(rig, 20, 5), 0x3010);
            CHECK_EQ(lineAddress(rig, 38, 7), 0x3010);
        }
        // The next frame is an ordinary one.
        CHECK_EQ(lineAddress(rig, 0, 0), 0x3010);
        CHECK_EQ(lineAddress(rig, 1, 0), 0x3010 + 40);
        CHECK_EQ(lineAddress(rig, 2, 0), 0x3010 + 80);
    }
    // Not from another character of the line, nor from a value that was
    // not 0.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(12, 0x30);
        toFrame(rig, false);
        rig.seek(5, 2, 62);
        rig.set(5, 1);
        rig.set(5, 0);
        CHECK_EQ(lineAddress(rig, 5, 3), 0x3000 + 5 * 40);
        rig.set(5, 2);
        rig.seek(6, 2, 63);
        rig.set(5, 1);
        CHECK_EQ(lineAddress(rig, 6, 3), 0x3000 + 6 * 40);
    }
    // On a row's last line it calls the thing off instead.
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(12, 0x30);
        toFrame(rig, false);
        rig.seek(5, 2, 63);
        rig.set(5, 1);
        rig.nextLine();
        rig.set(5, 0);
        CHECK_EQ(lineAddress(rig, 9, 0), 0x3000);
        rig.seek(9, 7, 63);
        rig.set(5, 1);
        rig.nextLine();
        rig.set(5, 0);
        // Nothing has been kept since row 4 ended; from here rows are
        // rows like any other again.
        CHECK_EQ(rig.crtc.ma(), 0x3000 + 5 * 40);
        CHECK_EQ(lineAddress(rig, 11, 0), 0x3000 + 6 * 40);
    }
    // Pinning the parity by switching "interlace sync & video" on and off
    // on an even line makes it even: done before, every frame has its
    // lines at R12/R13 to the end; done after the row has ended, none.
    for (const bool before : {true, false}) {
        Rig rig(CrtcType::UM6845R);
        rig.set(12, 0x30);
        for (int frame = 0; frame < 3; ++frame) {
            rig.seek(0, 0, 20);
            if (before) {
                rig.set(8, 3);
                rig.to(24);
                rig.set(8, 0);
            }
            rig.seek(5, 2, 63);
            rig.set(5, 1);
            rig.nextLine();
            rig.set(5, 0);
            if (!before) {
                rig.seek(6, 0, 20);
                rig.set(8, 3);
                rig.to(24);
                rig.set(8, 0);
            }
            if (frame > 0) {
                CHECK_EQ(lineAddress(rig, 6, 4), before ? 0x3000 : 0x3000 + 40);
                CHECK_EQ(lineAddress(rig, 30, 0), before ? 0x3000 : 0x3000 + 25 * 40);
            }
        }
    }
    // The other way in (13.7.1.2): the frame's end is noted as the last
    // character of its last line begins; R0 raised on that character keeps
    // the line going, and R9 or R4 moved away before it ends means that no
    // frame ends there. The address logic has been told one did.
    for (const bool odd : {true, false}) {
        Rig rig(CrtcType::UM6845R);
        rig.set(12, 0x30);
        toFrame(rig, odd);
        rig.set(4, 7);
        rig.seek(7, 7, 63);
        rig.set(0, 127);
        rig.to(88);
        rig.set(4, 37);
        rig.to(102);
        rig.set(9, 14);
        rig.nextLine();
        rig.set(0, 63);
        // The row goes on to line 14, its lines at R12/R13.
        CHECK_EQ(rig.crtc.vcc(), 7);
        CHECK_EQ(rig.crtc.vlc(), 8);
        CHECK_EQ(rig.crtc.ma(), 0x3000);
        CHECK_EQ(lineAddress(rig, 7, 14), 0x3000);
        rig.seek(8, 0, 12);
        rig.set(9, 7);
        CHECK_EQ(lineAddress(rig, 8, 4), odd ? 0x3000 + 40 : 0x3000);
        CHECK_EQ(lineAddress(rig, 20, 0), odd ? 0x3000 + 13 * 40 : 0x3000);
    }
    // With R4 alone moved away the row ends with that line: on an odd
    // frame that is the end of it at once, and nothing shows.
    for (const bool odd : {true, false}) {
        Rig rig(CrtcType::UM6845R);
        rig.set(12, 0x30);
        toFrame(rig, odd);
        rig.set(4, 7);
        rig.seek(7, 7, 63);
        rig.set(0, 127);
        rig.to(102);
        rig.set(4, 38);
        rig.nextLine();
        rig.set(0, 63);
        CHECK_EQ(rig.crtc.vcc(), 8);
        CHECK_EQ(rig.crtc.ma(), odd ? 0x3000 + 8 * 40 : 0x3000);
        CHECK_EQ(lineAddress(rig, 20, 0), odd ? 0x3000 + 20 * 40 : 0x3000);
    }
}

// A VSYNC starts when C4 meets R7, and not again while they stay together
// (16.3; Shaker "VSYNC torture" on a real chip: with R4 = R7 = 0 a VSYNC of
// 16 lines and no other, where the chips in the ASICs never come out of
// it).
void vsyncOnce()
{
    Rig rig(CrtcType::UM6845R);
    rig.seek(31, 0, 10);
    CHECK(rig.crtc.vsync());
    rig.seek(33, 0, 10);
    CHECK(!rig.crtc.vsync());
    // Frames of a single row, with R7 on it.
    rig.seek(0, 0, 10);
    rig.set(4, 0);
    rig.set(7, 0);
    CHECK(rig.crtc.vsync());
    int lines = 0;
    while (rig.crtc.vsync() && lines < 100) {
        rig.nextLine();
        ++lines;
    }
    CHECK_EQ(lines, 16);
    CHECK(!rig.vsyncWithin(100));
    // C4 leaving R7 and coming back is a new meeting: with two rows a
    // frame, the VSYNC that ends as C4 comes back to 0 starts again at
    // once.
    rig.set(4, 1);
    rig.seek(1, 0, 10);
    rig.seek(0, 0, 10);
    CHECK(rig.crtc.vsync());
    bool gap = false;
    for (int line = 0; line < 100; ++line) {
        rig.nextLine();
        gap = gap || !rig.crtc.vsync();
    }
    CHECK(!gap);
    // R7 taken away and brought back is one too, once the VSYNC is over;
    // during a VSYNC it is not looked at.
    rig.set(4, 38);
    rig.set(7, 30);
    rig.seek(10, 0, 10);
    CHECK(!rig.crtc.vsync());
    rig.set(7, 10);
    CHECK(rig.crtc.vsync());
    rig.set(7, 30);
    rig.set(7, 10);
    rig.seek(12, 0, 10);
    CHECK(!rig.crtc.vsync());
    rig.set(7, 12);
    CHECK(rig.crtc.vsync());
}

}  // namespace

int main()
{
    videoPointer();
    vsyncOnce();
    interlace();
    interlaceOnOff();
    ruptureForDummies();
    earlyWriteToR0();
    linesOfR5();
    addressInLinesOfR5();
    vsyncAndShortLines();
    return checkSummary("crtc1");
}
