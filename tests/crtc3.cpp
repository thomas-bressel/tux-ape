// What the CRTC inside Amstrad's ASICs has of its own (types 3 and 4: the
// Plus and the "cost down" CPC), as the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System describes it (chapters 11.2.6, 19.5.5, 19.6.4,
// 19.8.4, 21.2.3 and 21.3.4) and as the Logon System "Shaker" shows it on
// real machines (module D: "CRTC 3/4 status").

#include <initializer_list>
#include <string>

#include "check.h"
#include "crtc_rig.h"

namespace {

using namespace tuxape;

// The chip under test: the Plus's first, then the one of the "cost down"
// CPC, which the Compendium and the Shaker have the same in all of this.
CrtcType asic = CrtcType::AsicPlus;

int status1(Rig& rig)
{
    rig.crtc.select(10);
    return rig.crtc.readData();
}

int status2(Rig& rig)
{
    rig.crtc.select(11);
    return rig.crtc.readData();
}

// Where registers are read (21.2.3): three bits of the number count, the
// same on both ports, and 2 and 3 of the eight are the statuses.
void whatIsRead()
{
    for (const CrtcType type : {CrtcType::AsicPlus, CrtcType::PreAsic}) {
        Rig rig(type);
        rig.set(12, 0x30);
        rig.set(13, 0x45);
        rig.set(14, 0x12);
        rig.set(15, 0x34);
        rig.set(10, 0x55);  // nothing of this comes back
        rig.set(11, 0x15);
        rig.seek(5, 2, 10);
        for (const int base : {0, 8, 16, 24}) {
            rig.crtc.select(static_cast<uint8_t>(base + 4));
            CHECK_EQ(rig.crtc.readData(), 0x30);
            CHECK_EQ(rig.crtc.readStatus(), 0x30);
            rig.crtc.select(static_cast<uint8_t>(base + 5));
            CHECK_EQ(rig.crtc.readData(), 0x45);
            rig.crtc.select(static_cast<uint8_t>(base + 6));
            CHECK_EQ(rig.crtc.readData(), 0x12);
            rig.crtc.select(static_cast<uint8_t>(base + 7));
            CHECK_EQ(rig.crtc.readData(), 0x34);
            rig.crtc.select(static_cast<uint8_t>(base + 2));
            CHECK_EQ(rig.crtc.readData(), 0xFE);
            CHECK_EQ(rig.crtc.readStatus(), 0xFE);
            rig.crtc.select(static_cast<uint8_t>(base + 3));
            CHECK_EQ(rig.crtc.readData(), 0x37);
        }
    }
    // The other chips have no such thing.
    for (const CrtcType type : {CrtcType::HD6845S, CrtcType::UM6845R, CrtcType::MC6845}) {
        Rig rig(type);
        rig.seek(5, 2, 10);
        CHECK_EQ(status1(rig), 0x00);
        CHECK_EQ(status2(rig), 0x00);
    }
}

// The first status, along a line (21.3.4.1). The Shaker's first page reads
// it on a real machine: &FF on C0 = 63, &FE on C0 = 0 and 1, &FC on C0 = 31,
// and &FC on C0 = 32 with R0 = 64.
void statusOneAlongALine()
{
    Rig rig(asic);
    rig.set(12, 0x30);
    rig.seek(0, 0, 0);
    rig.seek(5, 2, 0);  // the row's lines start at &30C8
    for (int c0 = 0; c0 < 64; ++c0) {
        int expected = 0xFE;
        if (c0 == 63)
            expected |= 0x01;  // C0 = R0
        if (c0 == 31)
            expected &= ~0x02;  // C0 = R0 / 2
        if (c0 == 39)
            expected &= ~0x04;  // C0 = R1 - 1
        if (c0 == 46)
            expected &= ~0x08;  // C0 = R2
        if (c0 == 60)
            expected &= ~0x10;  // C0 = R2 + R3
        if (c0 == 55)
            expected &= ~0x80;  // the next address is &3100
        CHECK_EQ(rig.crtc.hcc(), c0);
        CHECK_EQ(status1(rig), expected);
        rig.crtc.tick();
    }
    rig.set(0, 64);
    rig.to(32);
    CHECK_EQ(status1(rig), 0xFC);
    rig.to(31);
    CHECK_EQ(status1(rig), 0xFE);
    // A width of 0 is sixteen characters.
    rig.set(0, 63);
    rig.set(3, 0x80);
    rig.seek(6, 2, 62);
    CHECK_EQ(status1(rig), 0xEE);
    // "C0 = R1 - 1" wants R1 within the line.
    rig.set(3, 0x8E);
    rig.set(1, 64);
    rig.seek(7, 2, 63);
    CHECK_EQ(status1(rig), 0xFF);
}

// Bit 7 of the first status: the address the next character will have ends
// in &00. On a line's last character that is the address the next line
// starts at: the row's own, the next row's once C0 has met R1 on the row's
// last line, R12/R13 as the frame ends.
void statusOneAddress()
{
    Rig rig(asic);
    rig.set(12, 0x30);
    rig.seek(0, 0, 0);
    for (int line = 0; line < 8; ++line) {
        rig.to(63);
        CHECK_EQ(status1(rig) >> 7, line == 7 ? 1 : 0);  // &3000, then &3028
        rig.nextLine();
    }
    rig.seek(38, 7, 63);
    CHECK_EQ(status1(rig) >> 7, 0);
    rig.set(13, 0x01);
    CHECK_EQ(status1(rig) >> 7, 1);
    rig.set(13, 0x00);
    // The Shaker's third page, on a real machine: with lines of one
    // character &7D, and &FD once R13 is not 0; with lines of two, &FC on
    // the first, and &F8 when R1 is 1; with lines of three, &FC on the
    // second.
    rig.set(0, 0);
    rig.seek(3, 0, 0);
    CHECK_EQ(status1(rig), 0x7D);
    rig.set(1, 1);
    rig.seek(4, 0, 0);
    CHECK_EQ(status1(rig), 0x7D);
    rig.set(13, 0x01);
    rig.seek(0, 3, 0);
    CHECK_EQ(status1(rig), 0xFD);
    rig.set(13, 0x00);
    rig.set(1, 40);
    rig.set(0, 1);
    rig.seek(0, 3, 0);
    CHECK_EQ(status1(rig), 0xFC);
    rig.set(1, 1);
    rig.seek(0, 5, 0);
    CHECK_EQ(status1(rig), 0xF8);
    rig.set(0, 2);
    rig.seek(0, 5, 1);
    CHECK_EQ(status1(rig), 0xFC);
    rig.set(1, 40);
    rig.seek(0, 5, 1);
    CHECK_EQ(status1(rig), 0xFC);
}

// Bit 5 of the first status: the VSYNC's lines, counted from 1, against
// R3's high half. The Shaker finds that half by counting the lines from
// the VSYNC's first to the one where the bit is 0 (9 for R3 = &9D on a real
// machine), and its map of the bit shows, with 0 there, fifteen lines at 1
// from the VSYNC's first.
void statusOneVsync()
{
    for (const int lines : {9, 7, 1, 0}) {
        Rig rig(asic);
        rig.set(3, lines << 4 | 0x0D);
        rig.seek(29, 7, 20);
        const int rest = lines == 0 ? 0 : 1;
        CHECK_EQ(status1(rig) >> 5 & 1, rest);
        rig.nextLine();
        CHECK(rig.crtc.vsync());
        for (int line = 1; line <= 20; ++line) {
            const int during = lines == 0 ? 16 : lines;
            const int expected = line > during ? rest : line == during ? 0 : 1;
            rig.to(0);
            CHECK_EQ(status1(rig) >> 5 & 1, expected);
            rig.to(63);
            CHECK_EQ(status1(rig) >> 5 & 1, expected);
            CHECK_EQ(rig.crtc.vsync(), line <= during);
            rig.nextLine();
        }
    }
}

// The second status over a frame (21.3.4.2).
void statusTwoOverAFrame()
{
    Rig rig(asic);
    rig.seek(0, 0, 0);
    const int timer = status2(rig) & 0x08;
    for (int row = 0; row < 39; ++row) {
        for (int line = 0; line < 8; ++line) {
            for (const int c0 : {0, 30, 62, 63}) {
                rig.to(c0);
                int expected = 0x37 | timer;
                if (line == 7)
                    expected &= ~0x20;  // C9 = R9
                // The next character is on a row's first line.
                if (c0 == 63 ? line == 7 : line == 0)
                    expected |= 0x80;
                if (line == 7 && c0 == 63) {
                    if (row == 38)
                        expected &= ~0x01;  // the frame's last character
                    if (row == 24)
                        expected &= ~0x02;  // the last one shown
                    if (row == 29)
                        expected &= ~0x04;  // the last one before the VSYNC
                }
                CHECK_EQ(rig.crtc.vcc(), row);
                CHECK_EQ(rig.crtc.vlc(), line);
                CHECK_EQ(status2(rig), expected);
            }
            rig.nextLine();
        }
    }
    CHECK(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0);
}

// The second status and the lines of R5. The Shaker's search for the
// registers finds R4 = 37 and R5 = 8 on a real machine from two things: bit
// 0 falls once a frame, on the last character of the last row, and bit 4,
// always set when R5 is 0, is then only set on the last character of the
// last of the lines of R5.
void statusTwoAndR5()
{
    Rig rig(asic);
    rig.seek(1, 0, 0);
    rig.set(4, 37);
    rig.set(5, 8);
    rig.seek(0, 0, 0);
    int frameEnds = 0, adjustEnds = 0;
    for (int line = 0; line < 312; ++line) {
        for (int c0 = 0; c0 < 64; ++c0) {
            const int status = status2(rig);
            if (!(status & 0x01)) {
                ++frameEnds;
                CHECK_EQ(line, 303);
                CHECK_EQ(c0, 63);
            }
            if (status & 0x10) {
                ++adjustEnds;
                CHECK_EQ(line, 311);
                CHECK_EQ(c0, 63);
            }
            rig.crtc.tick();
        }
    }
    CHECK(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0 && rig.crtc.hcc() == 0);
    CHECK_EQ(frameEnds, 1);
    CHECK_EQ(adjustEnds, 1);
    // Without lines of R5 the bit is set all the time.
    rig.set(4, 38);
    rig.set(5, 0);
    for (int i = 0; i < 312 * 64; ++i) {
        CHECK(status2(rig) & 0x10);
        rig.crtc.tick();
    }
}

// Bit 3 of the second status turns over every sixteen frames. The Shaker's
// fourth page makes each line a frame and reads &97 for sixteen lines,
// then &9F for sixteen.
void statusTwoTimer()
{
    Rig rig(asic);
    rig.seek(1, 0, 0);
    rig.set(4, 0);
    rig.set(9, 0);
    rig.seek(0, 0, 10);
    // To the line where the bit turns over.
    const int first = status2(rig);
    CHECK_EQ(first & ~0x08, 0x97);
    int guard = 0;
    while (status2(rig) == first && ++guard < 40) {
        rig.nextLine();
        rig.to(10);
    }
    CHECK(guard < 17);
    for (int line = 0; line < 64; ++line) {
        CHECK_EQ(status2(rig), (line / 16) % 2 == 0 ? first ^ 0x08 : first);
        rig.nextLine();
        rig.to(10);
    }
}

// Brings the chip to the start of an odd or an even frame. The parity turns
// over with every frame, whatever R8 holds (19.5.5).
void toFrame(Rig& rig, bool odd)
{
    for (int frame = 0; frame < 4; ++frame) {
        // To the first character of a frame: C4 back at 0 (C9 need not be,
        // with an interlace mode set).
        int before = 0;
        do {
            before = rig.crtc.vcc();
            rig.crtc.tick();
        } while (rig.crtc.vcc() != 0 || before == 0);
        if (rig.crtc.oddFrame() == odd)
            return;
    }
    std::printf("toFrame: no %s frame\n", odd ? "odd" : "even");
    ++g_failures;
}

// C4 and C9 at the start of each of the lines that follow.
struct Line {
    int c4, c9;
};

void expectLines(Rig& rig, std::initializer_list<Line> lines)
{
    for (const Line& line : lines) {
        rig.nextLine();
        CHECK_EQ(rig.crtc.vcc(), line.c4);
        CHECK_EQ(rig.crtc.vlc(), line.c9);
    }
}

// "Interlace sync & video" set in the middle of a frame (19.8.4, the six
// diagrams of "Switching to IVM mode"): the lines take the parity C9 has on
// the line of the write, in an odd frame as in an even one, C9 goes up two
// at a time from there, and a row ends once C9 has reached R9 or gone past
// it. With R9 odd the parity then turns over with every row, rows of five
// even lines and rows of four odd ones following one another.
void interlaceSwitchedOn()
{
    for (const bool odd : {false, true}) {
        {
            Rig rig(asic);
            toFrame(rig, odd);
            rig.to(20);
            rig.set(8, 3);
            expectLines(rig, {{0, 2}, {0, 4}, {0, 6}, {0, 8}, {1, 1}, {1, 3}, {1, 5}, {1, 7}, {2, 0}, {2, 2}, {2, 4}, {2, 6}, {2, 8}});
        }
        {
            Rig rig(asic);
            toFrame(rig, odd);
            rig.seek(0, 1, 20);
            rig.set(8, 3);
            expectLines(rig, {{0, 3}, {0, 5}, {0, 7}, {1, 0}, {1, 2}, {1, 4}, {1, 6}, {1, 8}, {2, 1}, {2, 3}, {2, 5}, {2, 7}});
        }
        {
            Rig rig(asic);
            toFrame(rig, odd);
            rig.seek(0, 2, 20);
            rig.set(8, 3);
            expectLines(rig, {{0, 4}, {0, 6}, {0, 8}, {1, 1}, {1, 3}, {1, 5}, {1, 7}, {2, 0}, {2, 2}, {2, 4}, {2, 6}});
        }
        // R9 even: every row has the same lines.
        {
            Rig rig(asic);
            rig.set(9, 6);
            toFrame(rig, odd);
            rig.to(20);
            rig.set(8, 3);
            expectLines(rig, {{0, 2}, {0, 4}, {0, 6}, {1, 0}, {1, 2}, {1, 4}, {1, 6}, {2, 0}, {2, 2}, {2, 4}, {2, 6}, {3, 0}, {3, 2}});
        }
        {
            Rig rig(asic);
            rig.set(9, 6);
            toFrame(rig, odd);
            rig.seek(0, 1, 20);
            rig.set(8, 3);
            expectLines(rig, {{0, 3}, {0, 5}, {0, 7}, {1, 1}, {1, 3}, {1, 5}, {1, 7}, {2, 1}, {2, 3}, {2, 5}, {2, 7}, {3, 1}});
        }
        {
            Rig rig(asic);
            rig.set(9, 6);
            toFrame(rig, odd);
            rig.seek(0, 2, 20);
            rig.set(8, 3);
            expectLines(rig, {{0, 4}, {0, 6}, {1, 0}, {1, 2}, {1, 4}, {1, 6}, {2, 0}, {2, 2}, {2, 4}, {2, 6}, {3, 0}});
        }
    }
    // The mode left, C9 goes up one at a time again from where it is.
    {
        Rig rig(asic);
        toFrame(rig, false);
        rig.seek(0, 1, 20);
        rig.set(8, 3);
        rig.seek(1, 2, 20);
        rig.set(8, 0);
        expectLines(rig, {{1, 3}, {1, 4}, {1, 5}, {1, 6}, {1, 7}, {2, 0}, {2, 1}});
    }
}

// With the mode left on, every new frame gives the lines its own parity
// (19.5.5): odd lines first on an odd frame, even ones on an even frame.
// And where an odd frame's row has even lines (an odd row when R9 is odd),
// the VSYNC comes a line late, which keeps the two frames in step; on an
// even frame it waits for the middle of the line.
void interlaceFrames()
{
    for (const bool odd : {false, true}) {
        Rig rig(asic);
        rig.set(4, 9);
        rig.set(6, 8);
        rig.set(7, 30);
        rig.seek(1, 0, 0);
        rig.set(8, 3);
        toFrame(rig, odd);
        CHECK_EQ(rig.crtc.vlc(), odd ? 1 : 0);
        if (odd)
            expectLines(rig, {{0, 3}, {0, 5}, {0, 7}, {1, 0}, {1, 2}, {1, 4}, {1, 6}, {1, 8}, {2, 1}, {2, 3}, {2, 5}, {2, 7}, {3, 0}});
        else
            expectLines(rig, {{0, 2}, {0, 4}, {0, 6}, {0, 8}, {1, 1}, {1, 3}, {1, 5}, {1, 7}, {2, 0}, {2, 2}, {2, 4}, {2, 6}, {2, 8}, {3, 1}});
    }
    for (const bool odd : {false, true}) {
        for (const int row : {1, 2, 3, 4}) {
            Rig rig(asic);
            rig.set(4, 9);
            rig.set(6, 8);
            rig.set(7, row);
            rig.seek(row + 1, 0, 0);
            rig.set(8, 3);
            toFrame(rig, odd);
            rig.set(3, 0x2E);  // a VSYNC of two lines
            while (rig.crtc.vcc() != row)
                rig.nextLine();
            // The row's first line: C9 is the lines' parity there.
            const bool evenLines = rig.crtc.vlc() == 0;
            CHECK_EQ(evenLines, odd == ((row & 1) != 0));
            if (!odd) {
                CHECK(!rig.crtc.vsync());
                rig.to(31);
                CHECK(rig.crtc.vsync());
            } else if (evenLines) {
                rig.to(63);
                CHECK(!rig.crtc.vsync());
                rig.nextLine();
                CHECK_EQ(rig.crtc.vlc(), 2);
                CHECK(rig.crtc.vsync());
            } else {
                CHECK(rig.crtc.vsync());
            }
        }
    }
}

// R3 written during the HSYNC (14.5.3, with R2 = 11 and R3 = 10, the write
// landing on the pulse's fifth character as these chips count it: they
// start their pulse, and its counter, a character after C0 = R2). A width
// above the counter is the new end; the counter's own value comes too late
// and the pulse goes all the way round; and the value the counter is about
// to take ends the pulse with the character of the write. The Gate Array
// sees all of this a character late (GateArray::sync), which makes whole
// characters of it: 5 for a width of 5, 6 for 6, 20 for 4.
void r3WrittenDuringHsync()
{
    struct {
        int width;
        int lasts;  // characters of HSYNC from C0 = R2 on, the one of the write included
    } const cases[] = {{0, 16}, {1, 17}, {2, 18}, {3, 19}, {4, 20}, {5, 6}, {6, 6}, {7, 7}, {10, 10}};
    for (const auto& c : cases) {
        Rig rig(asic);
        rig.set(2, 11);
        rig.set(3, 0x8A);
        rig.seek(5, 2, 10);
        CHECK(!rig.crtc.hsync());
        rig.to(16);
        CHECK(rig.crtc.hsync());
        rig.set(3, 0x80 | c.width);
        // For 5 the pulse is over as the write is made, its sixth
        // character begun: the Gate Array, a character behind, has seen
        // five.
        CHECK_EQ(rig.crtc.hsync(), c.width != 5);
        int lasts = 6;
        while (rig.crtc.hsync() && lasts < 40) {
            rig.crtc.tick();
            if (rig.crtc.hsync())
                ++lasts;
        }
        CHECK_EQ(lasts, c.lasts);
    }
    // The Shaker's "R3 JIT" on a real Plus: R2 = R3 = 14, then R3 = 1, 2
    // or 3 written one, two or three characters after C0 = R2 give bars of
    // one, two and three characters, and R3 = 0 written on C0 = R2 one of
    // sixteen.
    for (const int width : {0, 1, 2, 3}) {
        Rig rig(asic);
        rig.set(2, 14);
        rig.set(3, 0x8E);
        rig.seek(5, 2, 14 + width);
        rig.set(3, 0x80 | width);
        CHECK_EQ(rig.crtc.hsync(), width == 0);
        if (width == 0) {
            int lasts = 1;
            while (rig.crtc.hsync() && lasts < 40) {
                rig.crtc.tick();
                if (rig.crtc.hsync())
                    ++lasts;
            }
            CHECK_EQ(lasts, 16);
        }
    }
    // The same write on a discrete chip is another story (crtc0, crtc2):
    // here only that the pulse does not go round on type 0.
    {
        Rig rig(CrtcType::HD6845S);
        rig.set(2, 11);
        rig.set(3, 0x8A);
        rig.seek(5, 2, 16);
        rig.set(3, 0x85);
        CHECK(!rig.crtc.hsync());
    }
}

// A pulse that begins on the very character where the one before ended
// does not start its counter again, which is at R3: it lasts sixteen
// characters whatever R3 says. The Shaker's "R2 update during HSYNC", with
// a pulse of 10 begun at &0B, on real machines of types 3 and 4: R2
// brought to &15 gives 26 characters of black, to &16 two bars of 10 a
// character apart.
void pulseBegunWhereOneEnds()
{
    for (const int second : {0x15, 0x16, 0x18}) {
        Rig rig(asic);
        rig.set(2, 0x0B);
        rig.set(3, 0x8A);
        rig.seek(5, 2, 0x10);
        rig.set(2, second);
        std::string line;
        for (int c0 = 0x10; c0 < 0x30; ++c0) {
            rig.to(c0);
            line += rig.crtc.hsync() ? '#' : '.';
        }
        const char* expected = second == 0x15   ? "#####################..........."
                               : second == 0x16 ? "#####.##########................"
                                                : "#####...##########..............";
        if (line != expected) {
            std::printf("R2 = &%02X during the pulse: %s, want %s\n", second, line.c_str(), expected);
            ++g_failures;
        }
    }
    // A discrete chip starts counting again (and type 0 never joins two
    // pulses at all, see crtc0).
    {
        Rig rig(CrtcType::UM6845R);
        rig.set(2, 0x0B);
        rig.set(3, 0x8A);
        rig.seek(5, 2, 0x10);
        rig.set(2, 0x15);
        rig.to(0x1E);
        CHECK(rig.crtc.hsync());
        rig.to(0x1F);
        CHECK(!rig.crtc.hsync());
    }
}

}  // namespace

int main()
{
    whatIsRead();
    for (const CrtcType type : {CrtcType::AsicPlus, CrtcType::PreAsic}) {
        asic = type;
        statusOneAlongALine();
        statusOneAddress();
        statusOneVsync();
        statusTwoOverAFrame();
        statusTwoAndR5();
        statusTwoTimer();
        interlaceSwitchedOn();
        interlaceFrames();
        r3WrittenDuringHsync();
        pulseBegunWhereOneEnds();
    }
    return checkSummary("crtc3");
}
