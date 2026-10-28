#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace tuxape {

// The firmware's key translation tables (KM): the character each of the 80
// keys gives alone, with SHIFT and with CONTROL. They are what makes a
// CPC's keyboard English, French, Spanish, Danish or German: the keys are
// the same everywhere, the tables are not. Key numbers are CpcKey's.
struct KeyTables {
    std::array<uint8_t, 80> normal{};
    std::array<uint8_t, 80> shift{};
    std::array<uint8_t, 80> control{};
};

// The tables in a firmware ROM, or in RAM where a program may have changed
// them: found by their first sixteen keys, the cursor and function keys,
// which give the same codes in every language; the SHIFT and CONTROL tables
// follow the normal one. Nothing if they are not there.
std::optional<KeyTables> findKeyTables(std::span<const uint8_t> data);
// Those of the English firmware, for a machine whose tables are not found.
const KeyTables& englishKeyTables();

// The language a firmware's tables are for, which also says which codes its
// character set draws as accented letters.
enum class KeyboardLanguage : uint8_t { English, French, Spanish, Danish, German };
KeyboardLanguage keyboardLanguage(const KeyTables& tables);

// The code of the CPC's character set that shows a Unicode character in
// that language, or -1 if the machine has no such character. ASCII stands
// for itself, even where the language draws its code as an accented letter
// (the French "{" is "é" on the screen).
int cpcCharCode(char32_t c, KeyboardLanguage language);

// The other way: the character a code of the CPC's set shows in that
// language (the French &7B is "é"), or 0 for a code that shows nothing.
char32_t cpcCharacter(uint8_t code, KeyboardLanguage language);

// What a key has printed on it, as the keyboard that goes with those tables
// has it. The keys that give no character have the same English word on
// every CPC ("ESC", "SHIFT", "COPY", "f7", an arrow); the others their
// characters: one for a letter, its capital; otherwise the one it gives
// with SHIFT over the one it gives alone ("2" over "é" on a French CPC).
struct KeyLegend {
    const char* word = nullptr;  // UTF-8
    char32_t top = 0, bottom = 0;
};
KeyLegend keyLegend(const KeyTables& tables, uint8_t key);

// The key that gives a code, and whether SHIFT or CONTROL goes with it:
// alone first, then with SHIFT, then with CONTROL. False if no key does.
struct KeyStroke {
    uint8_t key = 0;  // a CpcKey
    bool shift = false;
    bool control = false;
};
bool keyForCode(const KeyTables& tables, uint8_t code, KeyStroke& stroke);

}  // namespace tuxape
