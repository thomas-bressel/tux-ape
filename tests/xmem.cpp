// The X-MEM card: its 512K of RAM, its 32 ROMs in flash memory with the
// two places kept from the machine, its switches, and the flash written
// the way its own flashing routine writes it.
//
//   xmem [folder]   the folder holds what cannot be given out with TuxAPE:
//                   xmem/xmem_side-A.dsk, the card's own disc. With it,
//                   and the machine's ROM images, the disc's INSTALL is
//                   run on a 6128 and the machine then started from the
//                   card. With xmem/xmass_xmem-install.dsk an X-MASS
//                   card's disc is formatted and used too. Without them
//                   those parts are left out.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "core/autotype.h"
#include "core/cartridge.h"
#include "core/cpc.h"
#include "core/disc.h"
#include "core/files.h"
#include "core/screen_text.h"
#include "core/setup.h"
#include "core/xmem.h"

namespace {

using namespace tuxape;

bool contains(const std::string& text, const char* what)
{
    return text.find(what) != std::string::npos;
}

// A ROM's worth of bytes that say which ROM they are.
std::vector<uint8_t> pattern(int place)
{
    std::vector<uint8_t> rom(Xmem::kRomSize);
    for (size_t i = 0; i < rom.size(); ++i)
        rom[i] = static_cast<uint8_t>(place * 7 + i + (i >> 8));
    rom[0] = static_cast<uint8_t>(0x80 | place);
    return rom;
}

void testPlaces()
{
    Xmem card;
    for (int place = 0; place < Xmem::kRoms; ++place)
        card.setRom(place, pattern(place));
    CHECK(card.changed());
    card.clearChanged();
    const auto shows = [&](uint8_t select, int place) {
        const uint8_t* rom = card.upperRom(select);
        return place < 0 ? rom == nullptr : rom != nullptr && rom[0] == (0x80 | place) && rom[100] == pattern(place)[100];
    };

    // ROMs 1 to 31 are the card's, but 7, which is the machine's own, and
    // 0, its BASIC, unless the card boots.
    Xmem::Switches switches;
    CHECK(switches.ram6128 && !switches.bootRom && !switches.romFree && switches.readRom);
    for (int rom = 1; rom < 32; ++rom)
        CHECK(shows(static_cast<uint8_t>(rom), rom == 7 ? -1 : rom));
    CHECK(shows(0, -1));
    CHECK(card.lowerRom() == nullptr);
    // Bit 5 is not looked at; with bit 7 the card keeps quiet.
    CHECK(shows(0x22, 2) && shows(0x27, -1));
    CHECK(shows(0x82, -1) && shows(0xFF, -1));
    // Bit 6: the two places kept from the machine, by name.
    CHECK(shows(0x47, 7) && shows(0x40, 0) && shows(0x45, 5));

    // BOOT on ROM: the lower ROM is place 7, ROM 0 place 0. ROM 7 is still
    // not the card's.
    switches.bootRom = true;
    card.setSwitches(switches);
    CHECK(shows(0, 0) && shows(7, -1));
    CHECK(card.lowerRom() != nullptr && card.lowerRom()[0] == (0x80 | 7));

    // READ ROM on NO: none of it is seen.
    switches.readRom = false;
    card.setSwitches(switches);
    CHECK(shows(2, -1) && shows(0, -1) && shows(0x47, -1) && card.lowerRom() == nullptr);

    // The RAM: eight banks, or seven where the machine has the first.
    CHECK_EQ(card.ramBanks(), 0xFE);
    switches.ram6128 = false;
    card.setSwitches(switches);
    CHECK_EQ(card.ramBanks(), 0xFF);
}

// The chip's commands, given as the card's flashing routine gives them:
// &AA at &D555 with an odd ROM chosen, &55 at &EAAA with an even one, the
// command at &D555.
struct Flasher {
    Xmem& card;
    bool changed = false;
    void command(uint8_t value)
    {
        changed |= card.write(3, 0x1555, 0xAA);
        changed |= card.write(2, 0x2AAA, 0x55);
        changed |= card.write(3, 0x1555, value);
    }
    void program(uint8_t select, uint16_t offset, uint8_t value)
    {
        command(0xA0);
        card.write(select, offset, value);
    }
    void eraseSector(uint8_t select, uint16_t offset)
    {
        command(0x80);
        card.write(3, 0x1555, 0xAA);
        card.write(2, 0x2AAA, 0x55);
        card.write(select, offset, 0x30);
    }
};

void testFlash()
{
    Xmem card;
    card.setRom(5, pattern(5));
    card.setRom(6, pattern(6));
    card.clearChanged();
    Flasher flasher{card};

    // Locked, nothing is taken: not even the question of who the chip is.
    flasher.command(0x90);
    CHECK(!flasher.changed && card.upperRom(2)[0] == 0xFF);
    flasher.eraseSector(5, 0x0055);
    flasher.program(5, 0x2000, 0x00);
    CHECK(card.upperRom(5)[0] == 0x85 && card.upperRom(5)[0x2000] == pattern(5)[0x2000]);
    CHECK(!card.changed());

    Xmem::Switches switches;
    switches.romFree = true;
    card.setSwitches(switches);
    // Who it is: an SST39SF040, read where a ROM's first two bytes are,
    // until it is told to go back to its contents.
    flasher.command(0x90);
    CHECK(flasher.changed);
    CHECK(card.upperRom(2)[0] == Xmem::kMaker && card.upperRom(2)[1] == Xmem::kDevice);
    CHECK(card.upperRom(5)[0] == 0xBF && card.upperRom(5)[1] == 0xB7);
    flasher.changed = false;
    flasher.command(0xF0);
    CHECK(flasher.changed && card.upperRom(5)[0] == 0x85);
    CHECK(!card.changed());

    // A byte written without the command is nothing to the chip.
    card.write(5, 0x0000, 0x00);
    CHECK(card.upperRom(5)[0] == 0x85);
    // Programming clears bits; it sets none.
    flasher.program(5, 0x0000, 0x0F);
    CHECK_EQ(card.upperRom(5)[0], 0x05);
    flasher.program(5, 0x0000, 0xF0);
    CHECK_EQ(card.upperRom(5)[0], 0x00);
    CHECK(card.changed());
    // One command, one byte.
    card.write(5, 0x0001, 0x00);
    CHECK_EQ(card.upperRom(5)[1], pattern(5)[1]);

    // An erase clears 4K: the sector the address is in, and no more.
    flasher.eraseSector(5, 0x1055);
    CHECK(card.upperRom(5)[0x1000] == 0xFF && card.upperRom(5)[0x1FFF] == 0xFF);
    CHECK(card.upperRom(5)[0x0FFF] == pattern(5)[0x0FFF] && card.upperRom(5)[0x2000] == pattern(5)[0x2000]);
    CHECK(card.upperRom(6)[0] == 0x86);
    // The places kept from the machine are written by name.
    flasher.program(0x47, 0x0010, 0x42);
    flasher.program(0x40, 0x0010, 0x24);
    CHECK(card.upperRom(0x47)[0x10] == 0x42 && card.upperRom(0x40)[0x10] == 0x24);
    // By number they are not the card's: nothing is written.
    flasher.program(7, 0x0011, 0x00);
    flasher.command(0xF0);
    flasher.program(0, 0x0011, 0x00);
    flasher.command(0xF0);
    CHECK(card.upperRom(0x47)[0x11] == 0xFF && card.upperRom(0x40)[0x11] == 0xFF);
    // The whole chip.
    flasher.command(0x80);
    flasher.command(0x10);
    bool erased = true;
    for (const uint8_t byte : card.flash())
        erased = erased && byte == 0xFF;
    CHECK(erased);
    // Power off and on in the middle of a command: it is forgotten.
    flasher.command(0x90);
    card.reset();
    CHECK(card.upperRom(2)[0] == 0xFF);
}

// What a place holds, as a ROM tells of itself.
void testRomInfo()
{
    Xmem card;
    CHECK(!card.romInfo(3).present);
    // A background ROM, version 1.40, its name the first of its commands.
    std::vector<uint8_t> rom(Xmem::kRomSize, 0);
    const uint8_t header[] = {0x01, 1, 4, 0, 0x10, 0xC0};
    std::copy(header, header + 6, rom.begin());
    const char name[] = "UNIDOS";
    for (int i = 0; i < 6; ++i)
        rom[0x10 + static_cast<size_t>(i)] = static_cast<uint8_t>(name[i] | (i == 5 ? 0x80 : 0));
    card.setRom(5, rom);
    const Xmem::RomInfo info = card.romInfo(5);
    CHECK(info.present && !info.firmware && info.kind == 1 && info.name == "UNIDOS");
    CHECK(info.mark == 1 && info.version == 4 && info.modification == 0);
    // The BASIC's kind; a firmware, which has no such header; and what is
    // no ROM at all.
    rom[0] = 0x80;
    CHECK(Xmem::romInfo(rom).kind == 0x80);
    rom[0] = 0x01;
    rom[1] = 0x89;
    rom[2] = 0x7F;
    CHECK(Xmem::romInfo(rom).firmware && Xmem::romInfo(rom).name.empty());
    rom[0] = 0x55;
    CHECK(!Xmem::romInfo(rom).present);
    rom[0] = 0x02;
    rom[1] = 3;
    rom[4] = 0x00;
    rom[5] = 0x10;  // a table outside the ROM
    CHECK(!Xmem::romInfo(rom).present);
    CHECK(!card.romInfo(-1).present && !card.romInfo(32).present);
}

// On a Plus the card takes the lower ROM's place as on a CPC, when it
// boots; the cartridge's pages stay everywhere else.
void testPlus()
{
    Cartridge cartridge;
    cartridge.data.assign(4 * Cartridge::kPageSize, 0);
    for (int page = 0; page < 4; ++page)
        cartridge.data[static_cast<size_t>(page) * Cartridge::kPageSize] = static_cast<uint8_t>(0xC0 + page);
    Cpc cpc;
    cpc.setCartridge(&cartridge);
    cpc.fitXmem(true);
    for (int place = 0; place < Xmem::kRoms; ++place)
        cpc.xmem()->setRom(place, pattern(place));
    Memory& memory = cpc.memory();
    const auto fit = [&](bool boot) {
        Xmem::Switches switches;
        switches.bootRom = boot;
        cpc.xmem()->setSwitches(switches);
        memory.setXmem(cpc.xmem());
        memory.reset();
    };
    fit(false);
    memory.selectUpperRom(0);
    CHECK(memory.read(0x0000) == 0xC0 && memory.read(0xC000) == 0xC1);  // the cartridge's firmware and BASIC
    memory.selectUpperRom(7);
    CHECK_EQ(memory.read(0xC000), 0xC3);  // its AMSDOS
    memory.selectUpperRom(9);
    CHECK_EQ(memory.read(0xC000), 0x80 | 9);  // the card's ROMs are there all the same
    fit(true);
    memory.selectUpperRom(0);
    CHECK(memory.read(0x0000) == (0x80 | 7) && memory.read(0xC000) == 0x80);
    memory.selectUpperRom(7);
    CHECK_EQ(memory.read(0xC000), 0xC3);
    // The lower ROM moved to &4000 by the Plus's own register: that is the
    // cartridge's page there, and RAM at the bottom.
    memory.setRmr2(0x08 | 2);
    memory.write(0x0000, 0x11);
    CHECK(memory.read(0x4000) == 0xC2 && memory.read(0x0000) == 0x11);
    memory.setRmr2(0x00);
    CHECK_EQ(memory.read(0x0000), 0x80 | 7);
    memory.setRomEnables(false, false);
    CHECK_EQ(memory.read(0x0000), 0x11);
}

// In the machine: the RAM as the card's own example counts it, the ROMs
// through the machine's ROM select, and its files.
void testMachine()
{
    const auto dir = std::filesystem::temp_directory_path() / "tuxape_xmem_test";
    std::filesystem::remove_all(dir);
    const std::string file = (dir / "cards" / "xmem.flash").string();

    Cpc cpc;
    MachineConfig config;  // no ROM images: the machine is not run
    config.ram = RamExpansion::None;
    config.cards[2] = Card::Xmem;
    config.xmem.ram6128 = false;
    config.xmem.romFree = true;
    config.xmemFlash = file;
    CHECK(config.hasCard(Card::Xmem) && !MachineConfig().hasCard(Card::Xmem));
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK(cpc.xmem() != nullptr && cpc.memory().hasXmem());
    Memory& memory = cpc.memory();
    CHECK_EQ(memory.ramSizeKb(), 576);
    CHECK_EQ(Memory::ramSizeKb(RamExpansion::None, false, 0xFF), 576);
    CHECK_EQ(Memory::ramSizeKb(RamExpansion::Internal, false, 0xFE), 576);
    CHECK_EQ(Memory::ramSizeKb(RamExpansion::None, false, 0xFE), 512);

    // 32 pages of 16K at &4000, each its own, and none of them the base
    // RAM's: a bank that is not there leaves the base RAM in its place.
    const auto pages = [&] {
        for (int i = 0; i < 32; ++i) {
            memory.selectRamBank(static_cast<uint8_t>(0xC4 + i / 4 * 8 + i % 4), 0x7F);
            memory.write(0x4000, static_cast<uint8_t>(i + 1));
        }
        memory.selectRamBank(0xC0, 0x7F);
        memory.write(0x4000, 0xEE);
        int found = 0;
        for (int i = 0; i < 32; ++i) {
            memory.selectRamBank(static_cast<uint8_t>(0xC4 + i / 4 * 8 + i % 4), 0x7F);
            found += memory.read(0x4000) == i + 1;
        }
        memory.selectRamBank(0xC0, 0x7F);
        return memory.read(0x4000) == 0xEE ? found : -1;
    };
    CHECK_EQ(pages(), 32);
    // On 6128 in a machine of 64K, the first bank is nobody's.
    config.xmem.ram6128 = true;
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK_EQ(memory.ramSizeKb(), 512);
    CHECK_EQ(pages(), 28);
    // In a 6128 it is the machine's.
    config.ram = RamExpansion::Internal;
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK_EQ(memory.ramSizeKb(), 576);
    CHECK_EQ(pages(), 32);

    // The flash through the machine: what is written in the top 16K goes
    // to the chip as well as to the RAM, whether the ROM shows or not.
    const auto command = [&](uint8_t value) {
        memory.selectUpperRom(3);
        memory.write(0xD555, 0xAA);
        memory.selectUpperRom(2);
        memory.write(0xEAAA, 0x55);
        memory.selectUpperRom(3);
        memory.write(0xD555, value);
    };
    memory.reset();
    memory.setRomEnables(false, true);
    command(0x90);
    memory.selectUpperRom(2);
    CHECK(memory.read(0xC000) == 0xBF && memory.read(0xC001) == 0xB7);
    command(0xF0);
    memory.selectUpperRom(2);
    CHECK_EQ(memory.read(0xC000), 0xFF);
    const char text[] = "X-MEM";
    for (int i = 0; text[i]; ++i) {
        command(0xA0);
        memory.selectUpperRom(9);
        memory.write(static_cast<uint16_t>(0xC000 + i), static_cast<uint8_t>(text[i]));
    }
    memory.selectUpperRom(9);
    CHECK(memory.read(0xC000) == 'X' && memory.read(0xC004) == 'M');
    CHECK_EQ(memory.readRam(0xC004), 'M');  // the RAM got it too
    memory.setRomEnables(false, false);
    CHECK_EQ(memory.read(0xC000), 'X');
    memory.write(0xC000, 0x00);  // the RAM alone: no command was given
    memory.setRomEnables(false, true);
    CHECK_EQ(memory.read(0xC000), 'X');
    // ROM 7 and ROM 0 are the machine's: empty here, which shows as &FF.
    memory.selectUpperRom(7);
    CHECK_EQ(memory.read(0xC000), 0xFF);

    // The file: written when asked, and once the card has changed only.
    CHECK(cpc.xmem()->changed());
    CHECK(!std::filesystem::exists(file));
    CHECK(saveCards(cpc));
    CHECK(!cpc.xmem()->changed());
    const auto kept = readFile(file);
    CHECK(kept && kept->size() == Xmem::kFlashSize && (*kept)[9 * Xmem::kRomSize] == 'X' && (*kept)[0] == 0xFF);
    std::filesystem::remove(file);
    CHECK(saveCards(cpc) && !std::filesystem::exists(file));
    // Taken out, the card's RAM and ROMs go with it; what it held last is
    // in its file, and is back with the card.
    command(0xA0);
    memory.selectUpperRom(9);
    memory.write(0xC005, '!');
    config.cards[2] = Card::None;
    CHECK(applyMachine(cpc, config, dir, nullptr));
    CHECK(cpc.xmem() == nullptr && !memory.hasXmem());
    CHECK_EQ(memory.ramSizeKb(), 128);
    memory.setRomEnables(false, true);
    memory.selectUpperRom(9);
    CHECK_EQ(memory.read(0xC000), 0xFF);
    config.cards[0] = Card::Xmem;
    CHECK(applyMachine(cpc, config, dir, nullptr));
    memory.setRomEnables(false, true);
    memory.selectUpperRom(9);
    CHECK(memory.read(0xC000) == 'X' && memory.read(0xC005) == '!');
    CHECK(!cpc.xmem()->changed());
    // Another file: another card's worth of ROMs, erased if there is none.
    config.xmemFlash = (dir / "other.flash").string();
    CHECK(applyMachine(cpc, config, dir, nullptr));
    memory.setRomEnables(false, true);
    memory.selectUpperRom(9);
    CHECK_EQ(memory.read(0xC000), 0xFF);

    std::filesystem::remove_all(dir);
}

struct Machine {
    Cpc cpc;
    AutoType keys{cpc.keyboard()};
    void frames(int count)
    {
        for (int i = 0; i < count; ++i) {
            keys.frame();
            cpc.runFrame();
        }
    }
    bool waitFor(const char* text, int seconds)
    {
        for (int i = 0; i < 50 * seconds; ++i) {
            frames(1);
            if (!keys.active() && contains(readScreenText(cpc), text))
                return true;
        }
        std::printf("no \"%s\" on the screen:\n%s\n", text, readScreenText(cpc).c_str());
        ++g_failures;
        return false;
    }
};

// The card's own disc: its INSTALL writes the firmware, its extension and
// the BASIC into the flash; the machine then starts from the card.
void testInstall(const std::filesystem::path& folder)
{
    const auto image = readFile(folder / "xmem" / "xmem_side-A.dsk");
    auto disc = image ? Disc::fromDsk(*image) : std::nullopt;
    Machine m;
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    config.cards[0] = Card::Xmem;
    config.xmem.romFree = true;
    if (!disc || !applyMachine(m.cpc, config, defaultRomDir(), nullptr)) {
        std::printf("the X-MEM's disc or the ROM images were not found: its INSTALL is not run\n");
        return;
    }
    m.cpc.coldReset();
    m.cpc.fdc().setFast(true);
    m.cpc.fdc().drive(0).disc = std::make_unique<Disc>(std::move(*disc));
    CHECK(m.waitFor("Ready", 10));
    CHECK(contains(readScreenText(m.cpc), "128K"));
    m.frames(50);  // the keyboard is not listened to at once

    m.keys.type("RUN\"INSTALL\n");
    CHECK(m.waitFor("3- Spanish keymap", 30));
    m.keys.type("2");
    CHECK(m.waitFor("3- 448K RAM drive", 10));
    m.keys.type("1");
    CHECK(m.waitFor("2- No extra install", 10));
    m.keys.type("2");
    CHECK(m.waitFor("BASIC 1.1    :", 120));
    m.frames(25);
    const std::string report = readScreenText(m.cpc);
    CHECK(contains(report, "Firmware 3.15: PASSED"));
    CHECK(contains(report, "Firmware Exp.: PASSED"));
    CHECK(contains(report, "BASIC 1.1    : PASSED"));
    CHECK(!contains(report, "FAILED"));
    CHECK(m.cpc.xmem()->changed());
    // What the chip now holds.
    const std::span<const uint8_t> flash = m.cpc.xmem()->flash();
    CHECK(flash[Xmem::kLowerPlace * Xmem::kRomSize] == 0x01);        // LD BC,&7F89, as every firmware starts
    CHECK(flash[Xmem::kBasicPlace * Xmem::kRomSize] == 0x80);        // the BASIC's type
    CHECK(flash[1 * Xmem::kRomSize] <= 0x02);                         // a ROM in place 1
    CHECK(flash[2 * Xmem::kRomSize] == 0xFF);                         // none in place 2

    // BOOT on ROM, the power off and on: the firmware is the card's, and
    // says how much RAM it finds.
    config.xmem.bootRom = true;
    CHECK(applyMachine(m.cpc, config, defaultRomDir(), nullptr));
    m.cpc.coldReset();
    CHECK(m.waitFor("Ready", 20));
    // Firmware 3.15's own first line, in 80 columns. (What it finds, "RAM:576K
    // | CRTC0 | FW3.15-FR", is in inverse video, which is not read here.)
    const std::string booted = readScreenText(m.cpc);
    CHECK(contains(booted, "AMSTRAD Colour Personal Computer"));
    CHECK(contains(booted, "BASIC 1.1"));
    CHECK_EQ(m.cpc.memory().ramSizeKb(), 576);
    // BOOT back on CPC: the machine's own firmware again, the card's ROMs
    // still there for it.
    config.xmem.bootRom = false;
    CHECK(applyMachine(m.cpc, config, defaultRomDir(), nullptr));
    m.cpc.coldReset();
    CHECK(m.waitFor("Ready", 20));
    CHECK(contains(readScreenText(m.cpc), "Amstrad 128K Microcomputer"));
}

// The X-MASS with it, from the two cards' own disc: its INSTALL puts
// CubeMDOS in ROM 2, its FORMAT makes the IDE disc a FAT16 volume, and
// BASIC then saves a program there and finds it again.
void testXmass(const std::filesystem::path& folder)
{
    const auto image = readFile(folder / "xmem" / "xmass_xmem-install.dsk");
    auto disc = image ? Disc::fromDsk(*image) : std::nullopt;
    const auto dir = std::filesystem::temp_directory_path() / "tuxape_xmass_test";
    std::filesystem::remove_all(dir);
    Machine m;
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    config.cards[0] = Card::Xmem;
    config.cards[3] = Card::Xmass;
    config.xmem.romFree = true;
    config.xmassDisc = (dir / "xmass.img").string();
    if (!disc || !applyMachine(m.cpc, config, defaultRomDir(), nullptr)) {
        std::printf("the X-MASS's disc or the ROM images were not found: its FORMAT is not run\n");
        return;
    }
    m.cpc.coldReset();
    m.cpc.fdc().setFast(true);
    m.cpc.fdc().drive(0).disc = std::make_unique<Disc>(std::move(*disc));
    CHECK(m.waitFor("Ready", 10));
    m.frames(50);
    // The English keymap: what is typed here is typed on a British CPC.
    m.keys.type("RUN\"INSTALL\n");
    CHECK(m.waitFor("3- Spanish keymap", 30));
    m.keys.type("1");
    CHECK(m.waitFor("2- No extra install", 10));
    m.keys.type("2");
    CHECK(m.waitFor("CUBEMDOS     :", 200));
    m.frames(25);
    CHECK(contains(readScreenText(m.cpc), "CUBEMDOS     : PASSED"));
    CHECK(!contains(readScreenText(m.cpc), "FAILED"));

    config.xmem.bootRom = true;
    CHECK(applyMachine(m.cpc, config, defaultRomDir(), nullptr));
    m.cpc.coldReset();
    CHECK(m.waitFor("Ready", 20));
    m.frames(50);
    // The disc is CubeMDOS's own at first: FORMAT is on the floppy.
    m.keys.type("|A\n");
    m.frames(100);
    m.keys.type("RUN\"FORMAT\n");
    CHECK(m.waitFor("Choose 1,2 or 3", 60));
    CHECK(contains(readScreenText(m.cpc), "Controller Model: X-MASS DOM"));
    m.keys.type("2");
    CHECK(m.waitFor("Success", 300));
    CHECK(m.cpc.ide() && m.cpc.ide()->good());
    // A partition of 400 MB in the first sector, as a PC would have it:
    // its kind (6: FAT16), its first sector and its length.
    const auto written = readFile(config.xmassDisc);
    CHECK(written && written->size() > 2048 * 512);
    if (written && written->size() > 2048 * 512) {
        const std::vector<uint8_t>& w = *written;
        CHECK(w[510] == 0x55 && w[511] == 0xAA && w[0x1BE + 4] == 0x06);
        CHECK_EQ(w[0x1BE + 8] | w[0x1BE + 9] << 8, 2048);
        CHECK_EQ(w[0x1BE + 12] | w[0x1BE + 13] << 8 | w[0x1BE + 14] << 16, 817152);
        CHECK(w[2048 * 512 + 510] == 0x55 && w[2048 * 512 + 511] == 0xAA);
    }

    m.keys.type(" ");  // "Press any key to reboot"
    CHECK(m.waitFor("BASIC 1.1", 20));
    m.frames(100);
    m.keys.type("|D\n|FREE\n");
    CHECK(m.waitFor("Free   398 MB", 20));
    m.keys.type("10 PRINT \"kept on the X-MASS\"\nSAVE\"HELLO\"\nNEW\nCAT\n");
    CHECK(m.waitFor("HELLO   .BAS", 30));
    m.keys.type("RUN\"HELLO\"\n");
    CHECK(m.waitFor("\nkept on the X-MASS", 30));
    std::filesystem::remove_all(dir);
}

// A program's own installer, which flashes its ROMs from BASIC with the
// firmware's |FLASH: the iMPsuite's, as its owner runs it on the real
// card. Then IMP Draw on a Plus started from that card, and the keys that
// take it to its load screen, which is UniDOS's doing, in ROM 5.
void testSuite(const std::filesystem::path& folder)
{
    const auto first = readFile(folder / "xmem" / "xmem_side-A.dsk");
    const auto second = readFile(folder / "xmem" / "iMPsuite_230807.hfe");
    auto disc = first ? Disc::fromImage(*first) : std::nullopt;
    auto suite = second ? Disc::fromImage(*second) : std::nullopt;
    Machine m;
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    config.cards[0] = Card::Xmem;
    config.xmem.romFree = true;
    if (!disc || !suite || !applyMachine(m.cpc, config, defaultRomDir(), nullptr)) {
        std::printf("the iMPsuite's disc was not found: its installer is not run\n");
        return;
    }
    m.cpc.coldReset();
    m.cpc.fdc().setFast(true);
    m.cpc.fdc().drive(0).disc = std::make_unique<Disc>(std::move(*disc));
    CHECK(m.waitFor("Ready", 10));
    m.frames(50);
    m.keys.type("RUN\"INSTALL\n");
    CHECK(m.waitFor("3- Spanish keymap", 30));
    m.keys.type("1");
    CHECK(m.waitFor("3- 448K RAM drive", 10));
    m.keys.type("1");
    CHECK(m.waitFor("2- No extra install", 10));
    m.keys.type("2");
    CHECK(m.waitFor("BASIC 1.1    :", 200));
    m.frames(25);

    config.xmem.bootRom = true;
    CHECK(applyMachine(m.cpc, config, defaultRomDir(), nullptr));
    m.cpc.coldReset();
    CHECK(m.waitFor("Ready", 20));
    m.frames(50);
    m.cpc.fdc().drive(0).disc = std::make_unique<Disc>(std::move(*suite));
    m.keys.type("RUN\"#INSTALL\n");
    for (const char* question : {"Install iMPdraw", "Install rom addON", "Install JarvisOS", "Install Unidos", "Install iMPtiles"}) {
        CHECK(m.waitFor(question, 300));
        m.keys.type("Y\n");
    }
    CHECK(m.waitFor("iMPsuite Ready", 300));
    Xmem& card = *m.cpc.xmem();
    CHECK(card.romInfo(Xmem::kLowerPlace).firmware);
    CHECK(card.romInfo(0).kind == 0x80 && card.romInfo(0).name == "BASIC");
    CHECK(card.romInfo(1).name == "FW315 EXP");
    CHECK(card.romInfo(2).present && card.romInfo(2).kind == 1);     // IMP Draw
    CHECK(card.romInfo(3).present && card.romInfo(4).present && card.romInfo(6).present);
    CHECK(card.romInfo(5).name.rfind("UNIDOS", 0) == 0);
    CHECK(card.romInfo(23).kind == 2 && card.romInfo(24).kind == 2 && card.romInfo(25).kind == 2);
    CHECK(!card.romInfo(8).present && !card.romInfo(22).present);

    // The same card in a Plus: it starts from the card's firmware.
    MachineConfig plus = stockMachine(CpcModel::Plus6128);
    plus.cards = config.cards;
    plus.xmem = config.xmem;
    const std::vector<uint8_t> flash(card.flash().begin(), card.flash().end());
    Machine p;
    if (!applyMachine(p.cpc, plus, defaultRomDir(), nullptr)) {
        std::printf("no Plus cartridge: the card is not tried in a Plus\n");
        return;
    }
    p.cpc.xmem()->setFlash(flash);
    p.cpc.crtc().setType(CrtcType::AsicPlus);
    p.cpc.coldReset();
    CHECK(p.waitFor("Ready", 20));
    CHECK(contains(readScreenText(p.cpc), "AMSTRAD Colour Personal Computer"));
    p.frames(50);
    p.keys.type("|i\n");
    p.frames(300);
    CHECK(p.cpc.asic().unlocked());
    const auto press = [&](std::initializer_list<CpcKey> keys) {
        for (const CpcKey key : keys) {
            p.cpc.keyboard().set(key, true);
            p.frames(3);
        }
        p.frames(6);
        for (const CpcKey key : keys)
            p.cpc.keyboard().set(key, false);
        p.frames(150);
    };
    press({CpcKey::Copy});
    press({CpcKey::Control, CpcKey::Escape});  // its hardware sprites
    press({CpcKey::Control, CpcKey::L});       // "Sprites - Load .spr"
    p.frames(250);
    int drawn = 0;
    for (int at = 0xC000; at < 0x10000; ++at)
        drawn += p.cpc.memory().readRam(static_cast<uint16_t>(at)) != 0;
    CHECK_EQ(p.cpc.gateArray().mode(), 1);
    CHECK(drawn > 1000);  // a black screen in mode 2 is what it is without UniDOS in ROM 5
}

}  // namespace

int main(int argc, char* argv[])
{
    testPlaces();
    testFlash();
    testRomInfo();
    testPlus();
    testMachine();
    if (argc > 1) {
        testInstall(argv[1]);
        testXmass(argv[1]);
        testSuite(argv[1]);
    }
    return checkSummary("xmem");
}
