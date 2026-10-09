// When a write to the Gate Array or to the CRTC shows on screen, measured
// on the picture itself.
//
// The expected figures are from the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System, chapters 4.4.3 (which microsecond of an I/O
// instruction the write lands on), 9.2.2 (where within the character an ink
// change shows) and 9.3 (when a mode change is taken).

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "check.h"
#include "core/cpc.h"

namespace {

using namespace tuxape;

constexpr uint32_t kBlack = 0xFF000000;

// The three ways of writing to a port, which do not land on the same
// microsecond of the instruction.
enum class Form { OutC, OutN, Outi };

struct Probe {
    int row = -1;  // scanline of the picture where the change first shows
    int x = -1;    // and the pixel, 0-767
};

void outPort(Cpc& cpc, uint16_t port, uint8_t value)
{
    cpc.out(port, value);
}

// Runs a program that synchronises on an interrupt, waits `nops`
// microseconds, then writes `data` to the port whose high address byte is
// `portHigh`, with the given form of instruction. Returns where a pixel
// first differs from `before`, the colour the area had until then.
Probe measure(CrtcType type, Form form, uint8_t portHigh, uint8_t data, bool allBorder, uint32_t before, int mode = 0)
{
    Cpc cpc;
    cpc.crtc().setType(type);
    cpc.memory().setRomEnables(false, false);
    Memory& mem = cpc.memory();

    // Standard screen; all of it border when asked.
    const uint8_t crtc[] = {63, 40, 46, 0x8E, 38, 0, static_cast<uint8_t>(allBorder ? 0 : 25), 30, 0, 7, 0, 0, 0x30, 0};
    for (uint8_t r = 0; r < sizeof crtc; ++r) {
        outPort(cpc, 0xBC00, r);
        outPort(cpc, 0xBD00, crtc[r]);
    }
    // Pen 0 black (video memory is all zeros), border black or white.
    outPort(cpc, 0x7F00, 0x00);
    outPort(cpc, 0x7F00, 0x54);
    outPort(cpc, 0x7F00, 0x10);
    outPort(cpc, 0x7F00, allBorder ? 0x54 : 0x4B);
    outPort(cpc, 0x7F00, static_cast<uint8_t>(0x8C | mode));  // no ROM
    // Leave the right register selected for the write under test.
    outPort(cpc, 0xBC00, 8);
    outPort(cpc, 0x7F00, 0x10);

    // Like a real one, the monitor needs a few frames to lock on to the
    // vertical sync. Memory is all zeros, so the CPU just runs NOPs.
    cpc.run(20 * Cpc::kFrameMicroseconds);

    auto poke = [&](uint16_t addr, std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes)
            mem.write(addr++, b);
        return addr;
    };

    // Main program: wait for VSYNC, then idle with interrupts on.
    poke(0x4000, {
        0xF3,              // di
        0x06, 0xF5,        // ld b,&F5
        0xED, 0x78,        // in a,(c)
        0x1F,              // rra
        0x30, 0xFB,        // jr nc,-5
        0xAF,              // xor a
        0x32, 0x00, 0x50,  // ld (&5000),a
        0xED, 0x56,        // im 1
        0xFB,              // ei
        0x76,              // halt
        0x18, 0xFD,        // jr -3
    });

    // Interrupt handler: let two interrupts go by, act on the third, which
    // falls inside the displayed area. Every form runs the same register
    // loads first, so that the instruction under test always starts on the
    // same microsecond.
    uint16_t at = poke(0x0038, {
        0xF5,              // push af
        0x3A, 0x00, 0x50,  // ld a,(&5000)
        0x3C,              // inc a
        0x32, 0x00, 0x50,  // ld (&5000),a
        0xFE, 0x03,        // cp 3
        0x28, 0x03,        // jr z,event
        0xF1,              // pop af
        0xFB,              // ei
        0xC9,              // ret
    });
    const uint8_t b = form == Form::Outi ? portHigh + 1 : portHigh;
    const uint8_t a = form == Form::OutN ? portHigh : data;
    at = poke(at, {
        0x01, data, b,     // ld bc,...
        0x21, 0x00, 0x51,  // ld hl,&5100
        0x3E, a,           // ld a,...
    });
    mem.write(0x5100, data);
    switch (form) {
    case Form::OutC: at = poke(at, {0xED, 0x49}); break;  // out (c),c
    case Form::OutN: at = poke(at, {0xD3, 0xFF}); break;  // out (&FF),a
    case Form::Outi: at = poke(at, {0xED, 0xA3}); break;  // outi
    }
    poke(at, {0xF3, 0x76});  // di : halt

