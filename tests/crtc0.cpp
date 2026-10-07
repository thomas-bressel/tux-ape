// The vertical logic of CRTC 0 (HD6845S / UM6845), register by register,
// as the "Amstrad CPC CRTC Compendium" by Longshot / Logon System describes
// it and as the Logon System "Shaker" tests show it on real machines (tests
// A4, A7, AI and AU of module A).
//
// The chip is driven directly here: a write made after the tick that brings
// character N is a write "during character N".

#include <initializer_list>
#include <string>

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

// What the display enable output does on the characters 62, 63, 0, 1 and 2
// around the end of a line: 'D' for picture, 'B' for border, 'h' for a
// character that is picture then border. R8 is written during the two
// characters given, if any.
std::string aroundLineEnd(Rig& rig, int first = -1, int firstValue = 0, int second = -1, int secondValue = 0)
{
    rig.seek(1, 0, 30);
    std::string seen;
    bool firstDone = first < 0;
    bool secondDone = second < 0;
    for (int i = 0; i < 64 && seen.size() < 5; ++i) {
        const int c0 = rig.crtc.hcc();
        if (!firstDone && c0 == first) {
            rig.set(8, firstValue);
            firstDone = true;
        } else if (firstDone && !secondDone && c0 == second) {
            rig.set(8, secondValue);
            secondDone = true;
        }
        if (c0 >= 62 || (c0 <= 2 && !seen.empty())) {
            const bool a = rig.crtc.displayEnable(0), b = rig.crtc.displayEnable(1);
            seen += a && b ? 'D' : !a && !b ? 'B' : a ? 'h' : '?';
        }
        rig.crtc.tick();
    }
    return seen;
}

// Bits 4 and 5 of R8 delay the display enable output by one or two
// characters, or turn it off (19.2).
void displaySkew()
{
    {
        Rig rig;
        rig.set(1, 59);
        const int expected[4][2] = {{0, 59}, {1, 60}, {2, 61}, {64, 64}};  // first character shown, first not
        for (int skew = 0; skew < 4; ++skew) {
            rig.set(8, skew << 4);
            rig.seek(1, 0);
            rig.seek(2, 0);
            for (int c0 = 0; c0 < 64; ++c0) {
                CHECK_EQ(rig.crtc.hcc(), c0);
                CHECK_EQ(rig.crtc.displayEnable(0), c0 >= expected[skew][0] && c0 < expected[skew][1]);
                CHECK_EQ(rig.crtc.displayEnable(1), rig.crtc.displayEnable(0));
                rig.crtc.tick();
            }
        }
    }
    {
        // A line that never meets R1 still gets half a character of border
        // at its very end (17.6.2); delayed, it is a whole character
        // (19.2.4).
        Rig rig;
        rig.set(1, 64);
        CHECK(aroundLineEnd(rig) == "DhDDD");
        rig.set(8, 0x10);
        CHECK(aroundLineEnd(rig) == "DDBDD");
        rig.set(8, 0x20);
        CHECK(aroundLineEnd(rig) == "DDDBD");
    }
    {
        // With R1 = R0 the last character is border. Setting the delay and
        // taking it away again around that place moves this border, doubles
        // it or makes it vanish: what the eight lines of "R8 stories 1" of
        // Shaker A2 show on a real CRTC 0, where the first write is made
        // during character 57, 58... and the second four characters later.
        static const char* const stories[8] = {
            "DBDDD", "DBDDD", "DBDDD",  // back to no delay in time: nothing changes
            "DDDDD",                    // no border at all
            "DDBDD", "DDBDD", "DDBDD",  // the border comes a character late
            "DBBDD",                    // the border, then the delayed one as well
        };
        for (int story = 0; story < 8; ++story) {
            Rig rig;
            rig.set(1, 63);
            const std::string seen = aroundLineEnd(rig, (57 + story) & 63, 0x10, (61 + story) & 63, 0x00);
            if (seen != stories[story]) {
                std::printf("R8 story %d: %s, want %s\n", story, seen.c_str(), stories[story]);
                ++g_failures;
            }
        }
    }
    {
        // With lines of one character, picture and border alternate, half a
        // character each. A line made longer during what was to be its last
        // character loses that border (Shaker A7).
        Rig rig;
        rig.set(1, 4);
        rig.seek(1, 0, 63);
        rig.crtc.tick();
        rig.set(0, 0);  // during character 0: the line ends with it
        for (int i = 0; i < 5; ++i) {
            CHECK_EQ(rig.crtc.hcc(), 0);
            CHECK(rig.crtc.displayEnable(0));
            CHECK(!rig.crtc.displayEnable(1));
            rig.crtc.tick();
        }
        rig.set(0, 39);
        for (int c0 = 0; c0 < 6; ++c0) {
            CHECK_EQ(rig.crtc.hcc(), c0);
            CHECK_EQ(rig.crtc.displayEnable(0), c0 < 4);
            CHECK_EQ(rig.crtc.displayEnable(1), c0 < 4);
            rig.crtc.tick();
        }
    }
    {
        // The other chips: no border for a line that never meets R1 on types
        // 1, 3 and 4; no delay on types 1 and 2.
        for (const CrtcType type : {CrtcType::UM6845R, CrtcType::MC6845, CrtcType::AsicPlus, CrtcType::PreAsic}) {
            Rig rig(type);
            rig.set(1, 64);
            const bool lateBorder = type == CrtcType::MC6845;
            CHECK(aroundLineEnd(rig) == (lateBorder ? "DhDDD" : "DDDDD"));
            rig.set(1, 63);
            rig.set(8, 0x10);
            const bool skew = type == CrtcType::AsicPlus || type == CrtcType::PreAsic;
            CHECK(aroundLineEnd(rig) == (skew ? "DDBDD" : "DBDDD"));
        }
    }
}

