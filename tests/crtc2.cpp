// What sets CRTC 2 (MC6845) apart, as the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System describes it (chapters 11.2.5, 12.4, 15.5.2, 15.6,
// 16.4.3, 17.4.3, 19.4.3, 19.5.4, 19.6.3, 19.7.2, 19.8.3 and 20.3.3) and as
// the Logon System "Shaker" shows it on real machines (module A: "VSYNC
// conditions", "R4 & R9 checkings", "R1 stories", "CRTC 2 offset"; module
// B: "RLAL" on CRTC 2, "R5 stories", "Interlace C4/C9 counters"; module C:
// "Last line condition", "Add line on parity bug", "Add line request &
// trigger").

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

// Moves on to the start of a frame of the parity wanted. Frames take turns
// whatever R8 holds, as long as C4 meets R6 (19.5.4).
void toFrame(Rig& rig, bool odd)
{
    do
        rig.seek(0, 0);
    while (rig.crtc.oddFrame() != odd);
}

// "Interlace sync & video" (19.4.3, 19.8.3). C9 and C4 count as ever, so
// nothing else has to be set again; what is put out is another counter,
// which starts anew half-way down the row, doubled, with the frame's parity
// below it. It is kept all the time and put out from the very character R8
// is written on.
void interlaceVideoLines()
{
    // The Compendium's tables: R9 = 7, the mode set on line 0 to 5 of row
    // 0. Left column: what the lines that follow put out on an even frame.
    static const int expected[6][8] = {
        {0, 2, 4, 6, 0, 2, 4, 6}, {0, 1, 4, 6, 0, 2, 4, 6}, {0, 1, 2, 6, 0, 2, 4, 6},
        {0, 1, 2, 3, 0, 2, 4, 6}, {0, 1, 2, 3, 4, 2, 4, 6}, {0, 1, 2, 3, 4, 5, 4, 6},
    };
    for (const bool odd : {false, true}) {
        for (int at = 0; at < 6; ++at) {
            Rig rig(CrtcType::MC6845);
            toFrame(rig, odd);
            // Away from the frame's first line, which has ways of its own.
            rig.seek(2, 0);
            for (int line = 0; line < 8; ++line) {
                const int out = rig.crtc.ra();
                const bool doubled = line > at;
                CHECK_EQ(out, doubled ? expected[at][line] | odd : expected[at][line]);
                if (line == at) {
                    rig.to(20);
                    rig.set(8, 3);
                    // From this character on.
                    const int half = line > 3 ? line - 4 : line;
                    CHECK_EQ(rig.crtc.ra(), half * 2 | odd);
                }
                rig.nextLine();
                CHECK_EQ(rig.crtc.vlc(), (line + 1) & 7);
            }
            // Row 3: C4 has moved as ever, and the lines go two by two.
            CHECK_EQ(rig.crtc.vcc(), 3);
            for (int line = 0; line < 8; ++line) {
                CHECK_EQ(rig.crtc.ra(), (line & 3) * 2 | odd);
                rig.nextLine();
            }
            // The mode left: C9 itself again, at once.
            rig.seek(5, 6, 10);
            CHECK_EQ(rig.crtc.ra(), 4 | odd);
            rig.set(8, 0);
            CHECK_EQ(rig.crtc.ra(), 6);
        }
    }

    // The address moves on with that counter: twice a row when R9 is odd,
    // and once only, half-way down, when it is even, the display's counter
    // not getting to R9 again before C9 does.
    {
        Rig rig(CrtcType::MC6845);
        rig.set(8, 3);
        rig.seek(0, 0);
        rig.seek(0, 0);
        CHECK_EQ(rig.crtc.ma(), 0);
        rig.seek(0, 3);
        CHECK_EQ(rig.crtc.ma(), 0);
        rig.seek(0, 4);
        CHECK_EQ(rig.crtc.ma(), 40);
        rig.seek(0, 7);
        CHECK_EQ(rig.crtc.ma(), 40);
        rig.seek(1, 0);
        CHECK_EQ(rig.crtc.ma(), 80);
        rig.seek(2, 4);
        CHECK_EQ(rig.crtc.ma(), 5 * 40);
    }
    {
        Rig rig(CrtcType::MC6845);
        rig.set(8, 3);
        rig.set(9, 6);
        rig.seek(0, 0);
        rig.seek(0, 0);
        rig.seek(0, 4);
        CHECK_EQ(rig.crtc.ma(), 40);
        rig.seek(1, 0);
        CHECK_EQ(rig.crtc.ma(), 40);
        rig.seek(1, 4);
        CHECK_EQ(rig.crtc.ma(), 80);
    }
}

