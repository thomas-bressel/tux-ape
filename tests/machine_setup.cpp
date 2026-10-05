// Fitting a machine from a description of its RAM and ROMs, as WinAPE's
// Memory settings and profiles give it: finding ROM images by name, the
// stock machines, and changes made to a machine that is running.
//
// Needs the ROM images (see setup.h); exits with code 77, which CTest
// reports as "skipped", when they are not available.

#include <algorithm>
#include <string>

#include "check.h"
#include "core/cpc.h"
#include "core/screen_text.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

bool contains(const std::string& text, const char* what)
{
    return text.find(what) != std::string::npos;
}

std::string screenAfterBoot(Cpc& cpc)
{
    cpc.coldReset();
    for (int frame = 0; frame < 150; ++frame)
        cpc.runFrame();
    return readScreenText(cpc);
}

void testRomNames(const std::filesystem::path& romDir)
{
    const std::vector<std::string> names = romNames(romDir);
    auto has = [&](const char* name) { return std::find(names.begin(), names.end(), name) != names.end(); };
    CHECK(has("AMSDOS"));
    CHECK(has("BASIC1-1"));
    CHECK(has("OS6128"));
    CHECK(has("ParaDOS 1-2+"));
    CHECK(!has("CPC_PLUS"));  // a cartridge, not a ROM image
    // In alphabetical order whatever the case: "OS664" before "PARADOS"
    // before "ParaDOS 1-2".
    auto place = [&](const char* name) { return std::find(names.begin(), names.end(), name) - names.begin(); };
    CHECK(place("AMSDOS") < place("BASIC1-0"));
    CHECK(place("OS664") < place("PARADOS"));
    CHECK(place("PARADOS") < place("ParaDOS 1-2"));
    CHECK(romNames(romDir / "no such folder").empty());

    CHECK(findRom("OS6128", romDir) == romDir / "OS6128.ROM");
    CHECK(findRom("os6128", romDir) == romDir / "OS6128.ROM");
    CHECK(findRom("OS6128.ROM", romDir) == romDir / "OS6128.ROM");
    CHECK(findRom("os6128.rom", romDir) == romDir / "OS6128.ROM");
    CHECK(findRom("parados 1-2+", romDir) == romDir / "ParaDOS 1-2+.ROM");
    CHECK(findRom("ParaDOS 1-2", romDir) == romDir / "ParaDOS 1-2.ROM");
    CHECK(findRom((romDir / "AMSDOS.ROM").string(), romDir / "elsewhere") == romDir / "AMSDOS.ROM");
    CHECK(findRom("", romDir).empty());
    CHECK(findRom("NOPE", romDir).empty());
    CHECK(findRom("C:\\ROMS\\OS6128.ROM", romDir).empty());
    // A file of another kind is only found under its full name.
    CHECK(findRom("cpc_plus", romDir).empty());
    CHECK(findRom("cpc_plus.cpr", romDir) == romDir / "CPC_PLUS.CPR");
}

void testStockMachines(const std::filesystem::path& romDir)
{
    for (const CpcModel model : {CpcModel::Cpc464, CpcModel::Cpc664, CpcModel::Cpc6128})
        CHECK(modelOf(stockMachine(model)) == model);
    MachineConfig odd;
    CHECK(modelOf(odd) == CpcModel::Cpc6128);
    odd.lowerRom = "/roms/My OS464 (patched).ROM";
    CHECK(modelOf(odd) == CpcModel::Cpc464);

    Cpc cpc;
    std::string error;
    CHECK(applyMachine(cpc, stockMachine(CpcModel::Cpc464), romDir, &error));
    CHECK(error.empty());
    CHECK_EQ(cpc.memory().ramSizeKb(), 64);
    CHECK(cpc.memory().hasUpperRom(0));
    CHECK(!cpc.memory().hasUpperRom(7));
    std::string screen = screenAfterBoot(cpc);
    CHECK(contains(screen, "Amstrad 64K Microcomputer  (v1)"));
    CHECK(contains(screen, "BASIC 1.0"));

    // Another machine is fitted without a reset: the one that was running
    // goes on until then, and the new one shows after it.
    CHECK(applyMachine(cpc, stockMachine(CpcModel::Cpc6128), romDir, &error));
    CHECK_EQ(cpc.memory().ramSizeKb(), 128);
    CHECK(cpc.memory().hasUpperRom(7));
    CHECK(contains(readScreenText(cpc), "BASIC 1.0"));
    screen = screenAfterBoot(cpc);
    CHECK(contains(screen, "Amstrad 128K Microcomputer  (v3)"));
    CHECK(contains(screen, "BASIC 1.1"));
    CHECK(contains(screen, "Ready"));
}