    auto& cpu = cpc.cpu();
    cpu.pc = 0x4000;
    cpu.sp = 0x8000;
    // Run until the program has stopped for good, then to the end of the
    // frame it stopped in.
    for (int guard = 0; guard < 200 && !(cpu.halted && !cpu.iff1); ++guard)
        cpc.run(1000);
    CHECK(cpu.halted && !cpu.iff1);
    const uint64_t frame = cpc.monitor().frameNumber();
    while (cpc.monitor().frameNumber() == frame)
        cpc.run(1000);

    Probe probe;
    const uint32_t* pixels = cpc.monitor().frame();

    // Look only where the firmware's screen is: the 200 lines in the middle
    // and, across, characters that are picture on every machine. The rest
    // is ordinary border.
    for (int row = 35; row < 235 && probe.row < 0; ++row) {
        for (int x = 5 * 16; x < 43 * 16; ++x) {
            if (pixels[row * Monitor::kWidth + x] != before) {
                probe.row = row;
                probe.x = x;
                break;
            }
        }
    }
    return probe;
}

// An ink change, seen on the border colour.
void testInkTiming(CrtcType type, int splitPixel)
{
    // With OUT (n),A the data byte is the port's high byte, &7F: colour 31.
    const Probe outC = measure(type, Form::OutC, 0x7F, 0x5F, true, kBlack);
    const Probe outN = measure(type, Form::OutN, 0x7F, 0x5F, true, kBlack);
    const Probe outi = measure(type, Form::Outi, 0x7F, 0x5F, true, kBlack);
    CHECK(outC.row > 0);
    CHECK_EQ(outN.row, outC.row);
    CHECK_EQ(outi.row, outC.row);
    // OUT (C),r and OUT (n),A land on the third microsecond, OUTI on the
    // fifth: two characters further right.
    CHECK_EQ(outN.x, outC.x);
    CHECK_EQ(outi.x - outC.x, 2 * 16);
    // The new colour starts part-way through a character.
    CHECK_EQ(outC.x % 16, splitPixel);
}

// Where the picture starts and where it ends, on a line in the middle of a
// standard screen in the given mode: black paper in a white border.
struct Edges {
    int first = -1;  // the first pixel of the picture
    int after = -1;  // the first pixel of the border that follows it
};

Edges pictureEdges(CrtcType type, int mode)
{
    Cpc cpc;
    cpc.crtc().setType(type);
    cpc.memory().setRomEnables(false, false);
    const uint8_t crtc[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0};
    for (uint8_t r = 0; r < sizeof crtc; ++r) {
        outPort(cpc, 0xBC00, r);
        outPort(cpc, 0xBD00, crtc[r]);
    }
    outPort(cpc, 0x7F00, 0x00);
    outPort(cpc, 0x7F00, 0x54);
    outPort(cpc, 0x7F00, 0x10);
    outPort(cpc, 0x7F00, 0x4B);
    outPort(cpc, 0x7F00, static_cast<uint8_t>(0x8C | mode));
    // Memory is all zeros: the CPU runs NOPs while the monitor settles.
    cpc.run(20 * Cpc::kFrameMicroseconds);
    const uint32_t* line = cpc.monitor().frame() + 135 * Monitor::kWidth;
    Edges edges;
    for (int x = 1; x < Monitor::kWidth && edges.after < 0; ++x) {
        if (edges.first < 0 && line[x] == kBlack && line[x - 1] != kBlack)
            edges.first = x;
        else if (edges.first >= 0 && line[x] != kBlack)
            edges.after = x;
    }
    return edges;
}

