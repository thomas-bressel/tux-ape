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

    bool operator==(const KeyTables&) const = default;
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

// The three kinds of CPC keyboard. The 464's and the 664's have the cursor
// keys around COPY above the keypad, ENTER on the large key and CTRL beside
// the space bar; the 464's keypad has bare figures. The 6128's, which the
// Plus machines took, has RETURN, CONTROL and COPY at the left, ENTER
// beside the space bar, and the cursor keys in its keypad.
enum class KeyboardModel : uint8_t { Cpc464, Cpc664, Cpc6128 };

// What a key has printed on it, on that keyboard with those tables. The
// keys that give no character have a word ("ESC", "SHIFT", "f7", an
// arrow), English on every keyboard but the Spanish ("MAYS", "COPIA"); the
// others their characters: one for a letter, its capital; otherwise the
// one it gives with SHIFT over the one it gives alone ("2" over "é" on a
// French CPC), as the keys have them printed.
struct KeyLegend {
    const char* word = nullptr;  // UTF-8
    char32_t top = 0, bottom = 0;
    char32_t topAlso = 0;  // beside the top one: the French "@" key has "\" too
};
KeyLegend keyLegend(const KeyTables& tables, uint8_t key, KeyboardModel model = KeyboardModel::Cpc6128);

// The key that gives a code, and whether SHIFT or CONTROL goes with it:
// alone first, then with SHIFT, then with CONTROL. False if no key does.
struct KeyStroke {
    uint8_t key = 0;  // a CpcKey
    bool shift = false;
    bool control = false;
};
bool keyForCode(const KeyTables& tables, uint8_t code, KeyStroke& stroke);

}  // namespace tuxape
