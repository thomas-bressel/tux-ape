// The vertical logic of CRTC 0 (HD6845S / UM6845), register by register,
// as the "Amstrad CPC CRTC Compendium" by Longshot / Logon System describes
// it and as the Logon System "Shaker" tests show it on real machines (tests
// A4, A7, AI and AU of module A).
//
// The chip is driven directly here: a write made after the tick that brings
// character N is a write "during character N".

#include "check.h"
#include "crtc_rig.h"

namespace {

using namespace tuxape;

void plainFrame()
{
    Rig rig;
    CHECK_EQ(rig.ticksToFrameStart(), 312 * 64);

    rig.seek(30, 0);
    CHECK(rig.crtc.vsync());  // starts with the row
    int length = 0;
    while (rig.crtc.vsync()) {
        rig.crtc.tick();
        ++length;
    }
    CHECK_EQ(length, 8 * 64);  // R3 = &8E

    // A width of 0 means 16 lines.
    rig.set(3, 0x0E);
    rig.seek(30, 0);
    length = 0;
    while (rig.crtc.vsync()) {
        rig.crtc.tick();
        ++length;
    }
    CHECK_EQ(length, 16 * 64);
}

void verticalAdjustment()
{
    Rig rig;
    rig.set(5, 3);
    rig.seek(0, 0);
    CHECK_EQ(rig.ticksToFrameStart(), 315 * 64);

    // C4 steps past R4 once and C9 does the counting.
    rig.seek(39, 0);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 39);
    CHECK_EQ(rig.crtc.vlc(), 1);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vlc(), 2);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 0);
    CHECK_EQ(rig.crtc.vlc(), 0);

    // R5 is read on the last line no later than its third character.
    rig.set(5, 0);
    rig.seek(38, 7, 2);
    rig.set(5, 1);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 39);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 0);

    rig.set(5, 0);
    rig.seek(38, 7, 3);
    rig.set(5, 1);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 0);  // too late for this frame
    rig.set(5, 0);
}

void lastLineIsDecidedEarly()
{
    // Past the first characters of the last line, R4 and R9 can no longer
    // keep the frame from ending.
    Rig rig;
    rig.seek(38, 7, 10);
    rig.set(4, 20);
    rig.set(9, 3);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 0);
    CHECK_EQ(rig.crtc.vlc(), 0);
}

void r9OnTheLastCharacter()
{
    Rig rig;
    // R9 taken away from C9 on the last character: C4 steps all the same,
    // and C9 with it.
    rig.seek(5, 7, 63);
    rig.set(9, 9);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 6);
    CHECK_EQ(rig.crtc.vlc(), 8);
    rig.set(9, 7);

    // One character earlier, only C9 moves.
    rig.seek(8, 7, 62);
    rig.set(9, 9);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 8);
    CHECK_EQ(rig.crtc.vlc(), 8);
    rig.set(9, 7);

    // R9 brought to C9 on the last character ends the row (Shaker AU).
    rig.seek(12, 3, 63);
    rig.set(9, 3);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 13);
    CHECK_EQ(rig.crtc.vlc(), 0);
    rig.set(9, 7);
}

void lineCarriedOnPastItsEnd()
{
    // Shaker A4: two-character lines, then R0 back to 63. Written on
    // character 1, when the line was about to end with C9 = R9, the C4 step
    // has been decided and survives R9 being raised afterwards.
    for (int writeAt = 0; writeAt < 2; ++writeAt) {
        Rig rig;
        rig.seek(5, 6);
        rig.set(0, 1);
        rig.nextLine();  // C9 = 7, two characters long unless R0 changes
        CHECK_EQ(rig.crtc.vlc(), 7);
        rig.to(writeAt);
        rig.set(0, 63);
        rig.to(15);
        rig.set(9, 9);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), writeAt == 1 ? 6 : 5);
        CHECK_EQ(rig.crtc.vlc(), 8);
    }
}

void vsyncNeedsThreeCharacters()
{
    // The line before the VSYNC row has to reach character 2 (Shaker AI).
    for (int r0 = 0; r0 < 4; ++r0) {
        Rig rig;
        rig.seek(29, 7);
        rig.set(0, r0);
        // Let that line end; with R0 = 0 it is the C4 step left over from
        // the line before that brings row 30.
        rig.crtc.tick();
        while (rig.crtc.vcc() != 30)
            rig.crtc.tick();
        rig.set(0, 63);
        const bool seen = rig.vsyncWithin(20);
        CHECK_EQ(seen, r0 >= 2);
        // The next frame is back to normal.
        rig.seek(30, 0);
        CHECK(rig.crtc.vsync());
    }
}