// The firmware's screen is centred on every machine. The ASICs (CRTC 3 and
// 4) send their HSYNC a microsecond later than a Gate Array, which on the
// same monitor would put the picture a character to the left; the monitors
// sold with those machines are set for it (Compendium 15.1).
void testCentred()
{
    for (const CrtcType type : {CrtcType::HD6845S, CrtcType::UM6845R, CrtcType::MC6845, CrtcType::AsicPlus,
                                CrtcType::PreAsic}) {
        const Edges edges = pictureEdges(type, 1);
        CHECK_EQ(edges.first, 64);
        CHECK_EQ(edges.after, Monitor::kWidth - 64);
    }
}

// A Gate Array is a pixel early in mode 2: the picture starts and ends one
// pixel sooner than in the other modes. Not so the Plus's ASIC, which
// keeps its modes in line (Compendium 9.1; the Shaker's "Gate Array
// moderisation" screen shows the step between its zones on a real machine
// of each kind).
void testModeTwoEarly()
{
    for (const CrtcType type : {CrtcType::HD6845S, CrtcType::UM6845R, CrtcType::MC6845, CrtcType::PreAsic,
                                CrtcType::AsicPlus}) {
        const int early = type == CrtcType::AsicPlus ? 0 : 1;
        const Edges one = pictureEdges(type, 1);
        CHECK(one.first > 0 && one.after - one.first == 640);
        for (const int mode : {0, 3})
            CHECK_EQ(pictureEdges(type, mode).first, one.first);
        const Edges two = pictureEdges(type, 2);
        CHECK_EQ(two.first, one.first - early);
        CHECK_EQ(two.after, one.after - early);

        // An ink set in the middle of a line changes at the same place on
        // the screen whatever the mode. But on the 40226 (CRTC 4), where
        // the change comes a pixel sooner in mode 2, as the pixels do
        // (9.2.2).
        const int sooner = type == CrtcType::PreAsic ? 1 : 0;
        for (const Form form : {Form::OutC, Form::Outi}) {
            const Probe other = measure(type, form, 0x7F, 0x5F, true, kBlack, 1);
            const Probe inTwo = measure(type, form, 0x7F, 0x5F, true, kBlack, 2);
            CHECK(other.row > 0);
            CHECK_EQ(inTwo.row, other.row);
            CHECK_EQ(inTwo.x, other.x - sooner);
        }
    }
}

// Where an HSYNC's blanking starts and ends on the screen: a second pulse
// is made on one line of the picture, at C0 = 20, by moving R2 there for
// that line only. Returns the first black pixel of that line and the first
// one after the black; the screen is a white paper in a white border.
Edges hsyncBar(CrtcType type, int mode)
{
    Cpc cpc;
    cpc.crtc().setType(type);
    cpc.memory().setRomEnables(false, false);
    const uint8_t crtc[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0};
    for (uint8_t r = 0; r < sizeof crtc; ++r) {
        outPort(cpc, 0xBC00, r);
        outPort(cpc, 0xBD00, crtc[r]);
    }
    outPort(cpc, 0x7F00, 0x00);
    outPort(cpc, 0x7F00, 0x4B);
    outPort(cpc, 0x7F00, 0x10);
    outPort(cpc, 0x7F00, 0x4B);
    outPort(cpc, 0x7F00, static_cast<uint8_t>(0x8C | mode));
    outPort(cpc, 0xBC00, 2);
    cpc.run(20 * Cpc::kFrameMicroseconds);
    // Once this line's own HSYNC has begun, R2 goes to 20; it comes back
    // once the next line has had its pulse there, in time for the usual
    // one at 46.
    const Crtc& c = cpc.crtc();
    auto runTo = [&](int row, int line, int column) {
        for (int guard = 0; guard < 4 * 312 * 64; ++guard) {
            if (c.vcc() == row && c.vlc() == line && c.hcc() == column)
                return;
            cpc.run(1);
        }
        CHECK(false);
    };
    runTo(12, 3, 50);
    outPort(cpc, 0xBD00, 20);
    runTo(12, 4, 36);
    outPort(cpc, 0xBD00, 46);
    const uint64_t frame = cpc.monitor().frameNumber();
    while (cpc.monitor().frameNumber() == frame)
        cpc.run(1000);
    const uint32_t* pixels = cpc.monitor().frame();
    Edges edges;
    for (int row = 40; row < 230 && edges.first < 0; ++row) {
        const uint32_t* line = pixels + row * Monitor::kWidth;
        for (int x = 100; x < 700 && edges.after < 0; ++x) {
            if (edges.first < 0 && line[x] == kBlack)
                edges.first = x;
            else if (edges.first >= 0 && line[x] != kBlack)
                edges.after = x;
        }
    }
    return edges;
}

