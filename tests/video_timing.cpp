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
Probe measure(CrtcType type, Form form, uint8_t portHigh, uint8_t data, bool allBorder, uint32_t before)
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
    outPort(cpc, 0x7F00, 0x8C);  // mode 0, no ROM
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
    // and, across, the characters that are picture on every machine (with
    // an ASIC the picture sits one character further left). The rest is
    // ordinary border.
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

// The screen mode is taken two microseconds into the HSYNC, and not at all
// if the HSYNC is shorter than that.
void testModeChange()
{
    for (int width : {1, 2, 14}) {
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
        CHECK_EQ(ga.mode(), 0);
        tick();
        CHECK_EQ(ga.mode(), 0);
        tick();
        CHECK_EQ(ga.mode(), width >= 2 ? 1 : 0);
        while (crtc.hcc() != 20)
            tick();
        CHECK_EQ(ga.mode(), width >= 2 ? 1 : 0);
    }
}

}  // namespace

int main()
{
    testInkTiming(CrtcType::HD6845S, 8);
    testInkTiming(CrtcType::UM6845R, 8);
    testInkTiming(CrtcType::AsicPlus, 4);
    testCrtcTiming(CrtcType::HD6845S, 3);
    testCrtcTiming(CrtcType::AsicPlus, 4);
    testCrtcTiming(CrtcType::PreAsic, 4);
    testModeChange();
    return checkSummary("video_timing");
}