void testRam()
{
    for (const RamExpansion expansion :
         {RamExpansion::None, RamExpansion::Internal, RamExpansion::Dk256, RamExpansion::Yarek4M})
        for (const bool siliconDisc : {false, true}) {
            Memory memory;
            memory.setRam(expansion, siliconDisc);
            CHECK_EQ(Memory::ramSizeKb(expansion, siliconDisc), memory.ramSizeKb());
        }
    CHECK_EQ(Memory::ramSizeKb(RamExpansion::Dk256, true), 576);
    CHECK_EQ(Memory::ramSizeKb(RamExpansion::Yarek4M, false), 4160);

    // RAM added or taken away leaves the rest as it was.
    Memory memory;  // 128K
    memory.baseRam()[0x1234] = 0x5A;
    CHECK(memory.ramPage(1) != nullptr);
    memory.ramPage(1)[0x4321] = 0xA5;
    memory.setRam(RamExpansion::Dk256, false);
    CHECK_EQ(memory.baseRam()[0x1234], 0x5A);
    CHECK_EQ(memory.ramPage(1)[0x4321], 0xA5);
    CHECK(memory.ramPage(4) != nullptr);
    CHECK_EQ(memory.ramPage(4)[0], 0);
    CHECK(memory.ramPage(5) == nullptr);
    memory.ramPage(4)[7] = 0x77;
    memory.setRam(RamExpansion::Dk256, true);
    CHECK_EQ(memory.ramPage(4)[7], 0x77);
    CHECK(memory.ramPage(8) != nullptr);
    memory.setRam(RamExpansion::None, false);
    CHECK_EQ(memory.baseRam()[0x1234], 0x5A);
    CHECK(memory.ramPage(1) == nullptr);
    memory.setRam(RamExpansion::Internal, false);
    CHECK_EQ(memory.baseRam()[0x1234], 0x5A);
    CHECK_EQ(memory.ramPage(1)[0x4321], 0);  // that page had gone
}

void testRomChoices(const std::filesystem::path& romDir)
{
    Cpc cpc;
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    config.upperRoms[5] = "parados";
    config.upperRoms[20] = "AMSDOS";
    CHECK(applyMachine(cpc, config, romDir, nullptr));
    Memory& memory = cpc.memory();
    CHECK(memory.hasUpperRom(0));
    CHECK(memory.hasUpperRom(5));
    CHECK(memory.hasUpperRom(7));
    CHECK(!memory.hasUpperRom(20));  // out of a 16-slot board's reach

    config.rom32 = true;
    CHECK(applyMachine(cpc, config, romDir, nullptr));
    CHECK(memory.hasUpperRom(20));

    config.onlyLower0And7 = true;
    CHECK(applyMachine(cpc, config, romDir, nullptr));
    CHECK(memory.hasUpperRom(0));
    CHECK(!memory.hasUpperRom(5));
    CHECK(memory.hasUpperRom(7));
    CHECK(!memory.hasUpperRom(20));
    memory.reset();
    CHECK_EQ(memory.read(0x0000), 0x01);  // the firmware: LD BC,&7F89

    config.disableAllRoms = true;
    CHECK(applyMachine(cpc, config, romDir, nullptr));
    CHECK(!memory.hasUpperRom(0));
    CHECK(!memory.hasUpperRom(7));
    memory.reset();
    CHECK_EQ(memory.read(0x0000), 0xFF);

    // An image that cannot be read is reported and its place left empty;
    // the others are fitted all the same.
    config = stockMachine(CpcModel::Cpc6128);
    config.upperRoms[3] = "NOPE";
    config.upperRoms[4] = "/nowhere/GONE.ROM";
    std::string error;
    CHECK(!applyMachine(cpc, config, romDir, &error));
    CHECK(contains(error, "NOPE"));
    CHECK(contains(error, "/nowhere/GONE.ROM"));
    CHECK(!contains(error, "OS6128"));
    CHECK(memory.hasUpperRom(0));
    CHECK(!memory.hasUpperRom(3));
    CHECK(!memory.hasUpperRom(4));
    CHECK(memory.hasUpperRom(7));
}

}  // namespace

int main()
{
    const std::filesystem::path romDir = defaultRomDir();
    Cpc probe;
    if (!setupStockMachine(probe, CpcModel::Cpc6128, romDir, nullptr)) {
        std::printf("ROM images not found in %s; skipping\n", romDir.string().c_str());
        return 77;
    }
    testRomNames(romDir);
    testStockMachines(romDir);
    testRam();
    testRomChoices(romDir);
    return checkSummary("machine_setup");
}
