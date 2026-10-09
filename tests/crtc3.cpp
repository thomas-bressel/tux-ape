// What the CRTC inside Amstrad's ASICs has of its own (types 3 and 4: the
// Plus and the "cost down" CPC), as the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System describes it (chapters 11.2.6, 19.5.5, 19.6.4,
// 19.8.4, 21.2.3 and 21.3.4) and as the Logon System "Shaker" shows it on
// real machines (module D: "CRTC 3/4 status").

#include <initializer_list>

#include "check.h"
#include "crtc_rig.h"

namespace {

using namespace tuxape;

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
    Rig rig(CrtcType::AsicPlus);
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
    Rig rig(CrtcType::AsicPlus);
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
        Rig rig(CrtcType::AsicPlus);
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
    Rig rig(CrtcType::AsicPlus);
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
    Rig rig(CrtcType::AsicPlus);
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
    Rig rig(CrtcType::AsicPlus);
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

}  // namespace

int main()
{
    whatIsRead();
    statusOneAlongALine();
    statusOneAddress();
    statusOneVsync();
    statusTwoOverAFrame();
    statusTwoAndR5();
    statusTwoTimer();
    return checkSummary("crtc3");
}