void vsyncFromR7()
{
    // R7 made equal to C4 starts a VSYNC at once, but not from the first
    // two characters of a line.
    for (int c0 = 0; c0 < 4; ++c0) {
        Rig rig;
        rig.set(7, 127);
        rig.seek(10, 0, c0);
        rig.set(7, 10);
        CHECK_EQ(rig.crtc.vsync(), c0 >= 2);
        if (c0 >= 2) {
            // It lasts the rest of this line and R3's count of whole lines.
            for (int line = 0; line < 8; ++line) {
                rig.nextLine();
                CHECK(rig.crtc.vsync());
            }
            rig.nextLine();
            CHECK(!rig.crtc.vsync());
            continue;
        }
        // The match has been noted: moving R7 away and back within a line
        // brings nothing, on this line or the next.
        rig.to(17);
        rig.set(7, 16);
        rig.to(25);
        rig.set(7, 10);
        CHECK(!rig.crtc.vsync());
        rig.nextLine();
        rig.to(17);
        rig.set(7, 16);
        rig.to(25);
        rig.set(7, 10);
        CHECK(!rig.crtc.vsync());
        // A line that starts with C4 and R7 apart clears the note.
        rig.seek(11, 0, 10);
        CHECK(!rig.crtc.vsync());
        rig.set(7, 11);
        CHECK(rig.crtc.vsync());
    }

    // During a VSYNC, R7 neither stops it nor starts another.
    Rig rig;
    rig.seek(30, 2, 20);
    rig.set(7, 5);
    CHECK(rig.crtc.vsync());
    rig.set(7, 30);
    rig.seek(30, 7, 63);
    CHECK(rig.crtc.vsync());
    rig.seek(31, 0);
    CHECK(!rig.crtc.vsync());
    CHECK(!rig.vsyncWithin(20));
}

void twoCharacterFrames()
{
    // R0 = 1 with R4 = R9 = 0: each two-character "frame" is followed by
    // one adjustment line where C4 is 1 (Compendium 13.2.5).
    Rig rig;
    rig.seek(38, 7, 10);
    rig.set(4, 0);
    rig.set(9, 0);
    rig.nextLine();
    rig.set(0, 1);
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(rig.crtc.vcc(), i & 1);
        CHECK_EQ(rig.crtc.vlc(), 0);
        rig.nextLine();
    }
}

void adjustmentLeftArmed()
{
    // Shaker B, "RVNI LTD": R4 = 0, R9 = 2, lines of two characters. The
    // last line of each "frame" arms an adjustment it cannot settle; the
    // line it gives (C4 = 1) is the only one, even when R0 is raised on it.
    Rig rig;
    rig.seek(38, 7, 10);
    rig.set(4, 0);
    rig.set(9, 2);
    rig.nextLine();
    rig.set(0, 1);
    static const int c4[] = {0, 0, 0, 1, 0, 0, 0, 1};
    static const int c9[] = {0, 1, 2, 0, 0, 1, 2, 0};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(rig.crtc.vcc(), c4[i]);
        CHECK_EQ(rig.crtc.vlc(), c9[i]);
        if (i < 7)
            rig.nextLine();
    }
    rig.set(0, 43);  // on character 0 of the adjustment line
    rig.nextLine();
    CHECK_EQ(rig.crtc.hcc(), 0);
    CHECK_EQ(rig.crtc.vcc(), 0);
    CHECK_EQ(rig.crtc.vlc(), 0);

    // A last line long enough to settle it (R5 = 0) has no such line after.
    rig.nextLine();
    rig.nextLine();
    CHECK_EQ(rig.crtc.vlc(), 2);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 0);
    CHECK_EQ(rig.crtc.vlc(), 0);
}

