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

// The key that gives a code, and whether SHIFT or CONTROL goes with it:
// alone first, then with SHIFT, then with CONTROL. False if no key does.
struct KeyStroke {
    uint8_t key = 0;  // a CpcKey
    bool shift = false;
    bool control = false;
};
bool keyForCode(const KeyTables& tables, uint8_t code, KeyStroke& stroke);

}  // namespace tuxape
