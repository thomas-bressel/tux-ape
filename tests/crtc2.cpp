// What sets CRTC 2 (MC6845) apart, as the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System describes it (chapters 11.2.5, 12.4, 15.5.2, 15.6,
// 16.4.3, 17.4.3 and 20.3.3) and as the Logon System "Shaker" shows it on
// real machines (module A: "VSYNC conditions", "R4 & R9 checkings", "R1
// stories", "CRTC 2 offset"; module B: "RLAL" on CRTC 2, "R5 stories";
// module C: "Last line condition").

#include <initializer_list>

#include "check.h"
#include "crtc_rig.h"

namespace {

using namespace tuxape;

bool atFrameStart(const Rig& rig)
{
    return rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0;
}

void plainFrame()
{
    Rig rig(CrtcType::MC6845);
    CHECK_EQ(rig.linesToFrameStart(), 312);
    CHECK_EQ(rig.linesToFrameStart(), 312);
}

// Frames of a single line (12.4.2). R4 and R9 at 0 are not enough: a line
// that the HSYNC found at the frame's end keeps the next one from being the
// last as it begins, and on a frame's first line writes to R9 are not
// looked at before the HSYNC has found the frame's end unmet. Hence R9
// moved away during the HSYNC and back after it.
void oneLineFrames()
{
    // Without the trick C4 runs away on the second line.
    {
        Rig rig(CrtcType::MC6845);
        rig.set(2, 1);
        rig.set(3, 6);  // HSYNC on characters 1 to 6
        rig.seek(38, 7, 10);
        rig.set(4, 0);
        rig.set(9, 0);
        rig.nextLine();
        CHECK(atFrameStart(rig));
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 1);
        CHECK_EQ(rig.crtc.vlc(), 0);
    }
    // With it, on every line or on every second one, each line is a frame.
    for (const int every : {1, 2}) {
        Rig rig(CrtcType::MC6845);
        rig.set(2, 1);
        rig.set(3, 6);
        rig.seek(38, 7, 10);
        rig.set(4, 0);
        rig.set(9, 0);
        rig.nextLine();
        for (int line = 0; line < 20; ++line) {
            CHECK(atFrameStart(rig));
            if (line % every == 0) {
                rig.to(3);
                rig.set(9, 1);
                rig.to(7);
                rig.set(9, 0);
            }
            if (line == 19) {
                // A line found to be the last stays so: R9 raised now
                // only counts from the next line on.
                rig.to(40);
                rig.set(9, 7);
            }
            rig.nextLine();
        }
        CHECK(atFrameStart(rig));
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 0);
        CHECK_EQ(rig.crtc.vlc(), 1);
    }
}

// R4 and R9 brought to C4 and C9 in the middle of a frame make the line
// its last, unless the write that completes the match comes during the
// HSYNC (15.6; "Last line condition": 14 positions out of 64 with the
// firmware's HSYNC). Taking the values away again changes nothing.
void writesDuringTheLine()
{
    for (const int c0 : {10, 45, 46, 59, 60, 63}) {
        Rig rig(CrtcType::MC6845);
        rig.seek(4, 4, 5);
        rig.set(4, 4);
        rig.to(c0);
        rig.set(9, 4);
        rig.set(9, 7);
        rig.set(4, 38);
        rig.nextLine();
        const bool heard = c0 < 46 || c0 > 59;
        CHECK_EQ(atFrameStart(rig), heard);
        if (!heard)
            CHECK_EQ(rig.crtc.vlc(), 5);
    }
    // On a frame's first line such writes are not looked at...
    {
        Rig rig(CrtcType::MC6845);
        rig.seek(0, 0, 2);
        rig.set(4, 0);
        rig.to(12);
        rig.set(9, 0);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 1);
    }
    // ... until an HSYNC has ended with C4 or C9 away from R4 or R9.
    {
        Rig rig(CrtcType::MC6845);
        rig.set(2, 1);
        rig.set(3, 6);
        rig.seek(0, 0, 2);
        rig.set(4, 0);
        rig.to(12);
        rig.set(9, 0);
        rig.nextLine();
        CHECK(atFrameStart(rig));
    }
    // R4 written on a line's first character is in time for the look the
    // chip takes there; R9 is not.
    {
        Rig rig(CrtcType::MC6845);
        rig.seek(38, 7);
        rig.set(4, 10);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 39);
    }
    {
        Rig rig(CrtcType::MC6845);
        rig.seek(38, 7);
        rig.set(9, 10);
        rig.nextLine();
        CHECK(atFrameStart(rig));
    }
}