// The blanking, to the pixel (Compendium 9.3.4.2, "HSYNC under the
// microscope", and 14.7.2 for CRTC 4; the Shaker's "R3 JIT" screens against
// Amspirit-lite's captures for the Plus). A Gate Array blanks from the
// character before the one R2 names: three pixels into it with the HD6845S,
// four with the UM6845R, two with the MC6845, and the picture is back as
// far into the character after the pulse, one pixel further with the
// MC6845. The ASICs blank from the character R2 names: the Plus's a pixel
// before it, for as many characters as the pulse has; the 40226 on it, and
// for a pixel more. The screen mode changes none of it.
void testBlanking()
{
    struct Expected {
        CrtcType type;
        int character;  // where the blanking starts: R2 - 1 or R2
        int first, after;  // pixels from that character, and from the one 14 further
    };
    const Expected expected[] = {
        {CrtcType::HD6845S, 19, 3, 3}, {CrtcType::UM6845R, 19, 4, 4}, {CrtcType::MC6845, 19, 2, 3},
        {CrtcType::AsicPlus, 20, -1, -1}, {CrtcType::PreAsic, 20, 0, 1},
    };
    for (const Expected& e : expected) {
        for (const int mode : {1, 2}) {
            const Edges bar = hsyncBar(e.type, mode);
            CHECK_EQ(bar.first, 64 + e.character * 16 + e.first);
            CHECK_EQ(bar.after, 64 + (e.character + 14) * 16 + e.after);
        }
    }
}

// The pens a byte gives in each mode, one for each of its eight pixels of
// mode 2, written out again here from the manual's description.
void pensOf(int mode, uint8_t b, uint8_t out[8])
{
    auto bit = [b](int n) { return (b >> n) & 1; };
    for (int x = 0; x < 8; ++x) {
        switch (mode) {
        case 0: out[x] = x < 4 ? bit(7) | bit(3) << 1 | bit(5) << 2 | bit(1) << 3 : bit(6) | bit(2) << 1 | bit(4) << 2 | bit(0) << 3; break;
        case 1: out[x] = static_cast<uint8_t>(bit(7 - x / 2) | bit(3 - x / 2) << 1); break;
        case 2: out[x] = static_cast<uint8_t>(bit(7 - x)); break;
        default: out[x] = x < 4 ? bit(7) | bit(3) << 1 : bit(6) | bit(2) << 1; break;
        }
    }
}

// A line of the picture on which a pulse of two characters, at C0 = 20,
// takes the screen from one mode to another: the 17 pixels the chip puts
// out for the character that follows the pulse, the first of them where
// the character before ended. The screen is filled with one byte, each pen
// has a colour of its own, none of them black, and the border is white.
struct ModeChange {
    uint32_t pixels[17];
    uint32_t colour[16];
};

