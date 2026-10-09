// The Plus machines: cartridges, the ASIC's lock, the cartridge pages and
// the page of registers it maps, and its palette. Everything is done from
// outside, port by port, on a cartridge made up here: no ROM image is
// needed. What real machines do is in Kevin Thacker's tests (acid_plus).

#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "check.h"
#include "crtc_rig.h"
#include "core/cartridge.h"
#include "core/cpc.h"
#include "core/snapshot.h"

namespace {

using namespace tuxape;
using Bytes = std::vector<uint8_t>;

void chunk(Bytes& file, const char* id, const Bytes& data)
{
    file.insert(file.end(), id, id + 4);
    for (int shift = 0; shift < 32; shift += 8)
        file.push_back(static_cast<uint8_t>(data.size() >> shift));
    file.insert(file.end(), data.begin(), data.end());
    if (data.size() & 1)
        file.push_back(0);
}

void testCprFiles()
{
    Bytes file = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'M', 'S', '!'};
    chunk(file, "fmt ", {});
    chunk(file, "cb00", Bytes(0x4000, 0x11));
    chunk(file, "cb01", Bytes(0x1001, 0x22));  // short, and of an odd length
    chunk(file, "junk", Bytes(7, 0x99));
    chunk(file, "cb03", Bytes(0x5000, 0x44));  // too long
    const auto cartridge = Cartridge::parseCpr(file);
    CHECK(cartridge.has_value());
    if (cartridge) {
        CHECK_EQ(cartridge->pages(), 4);
        CHECK_EQ(cartridge->data[0x0000], 0x11);
        CHECK_EQ(cartridge->data[0x3FFF], 0x11);
        CHECK_EQ(cartridge->data[0x4000], 0x22);
        CHECK_EQ(cartridge->data[0x5000], 0x22);
        CHECK_EQ(cartridge->data[0x5001], 0x00);  // the rest of a short page
        CHECK_EQ(cartridge->data[0x8000], 0xFF);  // a page the file does not have
        CHECK_EQ(cartridge->data[0xC000], 0x44);
        CHECK_EQ(cartridge->data.size(), 0x10000);
    }
    CHECK(!Cartridge::parseCpr(Bytes{}).has_value());
    CHECK(!Cartridge::parseCpr(Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'}).has_value());
    CHECK(!Cartridge::parseCpr(Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'M', 'S', '!'}).has_value());
    // A file cut in the middle of a page keeps what it has of it.
    file.resize(12 + 8 + 8 + 0x100);
    const auto cut = Cartridge::parseCpr(file);
    CHECK(cut && cut->pages() == 1 && cut->data[0xFF] == 0x11 && cut->data[0x100] == 0x00);
}

// A cartridge of `pages` pages, each full of &C0 plus its number.
Cartridge numbered(int pages)
{
    Cartridge cartridge;
    for (int page = 0; page < pages; ++page)
        cartridge.data.insert(cartridge.data.end(), Cartridge::kPageSize, static_cast<uint8_t>(0xC0 + page));
    return cartridge;
}

void sendSequence(Cpc& cpc, uint8_t last, uint8_t after)
{
    for (const uint8_t byte : {0xFF, 0x00, 0xFF, 0x77, 0xB3, 0x51, 0xA8, 0xD4, 0x62, 0x39, 0x9C, 0x46, 0x2B, 0x15, 0x8A})
        cpc.out(0xBC00, byte);
    cpc.out(0xBC00, last);
    cpc.out(0xBC00, after);
}

void unlock(Cpc& cpc)
{
    sendSequence(cpc, 0xCD, 0xEE);
}

void lock(Cpc& cpc)
{
    sendSequence(cpc, 0xA5, 0x00);
}

void testCartridgePages()
{
    Cpc cpc;
    CHECK(!cpc.plus());
    const Cartridge cartridge = numbered(8);
    cpc.setCartridge(&cartridge);
    cpc.reset();
    CHECK(cpc.plus());
    Memory& memory = cpc.memory();

    // Page 0 is the lower ROM. The upper one is not seen until one has been
    // chosen: till then the top 16K is RAM, where No Exit finds its way
    // back from a subroutine called before it has set its stack.
    CHECK_EQ(memory.read(0x0000), 0xC0);
    CHECK(memory.upperRomEnabled());
    memory.write(0xFFFD, 0x3D);
    CHECK_EQ(memory.read(0xFFFD), 0x3D);
    // Once chosen it is page 1, whatever ROM is asked for; ROM 7 is page 3
    // on a machine with a disc drive.
    cpc.out(0xDF00, 0);
    CHECK_EQ(memory.read(0xC000), 0xC1);
    CHECK_EQ(memory.read(0xFFFD), 0xC1);
    cpc.out(0xDF00, 5);
    CHECK_EQ(memory.read(0xC000), 0xC1);
    cpc.out(0xDF00, 7);
    CHECK_EQ(memory.read(0xC000), 0xC3);
    // ROM numbers from 128 name a page of the cartridge.
    cpc.out(0xDF00, 0x86);
    CHECK_EQ(memory.read(0xC000), 0xC6);
    cpc.out(0xDF00, 0x80 + 13);  // pages come round: 13 is 5 on a cartridge of 8
    CHECK_EQ(memory.read(0xC000), 0xC5);
    // The Gate Array's ROM enables still do their work.
    memory.write(0xC000, 0x33);
    cpc.out(0x7F00, 0x8C);
    CHECK_EQ(memory.read(0x0000), 0x00);
    CHECK_EQ(memory.read(0xC000), 0x33);
    cpc.out(0x7F00, 0x80);
    cpc.out(0xDF00, 0);

    // A ROM on an expansion board shows in its slot.
    memory.setUpperRom(7, Bytes(0x4000, 0x77));
    cpc.out(0xDF00, 7);
    CHECK_EQ(memory.read(0xC000), 0x77);
    memory.setUpperRom(7, {});
    cpc.out(0xDF00, 7);
    CHECK_EQ(memory.read(0xC000), 0xC3);

    // Without a disc drive ROM 7 is BASIC like the others.
    cpc.setCartridge(&cartridge, false);
    cpc.out(0xDF00, 7);
    CHECK_EQ(memory.read(0xC000), 0xC1);

    // A reset hides the upper ROM again, until the next choice: Epyx World
    // of Sports and Eerie Forest choose a page and read it at once, without
    // a word to the Gate Array.
    cpc.reset();
    memory.write(0xFFFD, 0x3D);
    CHECK_EQ(memory.read(0xFFFD), 0x3D);
    cpc.out(0xDF00, 0x87);
    CHECK_EQ(memory.read(0xC000), 0xC7);

    // Without a cartridge the machine is a CPC again.
    cpc.setCartridge(nullptr);
    CHECK(!cpc.plus());
    memory.setLowerRom(Bytes(0x4000, 0x4C));
    cpc.reset();
    CHECK_EQ(memory.read(0x0000), 0x4C);
}

void testAsic()
{
    Cpc cpc;
    const Cartridge cartridge = numbered(8);
    cpc.setCartridge(&cartridge);
    cpc.reset();
    Memory& memory = cpc.memory();
    Asic& asic = cpc.asic();
    memory.write(0x4000, 0x99);
    memory.write(0x6400, 0x88);

    // Locked, the ASIC is a Gate Array: &B8 is its third register, with
    // the upper ROM off.
    CHECK(!asic.unlocked());
    cpc.out(0x7F00, 0xB8);
    CHECK_EQ(memory.read(0x4000), 0x99);
    CHECK(!cpc.gateArray().upperRomEnabled());
    cpc.out(0x7F00, 0x80);

    // The sequence unlocks it only when whole; the byte that follows &CD
    // can be any.
    for (const uint8_t byte : {0xFF, 0x00, 0xFF, 0x77, 0xB3, 0x51, 0xA8, 0xD4, 0x62, 0x39, 0x9C, 0x46, 0x2B, 0x15, 0x8A, 0xCD}) {
        CHECK(!asic.unlocked());
        cpc.out(0xBC00, byte);
    }
    CHECK(!asic.unlocked());
    cpc.out(0xBC00, 0x12);
    CHECK(asic.unlocked());
    // A sequence gone wrong half-way changes nothing.
    for (const uint8_t byte : {0xFF, 0x00, 0xFF, 0x77, 0x00, 0x51})
        cpc.out(0xBC00, byte);
    CHECK(asic.unlocked());
    // More than one zero may start it: Switchblade sends two, and locks
    // nothing by sending 238 bytes of its own code after the key.
    lock(cpc);
    CHECK(!asic.unlocked());
    for (const uint8_t byte : {0xFF, 0x00, 0x00, 0xFF, 0x77, 0xB3, 0x51, 0xA8, 0xD4, 0x62, 0x39, 0x9C, 0x46, 0x2B, 0x15, 0x8A, 0xCD, 0xEE,
                               0xCD, 0x4A, 0x1D, 0x21, 0xEC, 0x1F, 0x11, 0x22, 0x64, 0x01, 0x1E, 0x00, 0xED, 0xB0})
        cpc.out(0xBC00, byte);
    CHECK(asic.unlocked());

    // RMR2: the page of registers at &4000.
    cpc.out(0x7F00, 0xB8);
    CHECK(cpc.gateArray().upperRomEnabled());  // not the Gate Array's register any more
    CHECK_EQ(asic.rmr2(), 0x18);
    CHECK_EQ(memory.read(0x4000), 0x00);
    // Sprites' pixels are four bits wide.
    memory.write(0x4000, 0xAB);
    CHECK_EQ(memory.read(0x4000), 0x0B);
    memory.write(0x4FFF, 0xF7);
    CHECK_EQ(asic.spritePixel(15, 15, 15), 7);
    CHECK_EQ(asic.spritePixel(0, 0, 0), 0x0B);

    // The palette: red and blue in the first byte, green in the second.
    memory.write(0x6400, 0x5A);
    memory.write(0x6401, 0xFF);
    CHECK_EQ(memory.read(0x6400), 0x5A);
    CHECK_EQ(memory.read(0x6401), 0x0F);
    CHECK_EQ(asic.colour(0), 0xF5A);
    memory.write(0x6420, 0x0F);  // the border: blue
    CHECK_EQ(asic.colour(16), 0x00F);
    memory.write(0x6422, 0xF0);  // the sprites' colour 1: red
    CHECK_EQ(asic.colour(17), 0x0F0);
    CHECK_EQ(cpc.gateArray().spriteColour(1), 0xFFFF0000);
    // A colour set the CPC's way lands there too, as twelve bits.
    cpc.out(0x7F00, 0x01);
    cpc.out(0x7F00, 0x4B);  // bright white
    CHECK_EQ(asic.colour(1), 0xFFF);
    CHECK_EQ(memory.read(0x6402), 0xFF);
    CHECK_EQ(memory.read(0x6403), 0x0F);
    cpc.out(0x7F00, 0x10);
    cpc.out(0x7F00, 0x56);  // the border green: half-way is 6 of 15
    CHECK_EQ(asic.colour(16), 0x600);
    CHECK_EQ(GateArray::plusColour(0x14), 0x000);
    CHECK_EQ(GateArray::plusColour(0x0C), 0x0F0);  // bright red
    CHECK_EQ(GateArray::monitorColour12(0x0F0, MonitorKind::Colour, 0), 0xFFFF0000);
    CHECK_EQ(GateArray::monitorColour12(0x842, MonitorKind::Colour, 0), 0xFF448822);
    CHECK_EQ(GateArray::monitorColour12(0xFFF, MonitorKind::Green, 0), 0xFF00FF00);

    // Sprites: where they are reads back, four bytes on as well; a
    // negative place reads with all its upper bits set.
    memory.write(0x6008, 0x34);
    memory.write(0x6009, 0x02);
    memory.write(0x600A, 0x56);
    memory.write(0x600B, 0x00);
    CHECK_EQ(asic.sprite(1).x, 0x234);
    CHECK_EQ(asic.sprite(1).y, 0x56);
    CHECK_EQ(memory.read(0x6009), 0x02);
    CHECK_EQ(memory.read(0x600C), 0x34);
    CHECK_EQ(memory.read(0x600E), 0x56);
    memory.write(0x6009, 0x03);
    memory.write(0x600B, 0x01);
    CHECK_EQ(asic.sprite(1).x, -204);
    CHECK_EQ(asic.sprite(1).y, -170);
    CHECK_EQ(memory.read(0x6009), 0xFF);
    CHECK_EQ(memory.read(0x600B), 0xFF);
    // The size: none, and the sprite is not shown.
    CHECK(!asic.spritesShown());
    memory.write(0x600C, 0x0D);  // three times across (pixels of 4), once down
    CHECK_EQ(asic.sprite(1).magX, 4);
    CHECK_EQ(asic.sprite(1).magY, 1);
    CHECK(asic.spritesShown());
    CHECK_EQ(memory.read(0x600C), 0x34);  // not read back
    memory.write(0x600C, 0x08);  // across only: nothing to show
    CHECK(!asic.spritesShown());

    // The raster's registers.
    memory.write(0x6800, 100);
    memory.write(0x6801, 150);
    memory.write(0x6802, 0x30);
    memory.write(0x6803, 0x40);
    memory.write(0x6804, 0x93);
    memory.write(0x6805, 0x20);
    CHECK_EQ(asic.rasterInterruptLine(), 100);
    CHECK_EQ(asic.splitLine(), 150);
    CHECK_EQ(asic.splitAddress(), 0x3040);
    CHECK_EQ(asic.scroll(), 0x93);
    CHECK_EQ(asic.interruptVector(), 0x20);
    // The sound channels' status reads the same all over its block.
    memory.write(0x6C0F, 0x05);
    CHECK_EQ(memory.read(0x6C0F), 0x05);
    CHECK_EQ(memory.read(0x6C00), 0x05);
    CHECK_EQ(memory.read(0x6C07), 0x05);

    // The lower ROM elsewhere, and another page of the cartridge as lower ROM.
    cpc.out(0x7F00, 0xA8 + 2);
    CHECK_EQ(memory.read(0x4000), 0xC2);
    CHECK_EQ(memory.read(0x0000), 0x00);  // RAM
    cpc.out(0x7F00, 0xB0 + 5);
    CHECK_EQ(memory.read(0x8000), 0xC5);
    CHECK_EQ(memory.read(0x4000), 0x99);
    cpc.out(0x7F00, 0xA0 + 4);
    CHECK_EQ(memory.read(0x0000), 0xC4);
    CHECK_EQ(memory.read(0x4000), 0x99);  // what was written under the registers never went to RAM
    CHECK_EQ(memory.read(0x6400), 0x88);

    // Locked again, RMR2 is out of reach, but what it set stays: the
    // registers, shown, can still be used.
    cpc.out(0x7F00, 0xB8);
    lock(cpc);
    CHECK(!asic.unlocked());
    CHECK_EQ(memory.read(0x4000), 0x0B);
    memory.write(0x6400, 0x11);
    CHECK_EQ(asic.colour(0), 0xF11);
    cpc.out(0x7F00, 0xA0);  // the Gate Array's register now: nothing moves
    CHECK_EQ(memory.read(0x4000), 0x0B);
    unlock(cpc);
    cpc.out(0x7F00, 0xA0);
    CHECK_EQ(memory.read(0x4000), 0x99);

    // A reset locks the ASIC, puts the cartridge back and blacks the palette.
    cpc.out(0x7F00, 0xB8 + 3);
    cpc.reset();
    CHECK(!asic.unlocked());
    CHECK_EQ(asic.rmr2(), 0);
    CHECK_EQ(memory.read(0x0000), 0xC0);
    CHECK_EQ(memory.read(0x4000), 0x99);
    CHECK_EQ(asic.colour(0), 0);
    CHECK_EQ(asic.spritePixel(0, 0, 0), 0);

    // On a CPC nothing of this exists: the sequence does nothing.
    Cpc plain;
    unlock(plain);
    CHECK(!plain.asic().unlocked());
    plain.memory().write(0x4000, 0x55);
    plain.out(0x7F00, 0xB8);
    CHECK_EQ(plain.memory().read(0x4000), 0x55);
}

// The joystick's line read the way a cartridge written on a Plus may read
// it: the line is picked first, port A made an input afterwards.
uint8_t joystickLinePickedFirst(Cpc& cpc)
{
    cpc.out(0xF400, 0x0E);  // the sound chip's register 14: the keyboard
    cpc.out(0xF600, 0xC0);
    cpc.out(0xF600, 0x00);
    cpc.out(0xF600, 0x49);  // read it, line 9...
    cpc.out(0xF700, 0x92);  // ...and only now port A as an input
    const uint8_t line = cpc.in(0xF400);
    cpc.out(0xF700, 0x82);
    cpc.out(0xF600, 0x00);
    return line;
}

// The ASIC's stand-in for the 8255: port B always an input, port C always
// an output, port A read through the control register's address, and
// latches that setting the mode leaves alone.
void testPpi()
{
    Cpc cpc;
    const Cartridge cartridge = numbered(8);
    cpc.setCartridge(&cartridge);
    cpc.reset();
    cpc.out(0xF700, 0x80);  // all three ports asked to be outputs
    cpc.out(0xF500, 0x00);
    CHECK_EQ(cpc.in(0xF500) & 0x5E, 0x5E);  // port B still reads its links
    // ... and, with no tape playing, the cassette line high: &DE where a
    // CPC has &5E (the Shaker's "VSYNC conditions" on real machines).
    CHECK_EQ(cpc.in(0xF500) & 0xDE, 0xDE);
    cpc.out(0xF700, 0x9B);  // all three asked to be inputs
    cpc.out(0xF600, 0x25);
    CHECK_EQ(cpc.in(0xF600), 0x25);  // port C still gives back what was written
    CHECK_EQ(cpc.in(0xF700), 0xFF);  // port A's pins, with nothing driving them
    cpc.out(0xF700, 0x82);
    cpc.out(0xF400, 0x38);
    CHECK_EQ(cpc.in(0xF700), 0x00);  // port A an output
    // Port C keeps the keyboard's line through a change of mode.
    cpc.out(0xF600, 0x49);
    cpc.out(0xF700, 0x92);
    CHECK_EQ(cpc.in(0xF600), 0x49);
    cpc.out(0xF700, 0x82);
    cpc.out(0xF600, 0x00);
    cpc.keyboard().set(CpcKey::JoyRight, true);
    CHECK_EQ(joystickLinePickedFirst(cpc), 0xF7);
    cpc.keyboard().set(CpcKey::JoyRight, false);
    cpc.keyboard().set(CpcKey::JoyFire2, true);
    CHECK_EQ(joystickLinePickedFirst(cpc), 0xEF);

    // A CPC's 8255 does as it is told, and a change of mode empties its
    // latches: the line picked too soon is lost.
    Cpc plain;
    plain.out(0xF700, 0x80);
    plain.out(0xF500, 0x00);
    CHECK_EQ(plain.in(0xF500), 0x00);
    plain.out(0xF700, 0x9B);
    CHECK_EQ(plain.in(0xF500) & 0xDE, 0x5E);
    plain.out(0xF600, 0x25);
    CHECK_EQ(plain.in(0xF600), 0xFF);
    CHECK_EQ(plain.in(0xF700), 0xFF);
    plain.out(0xF700, 0x82);
    plain.out(0xF600, 0x49);
    plain.out(0xF700, 0x82);
    CHECK_EQ(plain.in(0xF600), 0x00);
    plain.keyboard().set(CpcKey::JoyRight, true);
    CHECK_EQ(joystickLinePickedFirst(plain), 0xFF);
    // ...unless it is given a Plus's ways (WinAPE's "Plus PPI Emulation").
    plain.setPlusPpi(true);
    CHECK_EQ(joystickLinePickedFirst(plain), 0xF7);
}

// A Plus with a cartridge that does nothing (DI, and a jump to itself), the
// ASIC unlocked with its registers shown, and the firmware's screen.
struct PlusMachine {
    Cpc cpc;
    PlusMachine()
    {
        Cartridge cartridge = numbered(8);
        cartridge.data[0] = 0xF3;
        cartridge.data[1] = 0x18;
        cartridge.data[2] = 0xFE;
        cpc.setCartridge(&cartridge);
        cpc.crtc().setType(CrtcType::AsicPlus);
        cpc.reset();
        static const uint8_t screen[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0};
        for (int r = 0; r < 14; ++r) {
            cpc.out(0xBC00, static_cast<uint8_t>(r));
            cpc.out(0xBD00, screen[r]);
        }
        unlock(cpc);
        cpc.out(0x7F00, 0xB8);
        // A few frames for the picture to settle on its new registers.
        for (int frame = 0; frame < 6; ++frame)
            cpc.runFrame();
    }
    void write(uint16_t address, uint8_t value) { cpc.memory().write(address, value); }
    // The box around the picture's pixels of one colour: left, top, width,
    // height; all zero if there is none.
    struct Box {
        int x = 0, y = 0, width = 0, height = 0;
    };
    // The last whole picture, drawn after everything asked for so far.
    const uint32_t* picture()
    {
        for (int frame = 0; frame < 3; ++frame)
            cpc.runFrame();
        return cpc.monitor().frame();
    }
    Box find(uint32_t colour)
    {
        const uint32_t* frame = picture();
        int left = Monitor::kWidth, right = -1, top = Monitor::kHeight, bottom = -1;
        for (int y = 0; y < Monitor::kHeight; ++y)
            for (int x = 0; x < Monitor::kWidth; ++x)
                if (frame[y * Monitor::kWidth + x] == colour) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        if (right < 0)
            return {};
        return {left, top, right - left + 1, bottom - top + 1};
    }
};

constexpr uint32_t kRed = 0xFFFF0000, kGreen = 0xFF00FF00, kBlue = 0xFF0000FF, kWhite = 0xFFFFFFFF;

void testSprites()
{
    PlusMachine m;
    // A blue screen in a green border, and the sprites' first two colours
    // red and white.
    m.write(0x6400, 0x0F);
    m.write(0x6420, 0x00);
    m.write(0x6421, 0x0F);
    m.write(0x6422, 0xF0);
    m.write(0x6424, 0xFF);
    m.write(0x6425, 0x0F);
    const PlusMachine::Box screen = m.find(kBlue);
    CHECK_EQ(screen.width, 640);
    CHECK_EQ(screen.height, 200);
    CHECK_EQ(m.find(kRed).width, 0);
    CHECK(m.find(kGreen).width > 640);

    // Sprite 0, all of colour 1, at its natural size: 16 pixels by 16
    // lines, counted from the screen's top left.
    for (int i = 0; i < 256; ++i)
        m.write(static_cast<uint16_t>(0x4000 + i), 1);
    m.write(0x6000, 32);
    m.write(0x6002, 16);
    m.write(0x6004, 0x05);
    PlusMachine::Box red = m.find(kRed);
    CHECK_EQ(red.x, screen.x + 32);
    CHECK_EQ(red.y, screen.y + 16);
    CHECK_EQ(red.width, 16);
    CHECK_EQ(red.height, 16);
    // Twice as wide, four times as high.
    m.write(0x6004, 0x0B);
    red = m.find(kRed);
    CHECK_EQ(red.x, screen.x + 32);
    CHECK_EQ(red.width, 32);
    CHECK_EQ(red.height, 64);
    // Colour 0 lets the screen show: a hole in the middle.
    m.write(0x6004, 0x05);
    for (int y = 4; y < 12; ++y)
        for (int x = 4; x < 12; ++x)
            m.write(static_cast<uint16_t>(0x4000 + y * 16 + x), 0);
    const uint32_t* frame = m.picture();
    CHECK_EQ(frame[(screen.y + 16 + 8) * Monitor::kWidth + screen.x + 32 + 8], kBlue);
    CHECK_EQ(frame[(screen.y + 16 + 2) * Monitor::kWidth + screen.x + 32 + 8], kRed);

    // A sprite of a lower number goes over one of a higher number.
    for (int i = 0; i < 256; ++i)
        m.write(static_cast<uint16_t>(0x4100 + i), 2);
    m.write(0x6008, 40);
    m.write(0x600A, 24);
    m.write(0x600C, 0x05);
    const PlusMachine::Box white = m.find(kWhite);
    CHECK_EQ(white.x, screen.x + 40);
    CHECK_EQ(white.width, 16);
    frame = m.cpc.monitor().frame();
    CHECK_EQ(frame[(screen.y + 30) * Monitor::kWidth + screen.x + 42], kRed);    // sprite 0, below its hole
    CHECK_EQ(frame[(screen.y + 30) * Monitor::kWidth + screen.x + 50], kWhite);  // sprite 1, beyond it
    CHECK_EQ(frame[(screen.y + 26) * Monitor::kWidth + screen.x + 42], kWhite);  // through sprite 0's hole
    m.write(0x600C, 0);

    // Off the screen's edge, a sprite is cut: it does not show on the
    // border.
    m.write(0x6000, 0xF8);  // X = -8
    m.write(0x6001, 0x03);
    m.write(0x6002, 0xFC);  // Y = -4
    m.write(0x6003, 0x01);
    red = m.find(kRed);
    CHECK_EQ(red.x, screen.x);
    CHECK_EQ(red.y, screen.y);
    CHECK_EQ(red.width, 8);
    CHECK_EQ(red.height, 12);
    // Hidden.
    m.write(0x6004, 0);
    CHECK_EQ(m.find(kRed).width, 0);
}

// The screen split in two, and scrolled by the pixel.
void testSplitAndScroll()
{
    PlusMachine m;
    // Mode 2 would do; mode 1 is what the machine starts in: pen 0 blue,
    // pen 1 white, border black. The screen at &C000 is blank but for a
    // white byte at the start of its first row; the one at &4000 (its RAM
    // lies under the registers) is all white.
    m.write(0x6400, 0x0F);
    m.write(0x6402, 0xFF);
    m.write(0x6403, 0x0F);
    m.cpc.out(0x7F00, 0x8D);  // mode 1, ROMs out of the way
    for (int line = 0; line < 8; ++line)
        m.cpc.memory().baseRam()[0xC000 + line * 0x800] = 0xF0;
    for (int i = 0; i < 0x4000; ++i)
        m.cpc.memory().baseRam()[0x4000 + i] = 0xF0;
    const PlusMachine::Box screen = m.find(kBlue);
    PlusMachine::Box white = m.find(kWhite);
    CHECK_EQ(white.x, screen.x);
    CHECK_EQ(white.y, screen.y);
    CHECK_EQ(white.width, 8);
    CHECK_EQ(white.height, 8);

    // After line 100 the picture comes from &4000.
    m.write(0x6801, 100);
    m.write(0x6802, 0x10);
    m.write(0x6803, 0x00);
    const uint32_t* frame = m.picture();
    CHECK_EQ(frame[(screen.y + 100) * Monitor::kWidth + screen.x + 100], kBlue);
    CHECK_EQ(frame[(screen.y + 101) * Monitor::kWidth + screen.x + 100], kWhite);
    CHECK_EQ(frame[(screen.y + 199) * Monitor::kWidth + screen.x + 100], kWhite);
    // Scrolled to the right, that white picture still ends where the
    // display does: what is pushed past the edge is lost.
    m.write(0x6804, 0x0B);
    frame = m.picture();
    CHECK_EQ(frame[(screen.y + 150) * Monitor::kWidth + screen.x + screen.width - 1], kWhite);
    CHECK(frame[(screen.y + 150) * Monitor::kWidth + screen.x + screen.width] != kWhite);
    CHECK_EQ(frame[(screen.y + 150) * Monitor::kWidth + screen.x + screen.width],
             frame[(screen.y + 150) * Monitor::kWidth + screen.x + screen.width + 40]);
    m.write(0x6804, 0);
    m.write(0x6801, 0);

    // The scroll: five pixels to the right, then three lines up.
    m.write(0x6804, 0x05);
    white = m.find(kWhite);
    CHECK_EQ(white.x, screen.x + 5);
    CHECK_EQ(white.width, 8);
    CHECK_EQ(white.y, screen.y);
    // Each row then ends on the first lines of the row below it.
    m.write(0x6804, 0x30);
    frame = m.picture();
    CHECK_EQ(frame[screen.y * Monitor::kWidth + screen.x + 2], kWhite);
    CHECK_EQ(frame[(screen.y + 4) * Monitor::kWidth + screen.x + 2], kWhite);
    CHECK_EQ(frame[(screen.y + 5) * Monitor::kWidth + screen.x + 2], kBlue);
    CHECK_EQ(frame[(screen.y + 8) * Monitor::kWidth + screen.x + 2], kBlue);
    // The border over the first character hides where the picture comes in.
    m.write(0x6804, 0x80);
    CHECK_EQ(m.find(kWhite).width, 0);
    CHECK_EQ(m.find(kBlue).x, screen.x + 16);
}

// When the ASIC looks at the split line: as that line's display ends, for
// the line that follows, and as a frame's last line ends; what is written
// counts a microsecond later. The figures are those of Kevin Thacker's
// "splittrig" programs on a real machine: the split shows when the line is
// set at the latest 2 characters before C0 = R1 (or before the frame's end).
void testSplitMoment()
{
    for (const int c0 : {37, 38, 39}) {
        Rig rig(CrtcType::AsicPlus);
        rig.seek(12, 3, c0);                 // line 99
        rig.crtc.setSplit(99, 0x1800);
        rig.nextLine();
        CHECK_EQ(rig.crtc.ma() == 0x1800, c0 <= 38);
        // Too late for this line, and the line is gone: no split this frame.
        rig.nextLine();
        CHECK_EQ(rig.crtc.ma() == 0x1800, c0 <= 38);
    }
    // Set for nine microseconds only, as the test programs do.
    {
        Rig rig(CrtcType::AsicPlus);
        rig.seek(12, 3, 30);
        rig.crtc.setSplit(99, 0x1800);
        rig.to(39);
        rig.crtc.setSplit(0, 0x1800);
        rig.nextLine();
        CHECK_EQ(rig.crtc.ma(), 0x1800);
    }
    // On the last line of a frame the new frame starts from the split's
    // address instead of R12/R13, if the line is still set as it ends.
    for (const int c0 : {40, 61, 62, 63}) {
        Rig rig(CrtcType::AsicPlus);
        rig.set(4, 18);
        rig.seek(18, 7, c0);                 // line 151
        rig.crtc.setSplit(151, 0x1800);
        if (c0 == 40) {
            rig.to(50);                      // seen as the display ended, gone by the end of the line
            rig.crtc.setSplit(0, 0x1800);
        }
        rig.nextLine();
        CHECK(rig.crtc.vcc() == 0 && rig.crtc.vlc() == 0);
        CHECK_EQ(rig.crtc.ma() == 0x1800, c0 == 61 || c0 == 62);
    }
}

// The parts of the register page with nothing behind them: a read there
// gives what the bus last carried, the last byte the instruction fetched.
void testFloatingBus()
{
    PlusMachine m;
    Cpc& cpc = m.cpc;
    cpc.memory().baseRam()[0x8000] = 0x7E;   // as if LD A,(HL) had just been fetched
    cpc.memory().baseRam()[0x5123] = 0x99;   // the RAM under the page
    cpc.cpu().pc = 0x8001;
    for (const uint16_t address : {0x5123, 0x60A0, 0x6300, 0x6440, 0x67FF, 0x6800, 0x6805, 0x6810, 0x6BFF, 0x6C10, 0x7FFF})
        CHECK_EQ(cpc.read(address), 0x7E);
    cpc.memory().baseRam()[0x8000] = 0x50;   // the high byte of LD A,(&5000)
    CHECK_EQ(cpc.read(0x5000), 0x50);
    // What is there answers for itself.
    m.write(0x6400, 0x5A);
    CHECK_EQ(cpc.read(0x6400), 0x5A);
    CHECK_EQ(cpc.read(0x6808), 0x3F);
    CHECK_EQ(cpc.read(0x6C0F) & 0x0F, 0x00);
    // With the page put away, the RAM is back.
    cpc.out(0x7F00, 0xA0);
    CHECK_EQ(cpc.read(0x5123), 0x99);
}

// The interrupt on a line of the program's choice, and the vector the ASIC
// gives for it.
void testRasterInterrupt()
{
    PlusMachine m;
    Cpc& cpc = m.cpc;
    auto nextInterrupt = [&] {
        // Interrupts are off (DI): one that is asked for stays asked for.
        for (int i = 0; i < 2 * 312 * 64; ++i) {
            cpc.run(1);
            if (cpc.gateArray().interruptRequested())
                return static_cast<int>(cpc.crtc().asicLine());
        }
        return -1;
    };
    // A CPC's interrupts come every 52 lines.
    const int first = nextInterrupt();
    CHECK(first >= 0);
    cpc.irqAck();
    const int second = nextInterrupt();
    cpc.irqAck();
    CHECK(second >= 0 && second != first);

    // Until the vector register is written to, every bit of it is set but
    // the lowest: the raster's interrupt puts &FE on the bus. In interrupt
    // mode 0, where a cartridge starts, that is a harmless CP n.
    CHECK_EQ(cpc.asic().interruptVector(), 0xFE);
    nextInterrupt();
    CHECK_EQ(cpc.irqAck(), 0xFE);

    // With a line set, there is the one interrupt a frame, on that line,
    // as its HSYNC starts.
    m.write(0x6800, 77);
    m.write(0x6805, 0x51);
    for (int frame = 0; frame < 3; ++frame) {
        CHECK_EQ(nextInterrupt(), 77);
        // R2 is 46, and the Gate Array sees the HSYNC a character late; the
        // instruction under way (three microseconds) then has to end.
        CHECK(cpc.crtc().hcc() >= 47 && cpc.crtc().hcc() <= 49);
        CHECK_EQ(cpc.memory().read(0x6C0F) & 0x80, 0x80);
        // The vector: the register's upper bits, with 11 for the raster.
        CHECK_EQ(cpc.irqAck(), 0x56);
        CHECK(!cpc.gateArray().interruptRequested());
        // Bit 7 of the status goes on saying that the interrupt taken last
        // was the raster's: a handler reads it to know why it was called.
        CHECK_EQ(cpc.memory().read(0x6C0F) & 0x80, 0x80);
    }
    // How many in a frame: one, whatever the line. The ASIC numbers lines
    // with six bits of C4, so that lines 256 and up, which only differ
    // from 0 to 55 by the bit a register of eight cannot hold, match
    // nothing ("pritest" on a real machine; counted in pairs before).
    auto inOneFrame = [&] {
        int count = 0;
        const uint64_t start = cpc.microseconds();
        while (cpc.microseconds() - start < 312 * 64) {
            cpc.run(1);
            if (cpc.gateArray().interruptRequested()) {
                cpc.irqAck();
                ++count;
            }
        }
        return count;
    };
    for (const int line : {10, 77, 200}) {
        m.write(0x6800, static_cast<uint8_t>(line));
        nextInterrupt();
        cpc.irqAck();
        CHECK_EQ(inOneFrame(), 1);
    }
    // Two, though, when the HSYNC runs on into the next line: the line
    // begins with the pulse still on, and then has its own. From R2 = 50
    // with a pulse of 14, and with R2 = 63 too.
    cpc.out(0xBC00, 3);
    cpc.out(0xBD00, 0x8E);
    for (const int r2 : {49, 50, 63}) {
        cpc.out(0xBC00, 2);
        cpc.out(0xBD00, static_cast<uint8_t>(r2));
        m.write(0x6800, 100);
        for (const uint64_t start = cpc.microseconds(); cpc.microseconds() - start < 3 * 312 * 64;) {
            cpc.run(1);
            if (cpc.gateArray().interruptRequested())
                cpc.irqAck();
        }
        CHECK_EQ(inOneFrame(), r2 >= 50 ? 2 : 1);
    }
    cpc.out(0xBC00, 2);
    cpc.out(0xBD00, 46);

    // Back to the CPC's.
    m.write(0x6800, 0);
    const int back = nextInterrupt();
    CHECK(back >= 0 && back != 77);
}

// The sound channels fed from memory: one instruction a line.
void testSoundChannels()
{
    PlusMachine m;
    Cpc& cpc = m.cpc;
    auto list = [&](uint16_t at, std::initializer_list<uint16_t> instructions) {
        for (const uint16_t instruction : instructions) {
            cpc.memory().baseRam()[at++] = static_cast<uint8_t>(instruction);
            cpc.memory().baseRam()[at++] = static_cast<uint8_t>(instruction >> 8);
        }
    };
    // To the start of the next HSYNC, or of the one after..., and past the
    // channels' round, which takes a few microseconds from there.
    auto lines = [&](int count) {
        for (int i = 0; i < count; ++i) {
            while (cpc.crtc().hsync())
                cpc.run(1);
            while (!cpc.crtc().hsync())
                cpc.run(1);
            cpc.run(9);
        }
    };
    // The Gate Array's own interrupts are not what is looked at here.
    auto settle = [&] {
        if (cpc.gateArray().interruptRequested())
            cpc.irqAck();
    };
    // A machine starts with bit 0 of the vector register clear: a
    // channel's interrupt goes by itself once taken. (The "CRTC 3" demo
    // never writes the register and answers such an interrupt with EI,
    // RET.)
    list(0xA000, {0x4030});
    m.write(0x6C08, 0x00);
    m.write(0x6C09, 0xA0);
    m.write(0x6C0A, 0x00);
    m.write(0x6C0F, 0x04);
    lines(1);
    CHECK_EQ(cpc.memory().read(0x6C0F) & 0x7F, 0x10);
    settle();
    CHECK(cpc.irq());
    CHECK_EQ(cpc.irqAck(), 0xF8);  // channel 2's vector
    CHECK_EQ(cpc.memory().read(0x6C0F) & 0x7F, 0x00);
    CHECK(!cpc.irq());

    // Volume 15 on channel A, a tone, a pause of two lines, then a loop of
    // two instructions gone through twice, another register, an interrupt,
    // and the end.
    list(0x8000, {0x080F, 0x0055, 0x1002, 0x2002, 0x0101, 0x4001, 0x0C77, 0x4010, 0x4020, 0x0AFF});
    m.write(0x6C00, 0x01);  // the address's lowest bit does not count
    m.write(0x6C01, 0x80);
    m.write(0x6C02, 0x00);
    m.write(0x6805, 0x21);  // the vector; bit 0: interrupts stay until cleared
    CHECK_EQ(cpc.psg().reg(8), 0);
    lines(3);
    CHECK_EQ(cpc.psg().reg(8), 0);  // not without its bit in the status register
    m.write(0x6C0F, 0x01);
    lines(1);
    CHECK_EQ(cpc.psg().reg(8), 0x0F);
    CHECK_EQ(cpc.psg().reg(0), 0x00);
    lines(1);
    CHECK_EQ(cpc.psg().reg(0), 0x55);
    lines(1);  // the pause is read: what follows comes two lines on
    lines(1);
    CHECK_EQ(cpc.psg().reg(1), 0x00);
    lines(2);  // the loop's start is noted, and its first instruction done
    CHECK_EQ(cpc.psg().reg(1), 0x01);
    cpc.psg().setRegister(1, 0);
    lines(2);  // back once: twice through in all
    CHECK_EQ(cpc.psg().reg(1), 0x01);
    CHECK_EQ(cpc.psg().reg(12), 0x00);
    lines(2);  // out of the loop
    CHECK_EQ(cpc.psg().reg(12), 0x77);
    CHECK_EQ(cpc.memory().read(0x6C0F) & 0x7F, 0x01);
    lines(1);
    // The interrupt: asked for until its flag is written to.
    CHECK_EQ(cpc.memory().read(0x6C0F) & 0x7F, 0x41);
    settle();
    CHECK(cpc.irq());
    CHECK_EQ(cpc.irqAck(), 0x24);  // the vector says channel 0 (bit 0 never shows)
    CHECK(cpc.irq());
    m.write(0x6C0F, 0x41);
    CHECK(!cpc.irq());
    lines(1);
    // Stopped: its bit is gone, and nothing more is taken from the list.
    CHECK_EQ(cpc.memory().read(0x6C0F) & 0x7F, 0x00);
    lines(3);
    CHECK_EQ(cpc.psg().reg(10), 0x00);

    // With bit 0 of the vector register clear, taking the interrupt takes
    // the flag down. Channel 1's vector is between the others'.
    list(0x9000, {0x4010, 0x4020});
    m.write(0x6C04, 0x00);
    m.write(0x6C05, 0x90);
    m.write(0x6805, 0x20);
    m.write(0x6C0F, 0x02);
    lines(1);
    CHECK_EQ(cpc.memory().read(0x6C0F) & 0x7F, 0x22);
    settle();
    CHECK_EQ(cpc.irqAck(), 0x22);
    CHECK_EQ(cpc.memory().read(0x6C0F) & 0x7F, 0x02);
    settle();
    CHECK(!cpc.irq());
    lines(1);

    // A pause counts in units of the prescaler plus one lines: here two
    // units of three lines.
    list(0xA000, {0x1002, 0x0203, 0x4020});
    m.write(0x6C08, 0x00);
    m.write(0x6C09, 0xA0);
    m.write(0x6C0A, 0x02);
    m.write(0x6C0F, 0x04);
    lines(6);
    CHECK_EQ(cpc.psg().reg(2), 0x00);
    lines(1);
    CHECK_EQ(cpc.psg().reg(2), 0x03);
}

// The channels' round within the line. Where a channel's interrupt comes
// is what Kevin Thacker's "dmatiming" marks on the screen, with what a real
// machine shows for fourteen set-ups. The rules checked here give twelve of
// them; the two others have two and three interrupts in one round, and come
// three and four microseconds later on a real machine for a reason not
// understood.
void testSoundChannelTiming()
{
    PlusMachine m;
    Cpc& cpc = m.cpc;
    auto list = [&](uint16_t at, std::initializer_list<uint16_t> instructions) {
        for (const uint16_t instruction : instructions) {
            cpc.memory().baseRam()[at++] = static_cast<uint8_t>(instruction);
            cpc.memory().baseRam()[at++] = static_cast<uint8_t>(instruction >> 8);
        }
    };
    // The processor walks through NOPs, a microsecond each, with its
    // interrupts off.
    std::memset(cpc.memory().baseRam() + 0xA000, 0, 0x2000);
    auto step = [&](int microseconds = 1) {
        cpc.cpu().pc = 0xA000;
        cpc.run(microseconds);
    };
    // To the first microsecond of the next HSYNC.
    auto toHsync = [&] {
        while (cpc.crtc().hsync())
            step();
        while (!cpc.crtc().hsync())
            step();
    };
    auto status = [&] { return cpc.memory().read(0x6C0F) & 0x7F; };
    // The three lists, and the channels given turned on well before the
    // HSYNC; then the microseconds from its start to the first of the
    // interrupts looked for (-1: none on that line).
    const uint16_t kLists[3] = {0x8000, 0x8100, 0x8200};
    auto point = [&] {
        m.write(0x6C0F, 0x70);
        for (int channel = 0; channel < 3; ++channel) {
            m.write(static_cast<uint16_t>(0x6C00 + channel * 4), static_cast<uint8_t>(kLists[channel]));
            m.write(static_cast<uint16_t>(0x6C01 + channel * 4), static_cast<uint8_t>(kLists[channel] >> 8));
            m.write(static_cast<uint16_t>(0x6C02 + channel * 4), 0);
        }
    };
    auto wait = [&](int flags) {
        for (int after = 0; after < 50; ++after) {
            if (status() & flags)
                return after;
            step();
        }
        return -1;
    };
    auto interruptAfter = [&](int channels, int flags) {
        point();
        toHsync();
        step(20);
        m.write(0x6C0F, static_cast<uint8_t>(channels));
        toHsync();
        return wait(flags);
    };
    const uint16_t kInterrupt = 0x4030;  // ask for an interrupt and stop
    const uint16_t kWrite = 0x0855;      // a register of the sound chip
    const uint16_t kNothing = 0x4000;
    const uint16_t kStop = 0x4020;
    m.write(0x6805, 0x01);  // interrupts stay until cleared

    // A channel on its own: the same moment for the three of them.
    list(kLists[0], {kInterrupt});
    list(kLists[1], {kInterrupt});
    list(kLists[2], {kInterrupt});
    const int alone = interruptAfter(0x01, 0x40);
    CHECK_EQ(alone, 5);
    CHECK_EQ(interruptAfter(0x02, 0x20), alone);
    CHECK_EQ(interruptAfter(0x04, 0x10), alone);
    // Behind a write to the sound chip: nine microseconds later; behind
    // two, eighteen.
    list(kLists[0], {kWrite, kStop});
    CHECK_EQ(interruptAfter(0x03, 0x20), alone + 9);
    list(kLists[1], {kWrite, kStop});
    CHECK_EQ(interruptAfter(0x06, 0x10), alone + 9);
    CHECK_EQ(interruptAfter(0x07, 0x10), alone + 18);
    // Behind anything else, one microsecond for each: a channel that
    // stops, one that asks for an interrupt, one with nothing to do.
    list(kLists[0], {kStop});
    CHECK_EQ(interruptAfter(0x07, 0x10), alone + 10);
    list(kLists[0], {kInterrupt});
    CHECK_EQ(interruptAfter(0x07, 0x10), alone + 10);
    list(kLists[0], {kNothing, kStop});
    list(kLists[1], {kNothing, kStop});
    CHECK_EQ(interruptAfter(0x06, 0x10), alone + 1);
    CHECK_EQ(interruptAfter(0x07, 0x10), alone + 2);
    list(kLists[1], {kInterrupt});
    CHECK_EQ(interruptAfter(0x03, 0x20), alone + 1);

    // Channel 2's interrupt stays until cleared like the others'...
    list(kLists[2], {kInterrupt});
    CHECK_EQ(interruptAfter(0x04, 0x10), alone);
    if (cpc.gateArray().interruptRequested())
        cpc.irqAck();
    CHECK_EQ(cpc.irqAck(), 0x00);  // its vector
    CHECK_EQ(status(), 0x10);
    CHECK(cpc.irq());
    m.write(0x6C0F, 0x10);
    CHECK_EQ(status(), 0x00);
    // ...and goes by itself with bit 0 of the vector register clear.
    m.write(0x6805, 0x00);
    CHECK_EQ(interruptAfter(0x04, 0x10), alone);
    if (cpc.gateArray().interruptRequested())
        cpc.irqAck();
    cpc.irqAck();
    CHECK_EQ(status(), 0x00);
    m.write(0x6805, 0x01);

    // The channels that are on are picked two microseconds into the round,
    // not as it begins: one turned on during the HSYNC's first microseconds
    // runs on that line ("dmatiming", seventh set-up), and one turned off
    // just after the choice still has its turn ("dmatest", where a handler
    // turns the channels off as the round starts and the interrupt comes
    // all the same).
    list(kLists[0], {kInterrupt});
    for (int late = 0; late < 6; ++late) {
        point();
        toHsync();
        step(late);
        m.write(0x6C0F, 0x01);
        const int after = wait(0x40);
        CHECK_EQ(after, late < 3 ? alone - late : -1);
    }
    for (int late = 0; late < 6; ++late) {
        point();
        toHsync();
        step(20);
        m.write(0x6C0F, 0x01);
        toHsync();
        step(late);
        m.write(0x6C0F, 0x00);
        CHECK_EQ(wait(0x40), late < 3 ? -1 : alone - late);
    }

    // Lines of three characters with an HSYNC of fifteen: each HSYNC ends
    // on the character where the next begins, and the pulse never drops,
    // yet every one of them is a round ("dmatest", "CRTC R0 length and
    // dma"). With R0 = 0 the counter never leaves R2: one round and no
    // more.
    auto crtc = [&](int reg, int value) {
        cpc.out(0xBC00, static_cast<uint8_t>(reg));
        cpc.out(0xBD00, static_cast<uint8_t>(value));
    };
    list(kLists[0], {0x0830, 0x0820, 0x0810, kStop});
    for (const int r0 : {2, 4, 14, 0}) {
        point();
        cpc.psg().setRegister(8, 0);
        crtc(0, 63);
        crtc(2, 0);
        crtc(3, 0x8F);
        step(200);
        crtc(0, r0);
        step(600);  // the counter has to come round to 0 first
        m.write(0x6C0F, 0x01);
        step(600);
        if (r0 != 0) {
            CHECK_EQ(status(), 0x00);
            CHECK_EQ(cpc.psg().reg(8), 0x10);
        } else {
            CHECK_EQ(status(), 0x01);
        }
    }
}

// A snapshot of a Plus holds what its ASIC held.
void testSnapshot()
{
    PlusMachine m;
    m.write(0x6400, 0x0F);
    m.write(0x6420, 0x00);
    m.write(0x6421, 0x0F);
    m.write(0x6422, 0xF0);
    for (int i = 0; i < 256; ++i)
        m.write(static_cast<uint16_t>(0x4300 + i), static_cast<uint8_t>(i % 5 == 0 ? 0 : 1));
    m.write(0x6018, 100);
    m.write(0x6019, 0x03);
    m.write(0x601A, 60);
    m.write(0x601C, 0x0E);
    m.write(0x6800, 90);
    m.write(0x6801, 150);
    m.write(0x6802, 0x10);
    m.write(0x6803, 0x22);
    m.write(0x6804, 0x93);
    m.write(0x6805, 0x59);
    m.cpc.memory().baseRam()[0x8000] = 0x34;  // a pause of &234 units of 5 lines, under way
    m.cpc.memory().baseRam()[0x8001] = 0x12;
    m.write(0x6C04, 0x00);
    m.write(0x6C05, 0x80);
    m.write(0x6C06, 0x04);
    m.write(0x6C0F, 0x02);
    m.cpc.run(3 * 64);
    m.cpc.out(0x7F00, 0xA0 + 0x08 + 5);  // the lower ROM at &4000, page 5
    const std::vector<uint8_t> snapshot = saveSnapshot(m.cpc, SnapshotMachine::Plus6128);
    SnapshotInfo info;
    CHECK(snapshotInfo(snapshot, info));
    CHECK(info.machine == SnapshotMachine::Plus6128);
    const std::vector<uint8_t> chunk = m.cpc.asic().snapshot();
    CHECK_EQ(chunk.size(), 0x8F8);

    PlusMachine other;
    std::string error;
    CHECK(loadSnapshot(other.cpc, snapshot, &error));
    Asic& asic = other.cpc.asic();
    CHECK(asic.unlocked());
    CHECK_EQ(asic.rmr2(), 0x0D);
    CHECK_EQ(other.cpc.memory().read(0x4000), 0xC5);
    CHECK_EQ(asic.colour(0), 0x00F);
    CHECK_EQ(asic.colour(16), 0xF00);
    CHECK_EQ(asic.colour(17), 0x0F0);
    CHECK_EQ(asic.spritePixel(3, 1, 0), 1);
    CHECK_EQ(asic.spritePixel(3, 0, 0), 0);
    CHECK_EQ(asic.sprite(3).x, 100 - 256);
    CHECK_EQ(asic.sprite(3).y, 60);
    CHECK_EQ(asic.sprite(3).magX, 4);
    CHECK_EQ(asic.sprite(3).magY, 2);
    CHECK(asic.spritesShown());
    CHECK_EQ(asic.rasterInterruptLine(), 90);
    CHECK_EQ(asic.splitLine(), 150);
    CHECK_EQ(asic.splitAddress(), 0x1022);
    CHECK_EQ(asic.scroll(), 0x93);
    CHECK_EQ(asic.interruptVector(), 0x59);
    CHECK(asic.soundChannelsOn());
    // To the byte: the sound channel is where it was in its pause.
    CHECK(asic.snapshot() == chunk);
    CHECK_EQ(other.cpc.gateArray().spriteColour(1), kRed);

    // A snapshot from a CPC, or from a program that knows nothing of the
    // Plus, leaves the ASIC as a reset does.
    std::vector<uint8_t> plain(snapshot.begin(), snapshot.end() - (8 + 0x8F8));
    CHECK(loadSnapshot(other.cpc, plain, &error));
    CHECK(!asic.unlocked());
    CHECK_EQ(asic.rmr2(), 0);
    CHECK_EQ(asic.rasterInterruptLine(), 0);
    CHECK(!asic.spritesShown());
    // On a CPC the chunk is passed over.
    Cpc cpc;
    CHECK(loadSnapshot(cpc, snapshot, &error));
    CHECK(!cpc.asic().unlocked());
}

}  // namespace

int main()
{
    testCprFiles();
    testCartridgePages();
    testAsic();
    testPpi();
    testSprites();
    testSplitAndScroll();
    testSplitMoment();
    testFloatingBus();
    testRasterInterrupt();
    testSoundChannels();
    testSoundChannelTiming();
    testSnapshot();
    return checkSummary("plus");
}
