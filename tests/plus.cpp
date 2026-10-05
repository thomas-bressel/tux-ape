// The Plus machines: cartridges, the ASIC's lock, the cartridge pages and
// the page of registers it maps, and its palette. Everything is done from
// outside, port by port, on a cartridge made up here: no ROM image is
// needed. What real machines do is in Kevin Thacker's tests (acid_plus).

#include <algorithm>
#include <vector>

#include "check.h"
#include "core/cartridge.h"
#include "core/cpc.h"

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

    // Page 0 is the lower ROM, page 1 the upper one, whatever ROM is asked
    // for; ROM 7 is page 3 on a machine with a disc drive.
    CHECK_EQ(memory.read(0x0000), 0xC0);
    CHECK_EQ(memory.read(0xC000), 0xC1);
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

// The ASIC's stand-in for the 8255: port B always an input, port C always
// an output, and port A read through the control register's address.
void testPpi()
{
    Cpc cpc;
    const Cartridge cartridge = numbered(8);
    cpc.setCartridge(&cartridge);
    cpc.reset();
    cpc.out(0xF700, 0x80);  // all three ports asked to be outputs
    cpc.out(0xF500, 0x00);
    CHECK_EQ(cpc.in(0xF500) & 0x5E, 0x5E);  // port B still reads its links
    cpc.out(0xF700, 0x9B);  // all three asked to be inputs
    cpc.out(0xF600, 0x25);
    CHECK_EQ(cpc.in(0xF600), 0x25);  // port C still gives back what was written
    CHECK_EQ(cpc.in(0xF700), 0xFF);  // port A's pins, with nothing driving them
    cpc.out(0xF700, 0x82);
    cpc.out(0xF400, 0x38);
    CHECK_EQ(cpc.in(0xF700), 0x00);  // port A an output

    // A CPC's 8255 does as it is told.
    Cpc plain;
    plain.out(0xF700, 0x80);
    plain.out(0xF500, 0x00);
    CHECK_EQ(plain.in(0xF500), 0x00);
    plain.out(0xF700, 0x9B);
    plain.out(0xF600, 0x25);
    CHECK_EQ(plain.in(0xF600), 0xFF);
    CHECK_EQ(plain.in(0xF700), 0xFF);
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

    // From line 100 on the picture comes from &4000.
    m.write(0x6801, 100);
    m.write(0x6802, 0x10);
    m.write(0x6803, 0x00);
    const uint32_t* frame = m.picture();
    CHECK_EQ(frame[(screen.y + 99) * Monitor::kWidth + screen.x + 100], kBlue);
    CHECK_EQ(frame[(screen.y + 100) * Monitor::kWidth + screen.x + 100], kWhite);
    CHECK_EQ(frame[(screen.y + 199) * Monitor::kWidth + screen.x + 100], kWhite);
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
        CHECK_EQ(cpc.memory().read(0x6C0F) & 0x80, 0x00);
    }
    // Back to the CPC's.
    m.write(0x6800, 0);
    const int back = nextInterrupt();
    CHECK(back >= 0 && back != 77);
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
    testRasterInterrupt();
    return checkSummary("plus");
}