ModeChange modeChange(CrtcType type, int from, int to, uint8_t byte)
{
    Cpc cpc;
    cpc.crtc().setType(type);
    cpc.memory().setRomEnables(false, false);
    const uint8_t crtc[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0};
    for (uint8_t r = 0; r < sizeof crtc; ++r) {
        outPort(cpc, 0xBC00, r);
        outPort(cpc, 0xBD00, crtc[r]);
    }
    static const uint8_t inks[16] = {0x44, 0x55, 0x5C, 0x58, 0x5D, 0x4C, 0x45, 0x4D, 0x56, 0x46, 0x57, 0x5E, 0x40, 0x5F, 0x4E, 0x47};
    ModeChange result{};
    for (uint8_t pen = 0; pen < 16; ++pen) {
        outPort(cpc, 0x7F00, pen);
        outPort(cpc, 0x7F00, inks[pen]);
        result.colour[pen] = cpc.gateArray().colour(inks[pen] & 31);
    }
    outPort(cpc, 0x7F00, 0x10);
    outPort(cpc, 0x7F00, 0x4B);
    outPort(cpc, 0x7F00, static_cast<uint8_t>(0x8C | from));
    for (int i = 0xC000; i < 0x10000; ++i)
        cpc.memory().baseRam()[i] = byte;
    // The processor waits, a microsecond at a time, whatever the screen
    // holds: DI, HALT.
    cpc.memory().write(0x0000, 0xF3);
    cpc.memory().write(0x0001, 0x76);
    cpc.cpu().pc = 0x0000;
    cpc.run(20 * Cpc::kFrameMicroseconds);
    const Crtc& c = cpc.crtc();
    auto runTo = [&](int row, int line, int column) {
        for (int guard = 0; guard < 4 * 312 * 64; ++guard) {
            if (c.vcc() == row && c.vlc() == line && c.hcc() == column)
                return;
            cpc.run(1);
        }
        CHECK(false);
    };
    // Once this line's own pulse is over: R2 to 20, R3 to two characters,
    // and the new mode asked for, all of it for the next line.
    runTo(12, 3, 62);
    outPort(cpc, 0xBC00, 2);
    outPort(cpc, 0xBD00, 20);
    outPort(cpc, 0xBC00, 3);
    outPort(cpc, 0xBD00, 0x82);
    outPort(cpc, 0x7F00, static_cast<uint8_t>(0x8C | to));
    CHECK(c.vlc() == 3 || c.hcc() < 18);
    // After the short pulse: everything back for the usual one at 46.
    runTo(12, 4, 26);
    outPort(cpc, 0xBC00, 2);
    outPort(cpc, 0xBD00, 46);
    outPort(cpc, 0xBC00, 3);
    outPort(cpc, 0xBD00, 0x8E);
    outPort(cpc, 0x7F00, static_cast<uint8_t>(0x8C | from));
    CHECK(c.vlc() == 4 && c.hcc() < 46);
    const uint64_t frame = cpc.monitor().frameNumber();
    while (cpc.monitor().frameNumber() == frame)
        cpc.run(1000);
    const uint32_t* pixels = cpc.monitor().frame();
    // The line with a short black run in the middle of the picture. The
    // character after the pulse is the 21st shown, the 22nd with an ASIC,
    // which blanks from the character R2 names and not from the one before.
    const int after = type == CrtcType::PreAsic || type == CrtcType::AsicPlus ? 22 : 21;
    for (int row = 40; row < 230; ++row) {
        const uint32_t* line = pixels + row * Monitor::kWidth;
        int black = 0;
        for (int x = 64 + 18 * 16; x < 64 + 24 * 16; ++x)
            black += line[x] == kBlack;
        if (black >= 24 && black <= 40) {
            for (int i = 0; i < 17; ++i)
                result.pixels[i] = line[64 + after * 16 + i - 1];
            return result;
        }
    }
    CHECK(false);
    return result;
}

