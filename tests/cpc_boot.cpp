// Boots each stock machine with its real ROMs and checks what appears on
// screen, then types at the BASIC prompt. This exercises the whole machine:
// CPU, memory mapping, video, interrupts and the keyboard path.
//
// Needs the ROM images (see setup.h); exits with code 77, which CTest
// reports as "skipped", when they are not available.

#include <string>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/screen_text.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

bool contains(const std::string& text, const char* what)
{
    return text.find(what) != std::string::npos;
}

std::string bootScreen(CpcModel model, CrtcType crtc)
{
    Cpc cpc;
    cpc.crtc().setType(crtc);
    std::string error;
    if (!setupStockMachine(cpc, model, defaultRomDir(), &error)) {
        std::printf("%s\n", error.c_str());
        ++g_failures;
        return {};
    }
    for (int frame = 0; frame < 150; ++frame)
        cpc.runFrame();
    return readScreenText(cpc);
}

void testBanners()
{
    const std::string cpc464 = bootScreen(CpcModel::Cpc464, CrtcType::HD6845S);
    CHECK(contains(cpc464, "Amstrad 64K Microcomputer  (v1)"));
    CHECK(contains(cpc464, "BASIC 1.0"));
    CHECK(contains(cpc464, "Ready"));

    const std::string cpc664 = bootScreen(CpcModel::Cpc664, CrtcType::HD6845S);
    CHECK(contains(cpc664, "Amstrad 64K Microcomputer  (v2)"));
    CHECK(contains(cpc664, "BASIC 1.1"));
    CHECK(contains(cpc664, "Ready"));

    for (int type = 0; type <= 4; ++type) {
        const std::string cpc6128 = bootScreen(CpcModel::Cpc6128, static_cast<CrtcType>(type));
        CHECK(contains(cpc6128, "Amstrad 128K Microcomputer  (v3)"));
        CHECK(contains(cpc6128, "BASIC 1.1"));
        CHECK(contains(cpc6128, "Ready"));
    }
}

void testTyping()
{
    Cpc cpc;
    std::string error;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), &error)) {
        ++g_failures;
        return;
    }
    for (int frame = 0; frame < 150; ++frame)
        cpc.runFrame();

    AutoType autoType(cpc.keyboard());
    autoType.type("10 FOR i=1 TO 3:PRINT \"Hello\";i*i:NEXT\nRUN\nPRINT HIMEM,355/113\n");
    while (autoType.active()) {
        autoType.frame();
        cpc.runFrame();
    }
    for (int frame = 0; frame < 50; ++frame)
        cpc.runFrame();

    const std::string screen = readScreenText(cpc);
    CHECK(contains(screen, "Hello 1\nHello 4\nHello 9\nReady"));
    // AMSDOS takes its workspace from the top of memory.
    CHECK(contains(screen, " 42619"));
    CHECK(contains(screen, " 3.14159292"));
    if (g_failures)
        std::printf("screen was:\n%s\n", screen.c_str());
}

}  // namespace

int main()
{
    Cpc probe;
    if (!setupStockMachine(probe, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        std::printf("ROM images not found in %s; skipping\n", defaultRomDir().string().c_str());
        return 77;
    }
    testBanners();
    testTyping();
    return checkSummary("cpc_boot");
}