// R1 brought to the character in progress ends the line's display there
// (17.3): R1 = 0 written during character 0 still gives a line of border,
// written during character 1 it comes too late (17.5.1, Shaker "R1
// stories", third story, on a real chip). The ASICs have already made up
// their mind (17.5.2).
void r1WrittenOnItsCharacter()
{
    for (const int c0 : {62, 63, 0, 1}) {
        Rig rig;
        rig.seek(5, 2, c0);
        rig.set(1, 0);
        rig.seek(5, c0 >= 62 ? 3 : 2, 4);
        CHECK_EQ(rig.crtc.displayEnable(), c0 == 1);
        rig.set(1, 40);
        rig.to(20);
        CHECK_EQ(rig.crtc.displayEnable(), c0 == 1);
        rig.nextLine();
        rig.to(4);
        CHECK(rig.crtc.displayEnable());
    }
    {
        Rig rig;
        rig.seek(5, 2, 10);
        rig.set(1, 10);
        CHECK(!rig.crtc.displayEnable());
    }
    {
        Rig rig(CrtcType::AsicPlus);
        rig.seek(5, 2, 0);
        rig.set(1, 0);
        rig.to(4);
        CHECK(rig.crtc.displayEnable());
    }
}

// Two HSYNCs are never joined on type 0: the character one ends on cannot
// start the next (15.3.1; Shaker "R2 update during HSYNC", R2 = #15 written
// during a pulse of 10 begun at #0B, on a real chip). With lines of one
// character there is a pulse every second character (15.3.2).
void hsyncsAreNotJoined()
{
    {
        Rig rig;
        rig.set(2, 11);
        rig.set(3, 10);
        rig.seek(5, 2, 14);
        rig.set(2, 21);
        rig.to(20);
        CHECK(rig.crtc.hsync());
        rig.to(21);
        CHECK(!rig.crtc.hsync());
        rig.to(30);
        CHECK(!rig.crtc.hsync());
    }
    // One character further and there is room for a second pulse.
    {
        Rig rig;
        rig.set(2, 11);
        rig.set(3, 10);
        rig.seek(5, 2, 14);
        rig.set(2, 22);
        rig.to(21);
        CHECK(!rig.crtc.hsync());
        rig.to(22);
        CHECK(rig.crtc.hsync());
        rig.to(31);
        CHECK(rig.crtc.hsync());
        rig.to(32);
        CHECK(!rig.crtc.hsync());
    }
    {
        Rig rig;
        rig.set(3, 1);
        rig.set(2, 0);
        rig.seek(5, 2, 0);
        rig.set(0, 0);
        int pulses = 0;
        bool before = rig.crtc.hsync();
        for (int i = 0; i < 100; ++i) {
            rig.crtc.tick();
            pulses += rig.crtc.hsync() && !before;
            before = rig.crtc.hsync();
        }
        CHECK_EQ(pulses, 50);
    }
}