// What a Gate Array shows where it changes mode after a pulse of two
// microseconds (Compendium 9.3.4.2, 9.3.4.3 for the 40010 and 9.3.4.5 for
// the 40226). The black lasts into the character after the pulse: four
// pixels of it with the HD6845S and the MC6845, five with the UM6845R, two
// with the 40226. The change comes on the sixth pixel, the fourth on the
// 40226: what is seen before it, one pixel but with the UM6845R, is still
// in the old mode. From there the new mode takes the byte as the old one
// has shifted it, with zeros coming in, and goes on shifting it its own
// way: a mode shifts as each of its pixels ends, mode 2 at every pixel (its
// first comes a pixel early), mode 1 at the third, fifth and seventh of the
// byte, modes 0 and 3 at the fifth. The byte's place ends a pixel sooner
// in mode 2.
void testModeChangePixels()
{
    auto shifts = [](int mode, int at) { return mode == 2 ? at : mode == 1 ? (at - 1) / 2 : (at - 1) / 4; };
    struct Chip {
        CrtcType type;
        int blackTo, change;
    };
    const Chip chips[] = {{CrtcType::HD6845S, 4, 5}, {CrtcType::UM6845R, 5, 5}, {CrtcType::MC6845, 4, 5}, {CrtcType::PreAsic, 2, 3}};
    for (const Chip& chip : chips) {
        for (int from = 0; from < 4; ++from) {
            for (int to = 0; to < 4; ++to) {
                if (to == from)
                    continue;
                for (const uint8_t byte : {uint8_t(0xB5), uint8_t(0x4E), uint8_t(0xD3)}) {
                    const ModeChange seen = modeChange(chip.type, from, to, byte);
                    uint8_t before[8], after[8];
                    pensOf(from, byte, before);
                    pensOf(to, byte, after);
                    const int second = to == 2 ? 8 : 9;
                    for (int i = 0; i < 17; ++i) {
                        uint32_t want;
                        if (i < chip.blackTo) {
                            want = kBlack;
                        } else if (i < chip.change) {
                            want = seen.colour[before[from == 2 ? i : i - 1]];
                        } else if (i < second) {
                            uint8_t cooked[8];
                            const int done = shifts(from, chip.change) + shifts(to, i) - shifts(to, chip.change);
                            pensOf(to, static_cast<uint8_t>(byte << done), cooked);
                            want = seen.colour[cooked[0]];
                        } else {
                            want = seen.colour[after[(i - second) & 7]];
                        }
                        CHECK_EQ(seen.pixels[i], want);
                    }
                }
            }
        }
    }
}

// A CRTC register write, seen by turning the display off with R8 so that
// the border colour replaces the picture.
void testCrtcTiming(CrtcType type, int outCMicrosecond)
{
    const Probe outC = measure(type, Form::OutC, 0xBD, 0x30, false, kBlack);
    const Probe outN = measure(type, Form::OutN, 0xBD, 0x30, false, kBlack);
    const Probe outi = measure(type, Form::Outi, 0xBD, 0x30, false, kBlack);
    CHECK(outC.row > 0);
    CHECK_EQ(outN.row, outC.row);
    CHECK_EQ(outi.row, outC.row);
    CHECK_EQ(outC.x % 16, 0);
    // OUT (n),A lands on the third microsecond and OUTI on the fifth
    // everywhere; OUT (C),r on the third, or the fourth inside an ASIC.
    CHECK_EQ(outC.x - outN.x, (outCMicrosecond - 3) * 16);
    CHECK_EQ(outi.x - outN.x, 2 * 16);
}

// The screen mode asked for is taken two microseconds into the HSYNC, and
// not at all if the pulse lasts less than that; for as long as the Gate
// Array's own pulse then lasts, six microseconds at most, a mode asked for
// is taken at once (Compendium 9.3.1 and 9.3.2; on a real machine the
// Shaker's "Gate Array moderisation" screen shows the last write that is
// still in time, and its "R3 JIT" screens the picture already in the new
// mode when it comes back after three characters).
void testModeChange()
{
    for (int width : {1, 2, 4, 6, 14}) {
        Crtc crtc;
        GateArray ga;
        Monitor monitor;
        std::vector<uint8_t> ram(0x10000, 0);
        const uint8_t regs[] = {63, 40, 46, static_cast<uint8_t>(0x80 | width), 38, 0, 25, 30, 0, 7};
        for (uint8_t r = 0; r < sizeof regs; ++r) {
            crtc.select(r);
            crtc.write(regs[r]);
        }
        auto tick = [&] {
            crtc.tick();
            ga.sync(crtc, monitor);
            ga.render(crtc, ram.data(), monitor);
        };
        // Go to the start of a line, then ask for mode 1.
        do
            tick();
        while (crtc.hcc() != 0);
        ga.write(0x80 | 0x0C | 1);
        CHECK_EQ(ga.mode(), 0);
        while (crtc.hcc() != 46)
            tick();
        // That tick was the first microsecond of the HSYNC.
        CHECK(crtc.hsync());
        const int taken = width < 2 ? -1 : 48;  // character at which the mode is taken
        while (crtc.hcc() != 60) {
            CHECK_EQ(ga.mode(), taken >= 0 && crtc.hcc() >= taken ? 1 : 0);
            tick();
        }
        while (crtc.hcc() != 20)
            tick();
        CHECK_EQ(ga.mode(), width >= 2 ? 1 : 0);

        // A mode asked for during the pulse is still in time; once the pulse
        // has ended it waits for the next line.
        if (width == 14) {
            while (crtc.hcc() != 51)
                tick();
            ga.write(0x80 | 0x0C | 2);  // during the sixth microsecond
            tick();
            CHECK_EQ(ga.mode(), 2);
            ga.write(0x80 | 0x0C | 0);  // during the seventh
            while (crtc.hcc() != 45) {
                tick();
                CHECK_EQ(ga.mode(), 2);
            }
            while (crtc.hcc() != 52)
                tick();
            CHECK_EQ(ga.mode(), 0);
        }
    }
}

}  // namespace

