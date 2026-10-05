// The Plus machines: cartridges, the ASIC's lock, the cartridge pages and
// the page of registers it maps, and its palette. Everything is done from
// outside, port by port, on a cartridge made up here: no ROM image is
// needed. What real machines do is in Kevin Thacker's tests (acid_plus).

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

}  // namespace

int main()
{
    testCprFiles();
    testCartridgePages();
    testAsic();
    return checkSummary("plus");
}