// The line an interlace mode adds, and the VSYNC it holds back (19.5.4,
// 19.6.3, 19.7.2).
void interlaceAddedLine()
{
    for (const int mode : {1, 3}) {
        Rig rig(CrtcType::MC6845);
        rig.set(8, mode);
        // An even frame is a line longer, the line being one more of C4;
        // its VSYNC begins in the middle of a line.
        toFrame(rig, false);
        rig.seek(30, 0);
        CHECK(!rig.crtc.vsync());
        rig.to(30);
        CHECK(!rig.crtc.vsync());
        rig.to(31);  // R0 / 2
        CHECK(rig.crtc.vsync());
        rig.seek(38, 7);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 39);
        CHECK_EQ(rig.crtc.vlc(), 0);
        rig.nextLine();
        CHECK(atFrameStart(rig));
        CHECK(rig.crtc.oddFrame());
        // An odd one has its 312 lines, and its VSYNC where it always is.
        CHECK_EQ(rig.linesToFrameStart(), 312);
        CHECK(!rig.crtc.oddFrame());
        CHECK_EQ(rig.linesToFrameStart(), 313);
        rig.seek(30, 0);
        CHECK(rig.crtc.vsync());
    }
    // A VSYNC that a write to R7 calls for waits for the middle of the line
    // as well on an even frame ("VSYNC IVM story", R7 = 0 written on
    // character 16 of a frame's second line: 96 microseconds from the
    // frame's start on a real chip, 80 on an odd frame).
    for (const bool odd : {false, true}) {
        Rig rig(CrtcType::MC6845);
        rig.set(8, 3);
        toFrame(rig, odd);
        rig.seek(0, 1, 16);
        rig.set(7, 0);
        CHECK(rig.crtc.vsync() == odd);
        rig.to(30);
        CHECK(rig.crtc.vsync() == odd);
        rig.to(31);
        CHECK(rig.crtc.vsync());
    }
    // The parity of the next frame is settled as C4 meets R6: with R6 out
    // of reach it stays as it is, and every frame has the added line, or
    // none has.
    for (const bool odd : {false, true}) {
        Rig rig(CrtcType::MC6845);
        toFrame(rig, odd);
        rig.seek(26, 0);  // past R6: the next frame's parity is settled
        rig.set(6, 50);
        rig.set(8, 1);
        rig.seek(0, 0);
        CHECK(rig.crtc.oddFrame() != odd);
        const int lines = odd ? 312 : 313;
        CHECK_EQ(rig.linesToFrameStart(), lines);
        CHECK_EQ(rig.linesToFrameStart(), lines);
        CHECK(rig.crtc.oddFrame() != odd);
    }
    // The address for the next frame is taken on the added line, the very
    // last one, as it is on the last of the lines of R5.
    {
        Rig rig(CrtcType::MC6845);
        rig.set(8, 1);
        toFrame(rig, false);
        rig.seek(38, 7, 50);
        rig.set(12, 0x10);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 39);
        rig.to(41);
        rig.set(12, 0x20);
        rig.nextLine();
        CHECK(atFrameStart(rig));
        CHECK_EQ(rig.crtc.ma(), 0x1000);
    }

    // Two faults in that line ("Add line request & trigger" on a real
    // chip). An interlace mode set during the first line of an odd frame
    // makes that line the added one: line 0 comes again...
    {
        Rig rig(CrtcType::MC6845);
        toFrame(rig, true);
        rig.to(30);
        rig.set(8, 3);
        CHECK_EQ(rig.crtc.ra(), 1);  // odd at once
        rig.nextLine();
        CHECK(atFrameStart(rig));
        rig.nextLine();
        CHECK_EQ(rig.crtc.vlc(), 1);
        CHECK_EQ(rig.linesToFrameStart(), 311);
    }
    // ...not that of an even frame, nor a second line.
    {
        Rig rig(CrtcType::MC6845);
        toFrame(rig, false);
        rig.to(30);
        rig.set(8, 3);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vlc(), 1);
    }
    {
        Rig rig(CrtcType::MC6845);
        toFrame(rig, true);
        rig.nextLine();
        rig.to(30);
        rig.set(8, 3);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vlc(), 2);
    }
    // And the mode left during the added line, the frame does not end: C9
    // and C4 count on from there, C4 past R4.
    {
        Rig rig(CrtcType::MC6845);
        rig.set(8, 3);
        toFrame(rig, false);
        rig.seek(39, 0, 10);
        rig.set(8, 0);
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), 39);
        CHECK_EQ(rig.crtc.vlc(), 1);
        rig.seek(40, 0);
        rig.seek(127, 7);
        rig.nextLine();
        CHECK(atFrameStart(rig));
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
    interlaceVideoLines();
    interlaceAddedLine();
    return checkSummary("crtc2");
}