// A program is interrupted one microsecond after the HSYNC has ended, two
// inside an ASIC: with R3 = 14, 8 and 1 that is 15, 9 and 2 microseconds
// after the character C0 = R2 began on types 0, 1 and 2, and 16, 10 and 3
// on types 3 and 4 (Compendium 27.6.2 and 27.6.5). Measured on a program
// that waits in a HALT.
void testInterruptPosition()
{
    for (const CrtcType type : {CrtcType::HD6845S, CrtcType::UM6845R, CrtcType::MC6845, CrtcType::AsicPlus, CrtcType::PreAsic}) {
        const bool asic = type == CrtcType::AsicPlus || type == CrtcType::PreAsic;
        for (const int width : {14, 8, 1}) {
            Cpc cpc;
            cpc.crtc().setType(type);
            cpc.memory().setRomEnables(false, false);
            const uint8_t crtc[] = {63, 40, 46, static_cast<uint8_t>(0x80 | width), 38, 0, 25, 30, 0, 7};
            for (uint8_t r = 0; r < sizeof crtc; ++r) {
                outPort(cpc, 0xBC00, r);
                outPort(cpc, 0xBD00, crtc[r]);
            }
            outPort(cpc, 0x7F00, 0x8C);
            // IM 1: EI: HALT: JR back to the HALT; the handler is EI: RET.
            Memory& mem = cpc.memory();
            const uint8_t program[] = {0xED, 0x56, 0xFB, 0x76, 0x18, 0xFD};
            for (uint16_t i = 0; i < sizeof program; ++i)
                mem.write(static_cast<uint16_t>(0x4000 + i), program[i]);
            mem.write(0x0038, 0xFB);
            mem.write(0x0039, 0xC9);
            cpc.cpu().pc = 0x4000;
            cpc.cpu().sp = 0x8000;
            cpc.run(5 * Cpc::kFrameMicroseconds);
            uint64_t atR2 = 0;
            int after = -1;
            for (int guard = 0; guard < 100000 && after < 0; ++guard) {
                const uint64_t before = cpc.microseconds();
                if (cpc.crtc().hcc() == 46)
                    atR2 = before;
                cpc.stepInstruction();
                if (cpc.cpu().pc == 0x0038 && atR2 != 0)
                    after = static_cast<int>(before - atR2);
            }
            CHECK_EQ(after, width + (asic ? 2 : 1));
        }
    }
}

int main()
{
    testInterruptPosition();
    testInkTiming(CrtcType::HD6845S, 8);
    testInkTiming(CrtcType::UM6845R, 8);
    testInkTiming(CrtcType::AsicPlus, 5);
    testCrtcTiming(CrtcType::HD6845S, 3);
    testCrtcTiming(CrtcType::AsicPlus, 4);
    testCrtcTiming(CrtcType::PreAsic, 4);
    testCentred();
    testModeTwoEarly();
    testBlanking();
    testModeChangePixels();
    testModeChange();
    return checkSummary("video_timing");
}