void hsyncCutByR3()
{
    // Shaker B, "R3 JIT": R3 brought down to the HSYNC counter ends the
    // pulse; how exactly depends on where in the character the write comes.
    using Cut = Crtc::HsyncCut;
    struct {
        int at;      // characters into the pulse
        int width;   // value written to R3's low half
        bool early;  // an OUTI rather than an OUT (C),r
        Cut cut;
    } const cases[] = {
        {0, 0, false, Cut::SecondHalf},
        {1, 1, false, Cut::AfterQuarter},
        {2, 2, false, Cut::AfterQuarter},
        {0, 0, true, Cut::Never},
        {1, 1, true, Cut::AtStart},
        {2, 2, true, Cut::AtStart},
    };
    for (const auto& c : cases) {
        Rig rig;
        rig.set(2, 14);
        rig.set(3, 0x8E);
        rig.seek(5, 3, 14 + c.at);
        CHECK(rig.crtc.hsync());
        rig.crtc.select(3);
        rig.crtc.write(static_cast<uint8_t>(0x80 | c.width), c.early);
        CHECK(!rig.crtc.hsync());
        CHECK(rig.crtc.hsyncCut() == c.cut);
        rig.crtc.tick();
        CHECK(rig.crtc.previousHsyncCut() == c.cut);
        CHECK(rig.crtc.hsyncCut() == Cut::None);
        CHECK(!rig.crtc.hsync());
    }

    // Below the counter, the pulse goes on until the counter comes round.
    Rig rig;
    rig.set(2, 14);
    rig.set(3, 0x8E);
    rig.seek(5, 3, 14 + 5);
    rig.set(3, 0x82);
    for (int i = 5; i < 16 + 2; ++i) {
        CHECK(rig.crtc.hsync());
        rig.crtc.tick();
    }
    CHECK(!rig.crtc.hsync());

    // A width of 0 from the start means no pulse at all on this chip.
    rig.set(3, 0x80);
    rig.seek(5, 5, 14);
    CHECK(!rig.crtc.hsync());
}

void hsyncStartedByR2()
{
    // "R2.JIT": R2 set to the character in progress starts the HSYNC there,
    // for the usual R3 characters (Compendium 14.7; Shaker D, "CSYNC not
    // related to R2.JIT", keeps a stable picture that way).
    Rig rig;
    rig.set(2, 0x7F);  // no HSYNC of its own
    rig.seek(5, 3, 20);
    CHECK(!rig.crtc.hsync());
    rig.set(2, 20);
    CHECK(rig.crtc.hsync());
    rig.set(2, 0x7F);  // too late to stop it
    for (int i = 0; i < 14; ++i) {
        CHECK(rig.crtc.hsync());
        rig.crtc.tick();
    }
    CHECK(!rig.crtc.hsync());

    // Not a second one while the first lasts, and none with a width of 0.
    rig.seek(5, 5, 20);
    rig.set(2, 20);
    rig.crtc.tick();
    rig.crtc.tick();
    rig.set(2, 0x7F);
    rig.set(2, 22);
    for (int i = 2; i < 14; ++i) {
        CHECK(rig.crtc.hsync());
        rig.crtc.tick();
    }
    CHECK(!rig.crtc.hsync());
    rig.set(2, 0x7F);
    rig.set(3, 0x80);
    rig.seek(5, 7, 20);
    rig.set(2, 20);
    CHECK(!rig.crtc.hsync());
}

void oneCharacterLines()
{
    // R0 = 0 freezes C9; C4 steps once if C9 was at R9 (Compendium 13.2.6,
    // Shaker A7).
    Rig rig;
    rig.seek(38, 7, 10);
    rig.set(4, 0);
    rig.set(9, 0);
    rig.nextLine();  // first line of a frame, C4 = C9 = 0
    rig.set(0, 0);
    CHECK_EQ(rig.crtc.vcc(), 0);
    rig.crtc.tick();
    CHECK_EQ(rig.crtc.vcc(), 1);
    CHECK_EQ(rig.crtc.vlc(), 0);
    for (int i = 0; i < 20; ++i) {
        rig.crtc.tick();
        CHECK_EQ(rig.crtc.hcc(), 0);
        CHECK_EQ(rig.crtc.vcc(), 1);
        CHECK_EQ(rig.crtc.vlc(), 0);
    }

    // Back to longer lines, the adjustment that was left armed runs: C9
    // counts round to R5, with C4 where it is.
    rig.set(0, 5);
    for (int line = 1; line < 32; ++line) {
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 1);
        CHECK_EQ(rig.crtc.vlc(), line);
    }
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 0);
    CHECK_EQ(rig.crtc.vlc(), 0);

    // Away from R9, nothing moves at all.
    Rig other;
    other.seek(5, 3);
    other.set(0, 0);
    for (int i = 0; i < 20; ++i) {
        other.crtc.tick();
        CHECK_EQ(other.crtc.vcc(), 5);
        CHECK_EQ(other.crtc.vlc(), 3);
    }
}

}  // namespace

int main()
{
    plainFrame();
    verticalAdjustment();
    lastLineIsDecidedEarly();
    r9OnTheLastCharacter();
    lineCarriedOnPastItsEnd();
    vsyncNeedsThreeCharacters();
    vsyncFromR7();
    twoCharacterFrames();
    adjustmentLeftArmed();
    hsyncCutByR3();
    hsyncStartedByR2();
    oneCharacterLines();
    return checkSummary("crtc0");
}
