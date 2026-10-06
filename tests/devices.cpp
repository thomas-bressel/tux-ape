// What plugs into the printer's port and beside it: a printer, and the
// Digiblaster and AmDrum sound converters. The firmware's own printing is
// tried too when the ROM images are there (exit code 77 without them).

#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

void testPrinter()
{
    Cpc cpc;
    std::string printed;
    // Nothing connected: the port says "busy", and nothing is printed.
    CHECK(cpc.in(0xF500) & 0x40);
    cpc.out(0xEF00, 'A');
    cpc.out(0xEF00, 'A' | 0x80);
    cpc.setPrinterHook([&](uint8_t character) { printed += static_cast<char>(character); });
    CHECK(!(cpc.in(0xF500) & 0x40));
    // A character goes when the strobe bit rises, once.
    cpc.out(0xEF00, 'H');
    CHECK(printed.empty());
    cpc.out(0xEF00, 'H' | 0x80);
    CHECK(printed == "H");
    cpc.out(0xEF00, 'H' | 0x80);
    cpc.out(0xEF00, 'i' | 0x80);
    CHECK(printed == "H");
    cpc.out(0xEF00, 'i');
    cpc.out(0xEF00, 'i' | 0x80);
    cpc.out(0xEF00, 'i');
    CHECK(printed == "Hi");
    // Other ports are not the printer's.
    cpc.out(0xFF00, 'x' | 0x80);
    cpc.out(0xDF00, 0x80);
    CHECK(printed == "Hi");
    cpc.setPrinterHook(nullptr);
    CHECK(cpc.in(0xF500) & 0x40);
}

// How much the sound moves in a fiftieth of a second while a program
// writes a square wave to a port.
long heard(bool digiblaster, bool amDrum, uint16_t port)
{
    Cpc cpc;
    cpc.out(0x7F00, 0x8C);
    // 8000 LD BC,port / 8003 LD A,#10 / 8005 OUT (C),A / 8007 XOR #C0 /
    // 8009 LD D,#40 / 800B DEC D / 800C JR NZ,800B / 800E JR 8005
    const uint8_t program[] = {0x01, static_cast<uint8_t>(port), static_cast<uint8_t>(port >> 8), 0x3E, 0x10, 0xED, 0x79, 0xEE,
                               0xC0, 0x16, 0x40, 0x15, 0x20, 0xFD, 0x18, 0xF5};
    for (size_t i = 0; i < sizeof program; ++i)
        cpc.memory().write(static_cast<uint16_t>(0x8000 + i), program[i]);
    cpc.cpu().pc = 0x8000;
    cpc.cpu().iff1 = cpc.cpu().iff2 = false;
    std::string printed;
    cpc.setPrinterHook([&](uint8_t character) { printed += static_cast<char>(character); });
    cpc.setDigiblaster(digiblaster);
    cpc.setAmDrum(amDrum);
    cpc.audio().setSampleRate(44100);
    cpc.run(20000);
    // A Digiblaster in the printer's place: nothing is printed.
    CHECK(printed.empty() == (digiblaster || port != 0xEF00));
    const std::vector<int16_t>& samples = cpc.audio().samples();
    long moves = 0;
    for (size_t i = 2; i < samples.size(); i += 2)
        moves += std::abs(samples[i] - samples[i - 2]);
    return moves;
}

void testConverters()
{
    CHECK_EQ(heard(false, false, 0xEF00), 0);
    CHECK_EQ(heard(false, false, 0xFF00), 0);
    CHECK(heard(true, false, 0xEF00) > 100000);
    CHECK(heard(false, true, 0xFF00) > 100000);
    // Each on its own port.
    CHECK_EQ(heard(true, false, 0xFF00), 0);
    CHECK_EQ(heard(false, true, 0xEF00), 0);
}

// The AMX mouse: its steps are found one at a time on the joystick's
// line, one for each time the line is looked at afresh.
void testMouse()
{
    Cpc cpc;
    // A line of the keyboard, read as the firmware does: through the sound
    // chip's port, which the PPI's port A leads to.
    const auto line = [&](int n) {
        cpc.out(0xF700, 0x82);  // port A out
        cpc.out(0xF400, 14);    // the sound chip's register 14...
        cpc.out(0xF600, 0xC0);  // ...chosen
        cpc.out(0xF600, 0x00);
        cpc.out(0xF700, 0x92);  // port A in
        cpc.out(0xF600, static_cast<uint8_t>(0x40 | n));
        const uint8_t value = cpc.in(0xF400);
        cpc.out(0xF600, static_cast<uint8_t>(n));
        return value;
    };
    // Looked at again without having looked elsewhere: the same step.
    const auto again = [&] {
        cpc.out(0xF600, 0x49);
        return cpc.in(0xF400);
    };
    CHECK_EQ(line(9), 0xFF);
    cpc.moveAmxMouse(5, 5);  // not a mouse yet: nothing
    cpc.setAmxMouse(true);
    CHECK(cpc.amxMouse());
    CHECK_EQ(line(9), 0xFF);
    // Three steps right and two up.
    cpc.moveAmxMouse(3, -2);
    line(8);
    CHECK_EQ(line(9), 0xFF & ~0x09);
    CHECK_EQ(again(), 0xFF & ~0x09);
    cpc.out(0xF600, 0x09);
    line(0);
    CHECK_EQ(line(9), 0xFF & ~0x09);
    line(0);
    CHECK_EQ(line(9), 0xFF & ~0x08);
    line(0);
    CHECK_EQ(line(9), 0xFF);
    // Left and down; the buttons are the fire buttons, and stay down.
    cpc.moveAmxMouse(-1, 1);
    cpc.setAmxButtons(true, false, false);
    line(0);
    CHECK_EQ(line(9), 0xFF & ~0x16);
    line(0);
    CHECK_EQ(line(9), 0xFF & ~0x10);
    cpc.setAmxButtons(false, true, true);
    CHECK_EQ(cpc.amxButtons(), 0x60);
    line(0);
    CHECK_EQ(line(9), 0xFF & ~0x60);
    // The joystick and the keyboard's other lines are as they were.
    cpc.keyboard().set(CpcKey::JoyFire2, true);
    cpc.keyboard().set(CpcKey::A, true);
    cpc.setAmxButtons(false, false, false);
    line(0);
    CHECK_EQ(line(9), 0xFF & ~0x10);
    CHECK_EQ(line(8), 0xFF & ~0x20);
    cpc.setAmxMouse(false);
    cpc.moveAmxMouse(4, 4);
    CHECK_EQ(line(9), 0xFF & ~0x10);
}

// BASIC's PRINT #8 through the firmware: the characters, then the end of
// the line.
bool printsFromBasic()
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr))
        return false;
    std::string printed;
    cpc.setPrinterHook([&](uint8_t character) { printed += static_cast<char>(character); });
    AutoType keys(cpc.keyboard());
    const auto frames = [&](int count) {
        for (int i = 0; i < count; ++i) {
            keys.frame();
            cpc.runFrame();
        }
    };
    frames(150);
    keys.type("PRINT #8,\"Hello, CPC!\"\n");
    frames(300);
    CHECK(printed == "Hello, CPC!\r\n");
    if (printed != "Hello, CPC!\r\n")
        std::printf("printed \"%s\"\n", printed.c_str());
    return true;
}

}  // namespace

int main()
{
    testPrinter();
    testConverters();
    testMouse();
    const bool roms = printsFromBasic();
    if (!roms)
        std::printf("ROM images not found; printing from BASIC was not tested\n");
    const int result = checkSummary("devices");
    return result != 0 ? result : roms ? 0 : 77;
}