// R7 made equal to C4 on a line's last character: the VSYNC waits for the
// next character and only comes if C4 is still R7 there. On a row's last
// line the write is therefore too late (Shaker "OUTI story", R7 last
// chance, on a real chip), on any other it gives a VSYNC (Shaker "VSYNC
// conditions", fourth screen).
void r7OnTheLastCharacter()
{
    {
        Rig rig;
        rig.set(7, 127);
        rig.seek(10, 7, 63);
        rig.set(7, 10);
        CHECK(!rig.crtc.vsync());
        rig.crtc.tick();
        CHECK_EQ(rig.crtc.vcc(), 11);
        CHECK(!rig.crtc.vsync());
        CHECK(!rig.vsyncWithin(2));
    }
    {
        Rig rig;
        rig.set(7, 127);
        rig.seek(10, 3, 63);
        rig.set(7, 10);
        CHECK(!rig.crtc.vsync());
        rig.crtc.tick();
        CHECK_EQ(rig.crtc.vcc(), 10);
        CHECK(rig.crtc.vsync());
    }
    // One character sooner it comes at once, as anywhere else on the line.
    {
        Rig rig;
        rig.set(7, 127);
        rig.seek(10, 7, 62);
        rig.set(7, 10);
        CHECK(rig.crtc.vsync());
    }
}

// R0 at 1 and a line that was ending on character 1, the last line of a
// row, when R0 is raised there: the line goes on, but C4 steps at once, on
// character 2, C9 staying where it is (13.7.2; Shaker "Bug OVF C4" on a
// real chip, whose text says "OVF C4 on C0vs=2 when R0 upd (val>1) on
// C0vs=1 when R0=1").
void r0RaisedFromOne()
{
    // Not on the frame's last line: the step is one too many and nothing
    // more; the row ends as rows do (13.7.2.1).
    {
        Rig rig;
        rig.seek(6, 4, 56);
        rig.set(0, 59);
        rig.to(0);
        rig.set(0, 1);
        rig.to(1);
        rig.to(0);
        CHECK(rig.crtc.vcc() == 6 && rig.crtc.vlc() == 6);
        rig.to(1);
        rig.to(0);
        CHECK(rig.crtc.vcc() == 6 && rig.crtc.vlc() == 7);
        rig.to(1);
        rig.set(0, 63);
        CHECK(rig.crtc.vcc() == 6);
        rig.crtc.tick();
        CHECK(rig.crtc.hcc() == 2 && rig.crtc.vcc() == 7 && rig.crtc.vlc() == 7);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 8 && rig.crtc.vlc() == 0);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 8 && rig.crtc.vlc() == 1);
    }
    // On the frame's last line the chip is left adding lines: C9 runs on
    // to 31, R5 being 0, with C4 one past R4, before the frame ends
    // (13.7.2.2). R6 at that C4 brings the border from character 2 on.
    {
        Rig rig;
        rig.seek(38, 4, 56);
        rig.set(4, 0);
        // C4 goes all the way round first; the frame after that one.
        rig.seek(0, 4, 56);
        rig.seek(0, 4, 56);
        rig.set(6, 1);
        rig.set(0, 59);
        rig.to(0);
        rig.set(0, 1);
        rig.to(1);
        rig.to(0);
        rig.to(1);
        rig.to(0);
        CHECK(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 7);
        CHECK(rig.crtc.displayEnable());
        rig.to(1);
        rig.set(0, 63);
        rig.crtc.tick();
        CHECK(rig.crtc.hcc() == 2 && rig.crtc.vcc() == 1 && rig.crtc.vlc() == 7);
        CHECK(!rig.crtc.displayEnable());
        for (int line = 8; line <= 31; ++line) {
            rig.nextLine();
            CHECK_EQ(rig.crtc.vcc(), 1);
            CHECK_EQ(rig.crtc.vlc(), line);
        }
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0);
        rig.to(5);
        CHECK(rig.crtc.displayEnable());
    }
    // Raised on character 0 instead, nothing of the kind: the way to
    // lengthen such a line without C4 moving (13.7.2.2, last words).
    {
        Rig rig;
        rig.seek(6, 4, 56);
        rig.set(0, 59);
        rig.to(0);
        rig.set(0, 1);
        rig.to(1);
        rig.to(0);
        rig.to(1);
        rig.to(0);
        CHECK(rig.crtc.vcc() == 6 && rig.crtc.vlc() == 7);
        rig.set(0, 63);
        rig.to(5);
        CHECK(rig.crtc.vcc() == 6 && rig.crtc.vlc() == 7);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 7 && rig.crtc.vlc() == 0);
    }
}

