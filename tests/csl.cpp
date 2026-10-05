// The CSL script player and the SSM codes a program can send the emulator.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "core/cpc.h"
#include "core/csl.h"
#include "core/files.h"
#include "core/screen_text.h"
#include "core/setup.h"

namespace {

using namespace tuxape;
namespace fs = std::filesystem;

void testSsmCodes()
{
    Cpc cpc;
    cpc.memory().setRomEnables(false, false);
    std::vector<uint16_t> codes;
    cpc.setSsmHook([&](uint16_t code) { codes.push_back(code); });

    const uint8_t program[] = {
        0xED, 0x01, 0xED, 0x02,              // code 0201
        0x00,                                // nop
        0xED, 0x3F, 0x00, 0xED, 0x3E, 0xED, 0x3D,  // the NOP breaks the first pair: code 3D3E
        0xED, 0xFE, 0xED, 0xFF,              // code FFFE
        0xED, 0x77, 0xED, 0x05,              // ED 77 is not an SSM opcode: nothing
        0x00,
        0xED, 0x44, 0xED, 0x06,              // NEG is a real instruction: nothing
        0x00,
        0xED, 0x00, 0xED, 0x00,              // code 0000
        0x76,                                // halt
    };
    uint16_t addr = 0x4000;
    for (uint8_t byte : program)
        cpc.memory().write(addr++, byte);
    cpc.cpu().pc = 0x4000;
    cpc.cpu().sp = 0x8000;
    while (!cpc.cpu().halted)
        cpc.cpu().step();

    CHECK_EQ(codes.size(), 4);
    if (codes.size() == 4) {
        CHECK_EQ(codes[0], 0x0201);
        CHECK_EQ(codes[1], 0x3D3E);
        CHECK_EQ(codes[2], 0xFFFE);
        CHECK_EQ(codes[3], 0x0000);
    }
}

void write(const fs::path& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary) << text;
}

void testScript()
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        std::printf("ROM images not found; script checks skipped\n");
        return;
    }
    const fs::path dir = fs::temp_directory_path() / "tuxape_csl_test";
    fs::create_directories(dir);

    // Windows line ends, comments, tabs, both kinds of quotes, a nested
    // script found whatever its letter case.
    write(dir / "main.csl",
          "; a comment\r\n"
          "csl_version 1.0\r\n"
          "crtc_select 1\t; UM6845R\r\n"
          "reset\r\n"
          "wait 3000000\r\n"
          "key_delay 40000 40000 400000\r\n"
          "key_output 'PRINT \"A;B\";6*7\\(RET)'\r\n"
          "wait 1000000\r\n"
          "csl_load 'INNER'\r\n");
    write(dir / "inner.CSL",
          "screenshot_name \"shot one\"\n"
          "screenshot\n"
          "key_output '{\\(CTR)\\(SHI)\\(ESC)}'   ; the three keys that reset the machine\n"
          "wait 3000000\n"
          "screenshot vsync\n");

    std::vector<std::string> shots;
    std::string firstScreen;
    CslRunner runner(cpc);
    runner.setScreenshotSink([&](const std::string& name) {
        if (shots.empty())
            firstScreen = readScreenText(cpc);
        shots.push_back(name);
        return true;
    });
    const bool ok = runner.run(dir / "main.csl");
    if (!ok)
        std::printf("script error: %s\n", runner.error().c_str());
    CHECK(ok);
    CHECK(cpc.crtc().type() == CrtcType::UM6845R);
    CHECK_EQ(shots.size(), 2);
    if (shots.size() == 2) {
        CHECK(shots[0] == "shot one");
        CHECK(shots[1] == "shot one");
    }
    // The semicolon inside the quotes was typed, not taken for a comment.
    CHECK(firstScreen.find("A;B 42") != std::string::npos);
    // CTRL+SHIFT+ESC restarted the machine: the PRINT is gone.
    const std::string after = readScreenText(cpc);
    CHECK(after.find("42") == std::string::npos);
    CHECK(after.find("BASIC 1.1") != std::string::npos);

    // Errors say where they are.
    write(dir / "bad.csl", "wait 1000\nfrobnicate 3\n");
    CslRunner bad(cpc);
    CHECK(!bad.run(dir / "bad.csl"));
    CHECK(bad.error().find("bad.csl line 2") != std::string::npos);
    CHECK(bad.error().find("frobnicate") != std::string::npos);

    write(dir / "nodisc.csl", "disk_insert 'nowhere.dsk'\n");
    CslRunner noDisc(cpc);
    CHECK(!noDisc.run(dir / "nodisc.csl"));
    CHECK(!noDisc.run(dir / "missing.csl"));

    fs::remove_all(dir);
}

}  // namespace

// A script saves the machine, lets it move on and brings it back.
void testSnapshots()
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr))
        return;
    const fs::path dir = fs::temp_directory_path() / "tuxape_csl_test";
    fs::create_directories(dir);
    write(dir / "snap.csl",
          "reset\n"
          "wait 3000000\n"
          "key_delay 40000 40000 400000\n"
          "key_output 'a=77\\(RET)'\n"
          "wait 1000000\n"
          "snapshot_name 'state'\n"
          "snapshot\n"
          "key_output 'a=1\\(RET)'\n"
          "wait 1000000\n"
          "snapshot_load 'state'\n"
          "key_output 'PRINT a*2\\(RET)'\n"
          "wait 1000000\n");

    CslRunner runner(cpc);
    std::string saved;
    runner.setSnapshotSink([&](const std::string& name, const std::vector<uint8_t>& data) {
        saved = name;
        return writeFile(dir / (name + ".sna"), data);
    });
    const bool ok = runner.run(dir / "snap.csl");
    if (!ok)
        std::printf("script error: %s\n", runner.error().c_str());
    CHECK(ok);
    CHECK(saved == "state");
    CHECK_EQ(runner.snapshotCount(), 1);
    CHECK(readScreenText(cpc).find("154") != std::string::npos);

    write(dir / "missing.csl", "snapshot_load 'nowhere'\n");
    CslRunner other(cpc);
    CHECK(!other.run(dir / "missing.csl"));
    CHECK(other.error().find("nowhere") != std::string::npos);
}

int main()
{
    testSsmCodes();
    testScript();
    testSnapshots();
    return checkSummary("csl");
}
