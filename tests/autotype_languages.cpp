// Auto Type on the CPCs of other languages: the keys are found in the
// machine's own key tables, and accented letters are typed as the codes
// its character set shows them with. Each firmware of the user's ROM
// folder that is there boots, is typed lines of PRINT ASC(...) and shows
// the codes it was given.
//
// Needs the firmware images of roms/firmware (see setup.h); a language
// whose image is not there is left out, and the test is skipped (77) when
// none is.

#include <cctype>
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
    const char* file;    // its names, as they may be spelt, between '|'; any case
    KeyboardLanguage language;
    const char* typed;   // UTF-8
    const char* shown;   // what the screen then has, spaces and line breaks aside; lines between '|'
};

// The letters that move between AZERTY, QWERTY and QWERTZ, the digits, and
// each language's own characters.
const Case kCases[] = {
    {"os6128_uk.rom", KeyboardLanguage::English,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"z\");ASC(\"w\");ASC(\"m\");ASC(\"y\");ASC(\";\");ASC(\":\");ASC(\"£\")\n",
     "97 113 122 119 109 121 59 58 163"},
    {"os6128_fr.rom", KeyboardLanguage::French,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"z\");ASC(\"w\");ASC(\"m\");ASC(\"y\");ASC(\";\");ASC(\":\");12+3\n"
     "PRINT ASC(\"é\");ASC(\"à\");ASC(\"ç\");ASC(\"ù\");ASC(\"è\")\n",
     "97 113 122 119 109 121 59 58 15|123 64 92 124 125"},
    {"os6128_es.rom|os6128_s.rom", KeyboardLanguage::Spanish,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"z\");ASC(\":\");ASC(\"ñ\");ASC(\"Ñ\")\n",
     "97 113 122 58 171 161"},
    {"os6128_dk.rom", KeyboardLanguage::Danish,
     "PRINT ASC(\"a\");ASC(\"q\");ASC(\"æ\");ASC(\"ø\");ASC(\"å\");ASC(\"Æ\");ASC(\"Ø\");ASC(\"Å\")\n",
     "97 113 123 124 125 91 92 93"},
};

// The file of the folder that has one of those names, whatever its case.
std::filesystem::path findFile(const std::filesystem::path& folder, const std::string& names)
{
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ignored)) {
        std::string name = entry.path().filename().string();
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (("|" + names + "|").find("|" + name + "|") != std::string::npos)
            return entry.path();
    }
    return {};
}

// What the keys have printed on them: the words, and characters read from
// the tables in each language's own font.
void checkLegends(const KeyTables& tables, KeyboardLanguage language)
{
    const auto key = [&](CpcKey k) { return keyLegend(tables, static_cast<uint8_t>(k)); };
    const auto word = [&](CpcKey k) { return std::string(key(k).word ? key(k).word : ""); };
    CHECK(word(CpcKey::Escape) == std::string("ESC"));
    CHECK(word(CpcKey::Control) == std::string("CONTROL"));
    CHECK(word(CpcKey::CursorLeft) == std::string("\u2190"));
    CHECK(word(CpcKey::F7) == std::string("f7"));
    // A letter: its capital alone.
    CHECK(!key(CpcKey::K).word && key(CpcKey::K).top == U'K' && !key(CpcKey::K).bottom);
    switch (language) {
    case KeyboardLanguage::English:
        CHECK(key(CpcKey::Num2).top == U'"' && key(CpcKey::Num2).bottom == U'2');
        CHECK(key(CpcKey::Caret).top == U'£' && key(CpcKey::Caret).bottom == U'↑');
        CHECK(key(CpcKey::Q).top == U'Q');
        break;
    case KeyboardLanguage::French:
        CHECK(key(CpcKey::Num2).top == U'2' && key(CpcKey::Num2).bottom == U'é');
        CHECK(key(CpcKey::Q).top == U'A');
        CHECK(key(CpcKey::Semicolon).top == U'%' && key(CpcKey::Semicolon).bottom == U'ù');
        break;
    case KeyboardLanguage::Spanish:
        CHECK(key(CpcKey::Colon).top == U'Ñ' && !key(CpcKey::Colon).bottom);
        // The Spanish keyboard's own words.
        CHECK(word(CpcKey::Copy) == "COPIA" && word(CpcKey::CapsLock) == "FIJA MAYS" && word(CpcKey::Enter) == "INTRO");
        CHECK(word(CpcKey::Control) == "CONTROL" && word(CpcKey::Return) == "RETURN");
        break;
    case KeyboardLanguage::Danish:
        CHECK(key(CpcKey::Colon).top == U'Æ' && !key(CpcKey::Colon).bottom);
        break;
    case KeyboardLanguage::German: break;
    }
}

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
    if (tables)
        checkLegends(*tables, *language);
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
    checkLegends(englishKeyTables(), KeyboardLanguage::English);
    // The older keyboards' words: ENTER on the large key, CTRL; and the
    // 464's keypad, figures alone.
    const auto on = [](KeyboardModel model, CpcKey key) {
        const char* word = keyLegend(englishKeyTables(), static_cast<uint8_t>(key), model).word;
        return std::string(word ? word : "");
    };
    CHECK(on(KeyboardModel::Cpc6128, CpcKey::Return) == "RETURN" && on(KeyboardModel::Cpc6128, CpcKey::F7) == "f7");
    CHECK(on(KeyboardModel::Cpc664, CpcKey::Return) == "ENTER" && on(KeyboardModel::Cpc664, CpcKey::Control) == "CTRL");
    CHECK(on(KeyboardModel::Cpc664, CpcKey::F7) == "f7" && on(KeyboardModel::Cpc464, CpcKey::F7) == "7");
    CHECK(on(KeyboardModel::Cpc464, CpcKey::Enter) == "ENTER" && on(KeyboardModel::Cpc464, CpcKey::FDot) == ".");
    CHECK(keyLegend(german, static_cast<uint8_t>(CpcKey::Y)).top == U'Z');
    CHECK(cpcCharacter(0x7B, KeyboardLanguage::German) == U'ä');
    CHECK(cpcCharacter(0x7B, KeyboardLanguage::English) == U'{');
    CHECK(cpcCharacter(0x00, KeyboardLanguage::English) == 0);
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
        const std::filesystem::path file = findFile(folder, test.file);
        if (file.empty())
            continue;
        ++booted;
        KeyboardLanguage language = KeyboardLanguage::English;
        bool inRam = false;
        const std::string screen = typeOn(file, test.typed, &language, &inRam);
        std::printf("%s: tables in RAM too: %s\n", file.filename().string().c_str(), inRam ? "yes" : "no");
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
            std::printf("%s, screen was:\n%s\n", file.filename().string().c_str(), screen.c_str());
    }
    if (booted == 0) {
        std::printf("no firmware of roms/firmware: skipped\n");
        return 77;
    }
    return checkSummary("autotype_languages");
}
