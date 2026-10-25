// Fitting a machine from a description of its RAM and ROMs, as WinAPE's
// Memory settings and profiles give it: finding ROM images by name, the
// stock machines, and changes made to a machine that is running.
//
// Needs the ROM images (see setup.h); exits with code 77, which CTest
// reports as "skipped", when they are not available.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "core/cpc.h"
#include "core/discfiles.h"
#include "core/files.h"
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
    CHECK(has("PARADOS_1-2+"));
    CHECK(!has("CPC_PLUS"));  // a cartridge, not a ROM image
    // In alphabetical order whatever the case: "OS664" before "PARADOS"
    // before "PARADOS_1-2".
    auto place = [&](const char* name) { return std::find(names.begin(), names.end(), name) - names.begin(); };
    CHECK(place("AMSDOS") < place("BASIC1-0"));
    CHECK(place("OS664") < place("PARADOS"));
    CHECK(place("PARADOS") < place("PARADOS_1-2"));
    CHECK(romNames(romDir / "no such folder").empty());

    // The machines' own in system/, ParaDOS in dos/.
    const std::filesystem::path system = romDir / "system";
    CHECK(findRom("OS6128", romDir) == system / "OS6128.ROM");
    CHECK(findRom("os6128", romDir) == system / "OS6128.ROM");
    CHECK(findRom("OS6128.ROM", romDir) == system / "OS6128.ROM");
    CHECK(findRom("os6128.rom", romDir) == system / "OS6128.ROM");
    // WinAPE's names, which its profiles give.
    CHECK(findRom("parados 1-2+", romDir) == romDir / "dos/PARADOS_1-2+.ROM");
    CHECK(findRom("ParaDOS 1-2", romDir) == romDir / "dos/PARADOS_1-2.ROM");
    CHECK(findRom((system / "AMSDOS.ROM").string(), romDir / "elsewhere") == system / "AMSDOS.ROM");
    CHECK(findRom("", romDir).empty());
    CHECK(findRom("NOPE", romDir).empty());
    CHECK(findRom("C:\\ROMS\\OS6128.ROM", romDir).empty());
    // A file of another kind is only found under its full name.
    CHECK(findRom("cpc_plus", romDir).empty());
    CHECK(findRom("cpc_plus.cpr", romDir) == system / "CPC_PLUS.CPR");
}