void lastLineMadeOnCharacterOne()
{
    // C4 = R4 and C9 = R9 make the last line as long as C0 < 2: a write on
    // character 1 is still in time, one on character 2 is not, and C4 then
    // runs past R4 (12.2; Shaker C "last line upd limit" and Shaker E
    // "switch comparator" 12 to 14, both giving a real chip's answers).
    for (int c0 = 0; c0 <= 2; ++c0) {
        Rig rig;
        rig.seek(38, 6, c0);
        rig.set(9, 6);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), c0 < 2 ? 0 : 39);
        CHECK_EQ(rig.crtc.vlc(), 0);
    }
    for (int c0 = 0; c0 <= 2; ++c0) {
        Rig rig;
        rig.seek(20, 7, c0);
        rig.set(4, 20);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), c0 < 2 ? 0 : 21);
        CHECK_EQ(rig.crtc.vlc(), 0);
    }
}

void adjustmentCalledOff()
{
    // The adjustment settled on character 2 of the last line ends as soon
    // as the C9 worked out for the next line is R5 (11.2.2): R5 set to 0
    // later in that line leaves no line to add (Shaker E, "R5 cancelation
    // on R5 upd", a real chip's answers).
    for (int c0 : {3, 61, 63}) {
        Rig rig;
        rig.set(5, 20);
        rig.seek(38, 7, c0);
        rig.set(5, 0);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 0);
        CHECK_EQ(rig.crtc.vlc(), 0);
    }
    // Lowered to 1, one line is added.
    {
        Rig rig;
        rig.set(5, 20);
        rig.seek(38, 7, 30);
        rig.set(5, 1);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 39 && rig.crtc.vlc() == 0);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0);
    }
    // The same goes for the adjustment that R9 starts when it leaves C9 on
    // character 1 of the last line: put back before the line ends, with
    // R5 = 0, the frame ends as if nothing had happened (Shaker E, "switch
    // comparator" 4). Left away, C9 runs on.
    {
        Rig rig;
        rig.seek(38, 7, 1);
        rig.set(9, 6);
        rig.to(60);
        rig.set(9, 7);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0);

        rig.seek(38, 7, 1);
        rig.set(9, 6);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 38 && rig.crtc.vlc() == 8);
    }
}

void r4OnTheLastCharacter()
{
    // R4 taken away from C4 on the last line, the adjustment being settled:
    // C9 is counted against R5 from where it is, 12 lines to go to 20 from
    // 8. On the line's last character the write is too late for that: C4
    // steps, C9 starts again from 0 and the 20 lines are all there (Shaker
    // E, "R5 cancelation on R4 upd", a real chip's answers).
    {
        Rig rig;
        rig.set(5, 20);
        rig.seek(38, 7, 62);
        rig.set(4, 50);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 38 && rig.crtc.vlc() == 8);
        CHECK_EQ(rig.linesToFrameStart(), 12);
    }
    {
        Rig rig;
        rig.set(5, 20);
        rig.seek(38, 7, 63);
        rig.set(4, 50);
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 39 && rig.crtc.vlc() == 0);
        CHECK_EQ(rig.linesToFrameStart(), 20);
    }
}

int main()
{
    displaySkew();
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
    r1WrittenOnItsCharacter();
    hsyncsAreNotJoined();
    r7OnTheLastCharacter();
    r0RaisedFromOne();
    lastLineMadeOnCharacterOne();
    adjustmentCalledOff();
    r4OnTheLastCharacter();
    return checkSummary("crtc0");
}
