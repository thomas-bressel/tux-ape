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
    const bool roms = printsFromBasic();
    if (!roms)
        std::printf("ROM images not found; printing from BASIC was not tested\n");
    const int result = checkSummary("devices");
    return result != 0 ? result : roms ? 0 : 77;
}
