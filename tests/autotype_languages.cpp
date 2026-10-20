// Auto Type on the CPCs of other languages: the keys are found in the
// machine's own key tables, and accented letters are typed as the codes
// its character set shows them with. Each firmware of the user's ROM
// folder that is there boots, is typed lines of PRINT ASC(...) and shows
// the codes it was given.
//
// Needs the firmware images of roms/firmware (see setup.h); a language
// whose image is not there is left out, and the test is skipped (77) when
// none is.

#include <filesystem>
#include <string>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/keytables.h"
#include "core/screen_text.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

struct Case {
    const char* file;
    KeyboardLanguage language;
    const char* typed;   // UTF-8
    const char* shown;   // what the screen then has, spaces and line breaks aside; lines between '|'
};

// The letters that move between AZERTY, QWERTY and QWERTZ, the digits, and
// each language's own characters.
const Case kCases[] = {
    {"OS6128_UK.rom", KeyboardLanguage::English,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"z\");ASC(\"w\");ASC(\"m\");ASC(\"y\");ASC(\";\");ASC(\":\");ASC(\"£\")\n",
     "97 113 122 119 109 121 59 58 163"},
    {"OS6128_FR.rom", KeyboardLanguage::French,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"z\");ASC(\"w\");ASC(\"m\");ASC(\"y\");ASC(\";\");ASC(\":\");12+3\n"
     "PRINT ASC(\"é\");ASC(\"à\");ASC(\"ç\");ASC(\"ù\");ASC(\"è\")\n",
     "97 113 122 119 109 121 59 58 15|123 64 92 124 125"},
    {"OS6128_S.rom", KeyboardLanguage::Spanish,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"z\");ASC(\":\");ASC(\"ñ\");ASC(\"Ñ\")\n",
     "97 113 122 58 171 161"},
    {"OS6128_DK.rom", KeyboardLanguage::Danish,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"æ\");ASC(\"ø\");ASC(\"å\");ASC(\"Æ\");ASC(\"Ø\");ASC(\"Å\")\n",
     "97 113 123 124 125 91 92 93"},
};

std::string typeOn(const std::filesystem::path& firmware, const char* text, KeyboardLanguage* language, bool* inRam)
{
    Cpc cpc;
    MachineConfig config = stockMachine(CpcModel::Cpc6128);
    // A file of 32K: the firmware, then its BASIC, named for both places.
    config.lowerRom = firmware.string();
    config.upperRoms[0] = firmware.string();
    std::string error;
    if (!applyMachine(cpc, config, defaultRomDir(), &error)) {
        std::printf("%s: %s\n", firmware.filename().string().c_str(), error.c_str());
        ++g_failures;
        return {};
    }
    cpc.reset();
    for (int frame = 0; frame < 150; ++frame)
        cpc.runFrame();
    const auto tables = findKeyTables(cpc.memory().firmware());
    CHECK(tables.has_value());
    // Whether the firmware keeps a copy of them in RAM.
    std::vector<uint8_t> ram(0x10000);
    for (size_t a = 0; a < ram.size(); ++a)
        ram[a] = cpc.memory().readRam(static_cast<uint16_t>(a));
    *inRam = findKeyTables(ram).has_value();
    AutoType autoType(cpc.keyboard());
    if (tables)
        autoType.setKeyTables(*tables);
    *language = autoType.language();
    autoType.type("CLS\n");
    autoType.type(text);
    while (autoType.active()) {
        autoType.frame();
        cpc.runFrame();
    }
    for (int frame = 0; frame < 50; ++frame)
        cpc.runFrame();
    return readScreenText(cpc);
}

void testGermanTables()
{
    // No German firmware to boot: its QWERTZ tables made from the English
    // ones, Y and Z changing places, and the codes its DIN set shows
    // umlauts with.
    KeyTables german = englishKeyTables();
    std::swap(german.normal[43], german.normal[71]);
    std::swap(german.shift[43], german.shift[71]);
    CHECK(keyboardLanguage(german) == KeyboardLanguage::German);
    CHECK(keyboardLanguage(englishKeyTables()) == KeyboardLanguage::English);
    KeyStroke stroke;
    CHECK(keyForCode(german, 'z', stroke) && stroke.key == static_cast<uint8_t>(CpcKey::Y) && !stroke.shift);
    CHECK(keyForCode(german, 'Y', stroke) && stroke.key == static_cast<uint8_t>(CpcKey::Z) && stroke.shift);
    CHECK_EQ(cpcCharCode(U'ä', KeyboardLanguage::German), 0x7B);
    CHECK_EQ(cpcCharCode(U'Ü', KeyboardLanguage::German), 0x5D);
    CHECK_EQ(cpcCharCode(U'ß', KeyboardLanguage::German), 0x7E);
    // What a language has not got is not typed; ASCII is itself everywhere.
    CHECK_EQ(cpcCharCode(U'é', KeyboardLanguage::English), -1);
    CHECK_EQ(cpcCharCode(U'{', KeyboardLanguage::French), '{');
    CHECK_EQ(cpcCharCode(U'£', KeyboardLanguage::French), 0xA3);
    // The tilde of a command is not a German ß: "~RETURN~" stays a key.
    Keyboard keyboard;
    AutoType autoType(keyboard);
    autoType.setKeyTables(german);
    autoType.type("ß~RETURN~");
    bool returnPressed = false;
    for (int frame = 0; frame < 20 && autoType.active(); ++frame) {
        autoType.frame();
        returnPressed = returnPressed || keyboard.pressed(CpcKey::Return);
    }
    CHECK(returnPressed);
}

}  // namespace

int main()
{
    testGermanTables();
    const std::filesystem::path folder = userRomDir() / "firmware";
    int booted = 0;
    for (const Case& test : kCases) {
        const std::filesystem::path file = folder / test.file;
        if (!std::filesystem::exists(file))
            continue;
        ++booted;
        KeyboardLanguage language = KeyboardLanguage::English;
        bool inRam = false;
        const std::string screen = typeOn(file, test.typed, &language, &inRam);
        std::printf("%s: tables in RAM too: %s\n", test.file, inRam ? "yes" : "no");
        CHECK(language == test.language);
        // The screen's lines are 40 characters: what is looked for may run
        // over two. Spaces and line breaks are left aside on both sides.
        std::string flat;
        for (const char c : screen)
            if (c != ' ' && c != '\n')
                flat += c;
        bool shown = true;
        std::string wanted;
        for (const char* c = test.shown;; ++c) {
            if (*c == '|' || *c == '\0') {
                shown = shown && flat.find(wanted) != std::string::npos;
                wanted.clear();
                if (*c == '\0')
                    break;
            } else if (*c != ' ') {
                wanted += *c;
            }
        }
        CHECK(shown);
        if (!shown)
            std::printf("%s, screen was:\n%s\n", test.file, screen.c_str());
    }
    if (booted == 0) {
        std::printf("no firmware of roms/firmware: skipped\n");
        return 77;
    }
    return checkSummary("autotype_languages");
}
