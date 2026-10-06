// The Multiface II: its ROM and RAM paged in and out by its ports and by
// its red button, the notes it keeps of the write-only ports, and, with a
// real ROM image if the user has one in the "roms" folder, the Multiface's
// own program taking over a machine and giving it back.
//
//   multiface [rom image [picture.ppm]]   also saves the screen as the
//                                         Multiface's menu shows on it

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/files.h"
#include "core/screen_text.h"
#include "core/setup.h"

using namespace tuxape;
using Bytes = std::vector<uint8_t>;

namespace {

// A machine with no ROM, standing in a loop at &9000, interrupts off.
void park(Cpc& cpc)
{
    cpc.out(0x7F00, 0x8D);  // both ROMs out of the way
    cpc.memory().write(0x9000, 0x18);
    cpc.memory().write(0x9001, 0xFE);
    cpc.cpu().pc = 0x9000;
    cpc.cpu().sp = 0xBFF0;
    cpc.cpu().iff1 = cpc.cpu().iff2 = false;
}

// A ROM of the test's own. Its entry at &0066 keeps a mark in the
// Multiface's RAM, copies the pen it has on note to the machine's memory,
// and leaves through a routine in the machine's RAM that pages it out.
Bytes ownRom()
{
    Bytes rom(0x2000, 0x00);
    rom[0x0000] = 0xA5;
    const uint8_t entry[] = {0x3E, 0x5A,        // LD A,#5A
                             0x32, 0x00, 0x21,  // LD (#2100),A
                             0x3A, 0xCF, 0x3F,  // LD A,(#3FCF)
                             0x32, 0x00, 0x80,  // LD (#8000),A
                             0xC3, 0x00, 0x81}; // JP #8100
    std::copy(std::begin(entry), std::end(entry), rom.begin() + 0x66);
    return rom;
}

void testHardware()
{
    Cpc cpc;
    park(cpc);
    Memory& memory = cpc.memory();
    // Without one: nothing at its ports, and its button does nothing.
    CHECK(!cpc.hasMultiface() && memory.multifaceRam() == nullptr);
    cpc.out(0xFEE8, 0);
    cpc.multifaceStop();
    cpc.run(1000);
    CHECK(!memory.multifacePaged() && cpc.cpu().pc >= 0x9000 && cpc.cpu().pc <= 0x9001);

    const Bytes rom = ownRom();
    cpc.setMultiface(rom);
    CHECK(cpc.hasMultiface() && !memory.multifacePaged());
    uint8_t* notes = memory.multifaceRam();
    CHECK(notes != nullptr);
    if (!notes)
        return;
    memory.write(0x0000, 0x11);
    memory.write(0x2000, 0x22);
    CHECK(memory.read(0x0000) == 0x11 && memory.read(0x2000) == 0x22);

    // OUT &FEE8: its ROM at &0000, its RAM at &2000, over what was there.
    cpc.out(0xFEE8, 0);
    CHECK(memory.multifacePaged());
    CHECK_EQ(memory.read(0x0000), 0xA5);
    CHECK_EQ(memory.read(0x0066), 0x3E);
    CHECK_EQ(memory.read(0x2000), 0x00);
    CHECK_EQ(memory.read(0x4000), memory.readRam(0x4000));
    // Written to its RAM, not the machine's; under its ROM, to the
    // machine's as ever.
    memory.write(0x2000, 0x33);
    memory.write(0x3FFF, 0x44);
    memory.write(0x0000, 0x55);
    CHECK(memory.read(0x2000) == 0x33 && notes[0] == 0x33 && notes[0x1FFF] == 0x44);
    CHECK(memory.readRam(0x2000) == 0x22 && memory.readRam(0x0000) == 0x55);
    CHECK_EQ(memory.read(0x0000), 0xA5);
    // OUT &FEEA: gone, the machine's memory as it was.
    cpc.out(0xFEEA, 0);
    CHECK(!memory.multifacePaged());
    CHECK(memory.read(0x0000) == 0x55 && memory.read(0x2000) == 0x22);
    CHECK_EQ(notes[0], 0x33);  // its RAM keeps what it holds

    // Its notes of the ports that cannot be read back.
    cpc.out(0x7F00, 0x05);  // pen 5
    cpc.out(0x7F00, 0x4B);  // its colour
    cpc.out(0x7F00, 0x10);  // the border
    cpc.out(0x7F00, 0x54);
    cpc.out(0x7F00, 0x8D);  // mode 1, both ROMs off
    cpc.out(0x7F00, 0xC0);  // the RAM as it is
    cpc.out(0xBC00, 0x0C);  // CRTC register 12
    cpc.out(0xBD00, 0x30);
    cpc.out(0xBC00, 0x07);
    cpc.out(0xBD00, 0x1E);
    cpc.out(0xDF00, 0x07);  // ROM 7
    cpc.out(0xF700, 0x82);  // the PPI's control
    CHECK_EQ(notes[0x1FCF], 0x10);
    CHECK(notes[0x1F90 + 5] == 0x4B && notes[0x1F90 + 16] == 0x54);
    CHECK_EQ(notes[0x1FEF], 0x8D);
    CHECK_EQ(notes[0x1FFF], 0xC0);
    CHECK_EQ(notes[0x1CFF], 0x07);
    CHECK(notes[0x1DB0 + 12] == 0x30 && notes[0x1DB0 + 7] == 0x1E);
    CHECK_EQ(notes[0x1AAC], 0x07);
    CHECK_EQ(notes[0x17FF], 0x82);
    // The PPI's ports themselves are not its control.
    cpc.out(0xF600, 0x55);
    CHECK_EQ(notes[0x17FF], 0x82);

    // The red button: the machine is stopped where it is, the Multiface's
    // program runs, with its ROM and RAM in, and gives the machine back.
    const uint8_t leave[] = {0x01, 0xEA, 0xFE,  // LD BC,#FEEA
                             0xED, 0x49,        // OUT (C),C
                             0xED, 0x45};       // RETN
    for (size_t i = 0; i < sizeof leave; ++i)
        memory.write(static_cast<uint16_t>(0x8100 + i), leave[i]);
    cpc.out(0x7F00, 0x03);  // pen 3 on note
    notes[0x100] = 0;
    cpc.multifaceStop();
    CHECK(memory.multifacePaged());
    cpc.multifaceStop();  // once is enough
    cpc.run(200);
    CHECK(!memory.multifacePaged());
    CHECK_EQ(notes[0x100], 0x5A);
    CHECK_EQ(memory.read(0x8000), 0x03);
    CHECK(cpc.cpu().pc >= 0x9000 && cpc.cpu().pc <= 0x9001);
    CHECK_EQ(cpc.cpu().sp, 0xBFF0);

    // A reset pages it out; taking it out too.
    cpc.out(0xFEE8, 0);
    CHECK(memory.multifacePaged());
    cpc.reset();
    CHECK(!memory.multifacePaged() && cpc.hasMultiface());
    cpc.out(0xFEE8, 0);
    cpc.setMultiface({});
    CHECK(!cpc.hasMultiface() && !memory.multifacePaged() && memory.multifaceRam() == nullptr);
    CHECK(memory.read(0x0000) != 0xA5);
}

// Named in a machine's configuration, the ROM is fitted with the others.
void testConfiguration(const std::string& romFile)
{
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    CHECK(!config.multifaceEnabled && config.multifaceRom.empty());
    Cpc cpc;
    std::string error;
    config.multifaceRom = romFile;
    applyMachine(cpc, config, defaultRomDir(), &error);
    CHECK(!cpc.hasMultiface());  // named, not enabled
    config.multifaceEnabled = true;
    applyMachine(cpc, config, defaultRomDir(), &error);
    CHECK(cpc.hasMultiface());
    config.multifaceRom = "no such multiface";
    error.clear();
    applyMachine(cpc, config, defaultRomDir(), &error);
    CHECK(!cpc.hasMultiface() && error.find("no such multiface") != std::string::npos);
}

// The real thing: a 6128 at its "Ready", the red button, the Multiface's
// menu, and "r" to return.
bool testRealRom(const std::string& romFile, const char* picture)
{
    const auto rom = readFile(romFile);
    if (!rom || rom->size() != 0x2000)
        return false;
    Cpc cpc;
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    config.multifaceRom = romFile;
    config.multifaceEnabled = true;
    std::string error;
    if (!applyMachine(cpc, config, defaultRomDir(), &error))
        return false;
    cpc.reset();
    for (int i = 0; i < 150; ++i)
        cpc.runFrame();
    CHECK(readScreenText(cpc).find("Ready") != std::string::npos);
    const std::string before = readScreenText(cpc);
    CHECK(!cpc.memory().multifacePaged());

    cpc.multifaceStop();
    int paged = 0;
    for (int i = 0; i < 100; ++i) {
        cpc.runFrame();
        paged += cpc.memory().multifacePaged();
    }
    // Its menu is up: its program runs, in its ROM, and has drawn its
    // line over the bottom of the screen.
    CHECK(paged > 80 && cpc.memory().multifacePaged());
    CHECK(cpc.cpu().pc < 0x4000);
    CHECK(readScreenText(cpc) != before);
    if (picture) {
        if (FILE* file = std::fopen(picture, "wb")) {
            std::fprintf(file, "P6 %d %d 255\n", Monitor::kWidth, Monitor::kHeight);
            const uint32_t* frame = cpc.monitor().frame();
            for (int i = 0; i < Monitor::kWidth * Monitor::kHeight; ++i) {
                const uint8_t rgb[3] = {static_cast<uint8_t>(frame[i] >> 16), static_cast<uint8_t>(frame[i] >> 8),
                                        static_cast<uint8_t>(frame[i])};
                std::fwrite(rgb, 1, 3, file);
            }
            std::fclose(file);
        }
    }
    // The button again does nothing while the menu is up.
    const uint16_t sp = cpc.cpu().sp;
    cpc.multifaceStop();
    cpc.runFrame();
    CHECK(cpc.memory().multifacePaged());
    (void)sp;
    // "r": return. The machine is back where it was, its screen with it.
    for (int i = 0; i < 20; ++i) {
        cpc.keyboard().set(CpcKey::R, i < 10);
        cpc.runFrame();
    }
    for (int i = 0; i < 100; ++i)
        cpc.runFrame();
    CHECK(!cpc.memory().multifacePaged());
    CHECK(readScreenText(cpc) == before);
    // And goes on: BASIC answers.
    AutoType keys(cpc.keyboard());
    keys.type("PRINT 6*7\n");
    for (int i = 0; i < 200; ++i) {
        keys.frame();
        cpc.runFrame();
    }
    CHECK(readScreenText(cpc).find(" 42") != std::string::npos);
    // A second time, as well as the first.
    cpc.multifaceStop();
    for (int i = 0; i < 60; ++i)
        cpc.runFrame();
    CHECK(cpc.memory().multifacePaged());
    for (int i = 0; i < 20; ++i) {
        cpc.keyboard().set(CpcKey::R, i < 10);
        cpc.runFrame();
    }
    for (int i = 0; i < 60; ++i)
        cpc.runFrame();
    CHECK(!cpc.memory().multifacePaged());
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    testHardware();
    const std::string romFile = argc > 1 ? argv[1] : TUXAPE_USER_ROMS "/multiface2/multiface2.rom";
    const bool real = testRealRom(romFile, argc > 2 ? argv[2] : nullptr);
    if (real)
        testConfiguration(romFile);
    else
        std::printf("no Multiface ROM image (%s): its own program was not tested\n", romFile.c_str());
    return checkSummary("multiface");
}