// An HSYNC that lies on a line's first character (15.5.2, 15.6): the
// border stays, and a line that would have been the frame's last is not.
// One that only reaches the line's last character hides the VSYNC the next
// line brings ("VSYNC conditions", third screen, on a real chip: R2 = 50
// with R3 = 13, 14 and 15).
void hsyncOverLineStart()
{
    for (const int width : {13, 14, 15}) {
        Rig rig(CrtcType::MC6845);
        rig.set(2, 50);
        rig.set(3, width);
        rig.seek(10, 0, 5);
        CHECK_EQ(rig.crtc.displayEnable(), width < 15);
        rig.seek(30, 0, 5);
        CHECK_EQ(rig.crtc.vsync(), width == 13);
        rig.seek(38, 7);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), width < 15 ? 0 : 39);
    }
    // One that starts there hides nothing, but the rest holds.
    {
        Rig rig(CrtcType::MC6845);
        rig.set(2, 0);
        rig.set(3, 6);
        rig.seek(10, 0, 8);
        CHECK(!rig.crtc.displayEnable());
        rig.seek(30, 0, 8);
        CHECK(rig.crtc.vsync());
        rig.seek(38, 7);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 39);
    }
}

// R7 made equal to C4 starts a VSYNC at once, but one that stays inside
// the chip when the write comes during an HSYNC (16.4.3). Seen or not, it
// lasts its 16 lines and keeps another one away meanwhile.
void ghostVsync()
{
    for (const int c0 : {45, 46, 59, 60}) {
        Rig rig(CrtcType::MC6845);
        rig.set(7, 127);
        rig.seek(1, 0, c0);
        rig.set(7, 1);
        const bool hidden = c0 >= 46 && c0 <= 59;
        CHECK_EQ(rig.crtc.vsync(), !hidden);
        rig.seek(2, 0, 10);
        rig.set(7, 127);
        rig.set(7, 2);
        CHECK_EQ(rig.crtc.vsync(), !hidden);
        rig.seek(3, 1, 10);
        CHECK(!rig.crtc.vsync());
        rig.set(7, 127);
        rig.set(7, 3);
        CHECK(rig.crtc.vsync());
    }
}

// During the lines R5 adds, C9 goes on counting up to R9 and C4 with it,
// R4 not being asked (11.2.5): R6 can bring the border there ("R5 stories"
// on a real chip).
void adjustmentLines()
{
    Rig rig(CrtcType::MC6845);
    rig.set(4, 10);
    rig.set(5, 24);
    rig.set(6, 13);
    CHECK_EQ(rig.linesToFrameStart(), 11 * 8 + 24);
    rig.seek(10, 7);
    rig.nextLine();
    CHECK_EQ(rig.crtc.vcc(), 11);
    CHECK_EQ(rig.crtc.vlc(), 0);
    CHECK(rig.crtc.inVerticalAdjust());
    rig.to(5);
    CHECK(rig.crtc.displayEnable());
    rig.seek(12, 7, 5);
    CHECK(rig.crtc.displayEnable());
    rig.seek(13, 0, 5);
    CHECK(rig.crtc.inVerticalAdjust());
    CHECK(!rig.crtc.displayEnable());
    rig.seek(13, 7);
    rig.nextLine();
    CHECK(atFrameStart(rig));
    CHECK(!rig.crtc.inVerticalAdjust());
}