// One roms/ for every ROM, as dist/ has it: the machines' own in system/,
// the others in folders of their kind, names in upper case with "_" between
// their parts. Profiles that ask for WinAPE's names find them.
void testOneRomFolder()
{
    const std::filesystem::path roms = std::filesystem::temp_directory_path() / "tuxape_machine_setup_roms";
    std::filesystem::remove_all(roms);
    const std::vector<uint8_t> image(16384, 0xC9);
    for (const char* file : {"system/OS6128.ROM", "system/BASIC1-1.ROM", "system/CPC_PLUS.CPR", "dos/PARADOS_1-2+.ROM",
                             "xmem/XMEM_FW_3-15_FR.ROM", "firmware/PLUS_FR.CPR"}) {
        std::filesystem::create_directories((roms / file).parent_path());
        CHECK(writeFile(roms / file, image));
    }
    const std::string kept = std::getenv("TUXAPE_USER_ROM_DIR") ? std::getenv("TUXAPE_USER_ROM_DIR") : "";
    setenv("TUXAPE_USER_ROM_DIR", roms.c_str(), 1);

    CHECK(findRom("OS6128", roms) == roms / "system/OS6128.ROM");
    CHECK(findRom("basic1-1", roms) == roms / "system/BASIC1-1.ROM");
    CHECK(findRom("ParaDOS 1-2+", roms) == roms / "dos/PARADOS_1-2+.ROM");
    CHECK(findRom("parados_1_2+", roms) == roms / "dos/PARADOS_1-2+.ROM");
    CHECK(findRom("XMEM_FW_3_15_FR", roms) == roms / "xmem/XMEM_FW_3-15_FR.ROM");
    CHECK(findRom("cpc_plus.cpr", roms) == roms / "system/CPC_PLUS.CPR");
    CHECK(findRom("PLUS_FR.cpr", roms) == roms / "firmware/PLUS_FR.CPR");
    // The separators are alike, not left out: BASIC1-1 is not BASIC11.
    CHECK(findRom("BASIC11", roms).empty());
    CHECK(findRom("ParaDOS 1-2", roms).empty());
    const std::vector<std::string> names = romNames(roms);
    CHECK(names.size() == 4 && std::find(names.begin(), names.end(), "PARADOS_1-2+") != names.end());

    if (kept.empty())
        unsetenv("TUXAPE_USER_ROM_DIR");
    else
        setenv("TUXAPE_USER_ROM_DIR", kept.c_str(), 1);
    std::filesystem::remove_all(roms);
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

// A ROM image kept as a file on a CPC disc, as those burnt from BASIC are,
// comes with AMSDOS's header: the machine gets the image without it.
void testRomWithHeader(const std::filesystem::path& romDir)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "tuxape_machine_setup_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto amsdos = readFile(romDir / "system/AMSDOS.ROM");
    CHECK(amsdos && amsdos->size() == Memory::kBankSize);
    if (!amsdos)
        return;
    AmsdosHeader header;
    header.loadAddress = 0xC000;
    header.length = static_cast<int>(amsdos->size());
    std::vector<uint8_t> file = makeAmsdosHeader("AMSDOS.ROM", header);
    CHECK_EQ(file.size(), 128u);
    file.insert(file.end(), amsdos->begin(), amsdos->end());
    CHECK(writeFile(dir / "Headed.rom", file));
    CHECK(readRom(dir / "Headed.rom") == amsdos);
    CHECK(readRom(romDir / "system/AMSDOS.ROM") == amsdos);  // as it is without one
    CHECK(!readRom(dir / "Gone.rom"));

    // A header that speaks of another length is not this file's: such
    // bytes are the image's own.
    std::vector<uint8_t> odd = file;
    odd.resize(odd.size() - 256);
    CHECK(writeFile(dir / "Odd.rom", odd));
    CHECK(readRom(dir / "Odd.rom") == odd);

    // In the machine: the ROM answers where it should, and the firmware
    // takes it for the ROM it is.
    Cpc cpc;
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    config.upperRoms[7] = (dir / "Headed.rom").string();
    std::string error;
    CHECK(applyMachine(cpc, config, romDir, &error));
    CHECK(error.empty());
    Memory& memory = cpc.memory();
    memory.reset();
    memory.selectUpperRom(7);
    memory.setRomEnables(false, true);
    bool same = true;
    for (int at = 0; at < Memory::kBankSize; ++at)
        same = same && memory.read(static_cast<uint16_t>(0xC000 + at)) == (*amsdos)[static_cast<size_t>(at)];
    CHECK(same);
    CHECK(contains(screenAfterBoot(cpc), "Ready"));
    // BASIC's HIMEM (&AE5E): below the room AMSDOS took for itself.
    CHECK_EQ(memory.readRam(0xAE5E) | memory.readRam(0xAE5F) << 8, 0xA67B);

    std::filesystem::remove_all(dir);
}

int main()
{
    // Without ROM images: files of its own.
    testOneRomFolder();
    const std::filesystem::path romDir = defaultRomDir();
    Cpc probe;
    if (!setupStockMachine(probe, CpcModel::Cpc6128, romDir, nullptr)) {
        std::printf("ROM images not found in %s; the rest skipped\n", romDir.string().c_str());
        return checkSummary("machine_setup") == 0 ? 77 : 1;
    }
    testRomNames(romDir);
    testStockMachines(romDir);
    testRam();
    testRomChoices(romDir);
    testRomWithHeader(romDir);
    return checkSummary("machine_setup");
}