// R1 brought to the character in progress ends the display there as on
// the other chips, but comes too late for the address the next row starts
// from: that was settled with the old R1 (17.4.3; Shaker "R1 stories",
// R1 = 63 on C0 = 63, on a real chip: the same line of text all the way
// down).
void r1WrittenOnItsCharacter()
{
    Rig rig(CrtcType::MC6845);
    rig.set(1, 64);
    rig.seek(5, 7, 63);
    const uint16_t reached = rig.crtc.ma();
    rig.set(1, 63);
    CHECK(!rig.crtc.displayEnable());
    rig.set(1, 64);
    rig.nextLine();
    CHECK(rig.crtc.ma() != reached);
    CHECK_EQ(rig.crtc.ma(), 0);
}

// Where R12/R13 are taken (17.4.3, 20.3.3): as C0 meets R1 on the frame's
// very last line, and nowhere else.
void whereTheOffsetIsTaken()
{
    // Written before that character or during it, the next frame has them;
    // a character later they wait a frame ("CRTC 2 offset" on a real chip:
    // noise from &4000 with the write on C0 = 40, the old screen with it on
    // 41).
    for (const int at : {39, 40, 41}) {
        Rig rig(CrtcType::MC6845);
        rig.seek(38, 7, at);
        rig.set(12, 0x10);
        rig.nextLine();
        CHECK(atFrameStart(rig));
        CHECK_EQ(rig.crtc.ma(), at <= 40 ? 0x1000 : 0);
        rig.seek(38, 7);
        rig.nextLine();
        CHECK_EQ(rig.crtc.ma(), 0x1000);
    }
    // With R1 above R0 they are never taken: every line of every frame
    // starts at the address kept last. That is row 24's if R1 was raised
    // before C0 met it on that row's last line, row 25's if it was raised
    // on that very character, too late ("R1 stories").
    for (const int at : {39, 40}) {
        Rig rig(CrtcType::MC6845);
        rig.seek(24, 7, at);
        rig.set(1, 255);
        rig.to(50);
        rig.set(12, 0x10);
        const uint16_t kept = at == 39 ? 24 * 40 : 25 * 40;
        rig.nextLine();
        CHECK_EQ(rig.crtc.ma(), kept);
        rig.seek(0, 0);
        CHECK_EQ(rig.crtc.ma(), kept);
        rig.seek(10, 3);
        CHECK_EQ(rig.crtc.ma(), kept);
        rig.seek(0, 1);
        CHECK_EQ(rig.crtc.ma(), kept);
    }
    // With lines of R5 to come, the last line keeps the address of the next
    // row as any row's last line does, and the rows go on through those
    // lines; R12/R13 are taken on the last of them. So R12 written on the
    // last line after C0 = R1 is still in time ("R5 stories", whose second
    // picture starts where it should on a real chip).
    {
        Rig rig(CrtcType::MC6845);
        rig.set(4, 10);
        rig.set(5, 24);
        rig.seek(10, 7, 53);
        rig.set(12, 0x30);
        rig.nextLine();
        CHECK(rig.crtc.inVerticalAdjust());
        CHECK_EQ(rig.crtc.ma(), 11 * 40);
        rig.seek(12, 0);
        CHECK_EQ(rig.crtc.ma(), 12 * 40);
        rig.seek(13, 7, 41);
        rig.set(12, 0x20);  // past C0 = R1 on the last of the lines of R5: a frame late
        rig.nextLine();
        CHECK(atFrameStart(rig));
        CHECK_EQ(rig.crtc.ma(), 0x3000);
        rig.seek(13, 7);
        rig.nextLine();
        CHECK_EQ(rig.crtc.ma(), 0x2000);
    }
}

}  // namespace

int main()
{
    plainFrame();
    oneLineFrames();
    writesDuringTheLine();
    hsyncOverLineStart();
    ghostVsync();
    adjustmentLines();
    r1WrittenOnItsCharacter();
    whereTheOffsetIsTaken();
    return checkSummary("crtc2");
}
