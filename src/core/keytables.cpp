#include "core/keytables.h"

#include <algorithm>

namespace tuxape {

namespace {

// The first sixteen keys of the normal table: cursors, the keypad's
// function keys, COPY. The same in every firmware.
constexpr uint8_t kStart[16] = {0xF0, 0xF3, 0xF1, 0x89, 0x86, 0x83, 0x8B, 0x8A,
                                0xF2, 0xE0, 0x87, 0x88, 0x85, 0x81, 0x82, 0x80};

// The English firmware's tables (OS 6128 1.1, &1EEF in its ROM).
constexpr uint8_t kEnglish[240] = {
    // normal
    0xF0, 0xF3, 0xF1, 0x89, 0x86, 0x83, 0x8B, 0x8A, 0xF2, 0xE0, 0x87, 0x88, 0x85, 0x81, 0x82, 0x80,
    0x10, 0x5B, 0x0D, 0x5D, 0x84, 0xFF, 0x5C, 0xFF, 0x5E, 0x2D, 0x40, 0x70, 0x3B, 0x3A, 0x2F, 0x2E,
    0x30, 0x39, 0x6F, 0x69, 0x6C, 0x6B, 0x6D, 0x2C, 0x38, 0x37, 0x75, 0x79, 0x68, 0x6A, 0x6E, 0x20,
    0x36, 0x35, 0x72, 0x74, 0x67, 0x66, 0x62, 0x76, 0x34, 0x33, 0x65, 0x77, 0x73, 0x64, 0x63, 0x78,
    0x31, 0x32, 0xFC, 0x71, 0x09, 0x61, 0xFD, 0x7A, 0x0B, 0x0A, 0x08, 0x09, 0x58, 0x5A, 0xFF, 0x7F,
    // shift
    0xF4, 0xF7, 0xF5, 0x89, 0x86, 0x83, 0x8B, 0x8A, 0xF6, 0xE0, 0x87, 0x88, 0x85, 0x81, 0x82, 0x80,
    0x10, 0x7B, 0x0D, 0x7D, 0x84, 0xFF, 0x60, 0xFF, 0xA3, 0x3D, 0x7C, 0x50, 0x2B, 0x2A, 0x3F, 0x3E,
    0x5F, 0x29, 0x4F, 0x49, 0x4C, 0x4B, 0x4D, 0x3C, 0x28, 0x27, 0x55, 0x59, 0x48, 0x4A, 0x4E, 0x20,
    0x26, 0x25, 0x52, 0x54, 0x47, 0x46, 0x42, 0x56, 0x24, 0x23, 0x45, 0x57, 0x53, 0x44, 0x43, 0x58,
    0x21, 0x22, 0xFC, 0x51, 0x09, 0x41, 0xFD, 0x5A, 0x0B, 0x0A, 0x08, 0x09, 0x58, 0x5A, 0xFF, 0x7F,
    // control
    0xF8, 0xFB, 0xF9, 0x89, 0x86, 0x83, 0x8C, 0x8A, 0xFA, 0xE0, 0x87, 0x88, 0x85, 0x81, 0x82, 0x80,
    0x10, 0x1B, 0x0D, 0x1D, 0x84, 0xFF, 0x1C, 0xFF, 0x1E, 0xFF, 0x00, 0x10, 0xFF, 0xFF, 0xFF, 0xFF,
    0x1F, 0xFF, 0x0F, 0x09, 0x0C, 0x0B, 0x0D, 0xFF, 0xFF, 0xFF, 0x15, 0x19, 0x08, 0x0A, 0x0E, 0xFF,
    0xFF, 0xFF, 0x12, 0x14, 0x07, 0x06, 0x02, 0x16, 0xFF, 0xFF, 0x05, 0x17, 0x13, 0x04, 0x03, 0x18,
    0xFF, 0x7E, 0xFC, 0x11, 0xE1, 0x01, 0xFE, 0x1A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F,
};

KeyTables tablesAt(const uint8_t* at)
{
    KeyTables tables;
    std::copy_n(at, 80, tables.normal.begin());
    std::copy_n(at + 80, 80, tables.shift.begin());
    std::copy_n(at + 160, 80, tables.control.begin());
    return tables;
}

struct Substitute {
    char32_t c;
    uint8_t code;
};

// What each language draws in place of ASCII, or beyond it, as its
// firmware's font has it (French, Spanish and Danish read from their ROMs;
// German as the Schneider CPC's DIN set has it).
constexpr Substitute kFrench[] = {{U'à', 0x40}, {U'ç', 0x5C}, {U'é', 0x7B}, {U'ù', 0x7C}, {U'è', 0x7D}, {U'°', 0xA2}};
constexpr Substitute kSpanish[] = {{U'ñ', 0xAB}, {U'Ñ', 0xA1}, {U'₧', 0xA3}};
constexpr Substitute kDanish[] = {{U'Æ', 0x5B}, {U'Ø', 0x5C}, {U'Å', 0x5D}, {U'æ', 0x7B}, {U'ø', 0x7C}, {U'å', 0x7D}};
constexpr Substitute kGerman[] = {{U'§', 0x40}, {U'Ä', 0x5B}, {U'Ö', 0x5C}, {U'Ü', 0x5D},
                                  {U'ä', 0x7B}, {U'ö', 0x7C}, {U'ü', 0x7D}, {U'ß', 0x7E}};
// What every CPC has beyond ASCII.
constexpr Substitute kCommon[] = {{U'£', 0xA3}, {U'¨', 0xA2}, {U'¿', 0xAE}, {U'¡', 0xAF}};

std::span<const Substitute> substitutes(KeyboardLanguage language)
{
    switch (language) {
    case KeyboardLanguage::French: return kFrench;
    case KeyboardLanguage::Spanish: return kSpanish;
    case KeyboardLanguage::Danish: return kDanish;
    case KeyboardLanguage::German: return kGerman;
    case KeyboardLanguage::English: break;
    }
    return {};
}

}  // namespace

std::optional<KeyTables> findKeyTables(std::span<const uint8_t> data)
{
    if (data.size() < 240)
        return std::nullopt;
    const auto found = std::search(data.begin(), data.end() - 239, std::begin(kStart), std::end(kStart));
    if (found == data.end() - 239)
        return std::nullopt;
    return tablesAt(&*found);
}

const KeyTables& englishKeyTables()
{
    static const KeyTables tables = tablesAt(kEnglish);
    return tables;
}

KeyboardLanguage keyboardLanguage(const KeyTables& tables)
{
    // AZERTY: A where the English Q is.
    if (tables.normal[67] == 'a')
        return KeyboardLanguage::French;
    // QWERTZ: Z where the English Y is.
    if (tables.normal[43] == 'z')
        return KeyboardLanguage::German;
    // Ñ beside L.
    if (tables.normal[29] == 0xAB)
        return KeyboardLanguage::Spanish;
    // Æ, Ø and Å beside P.
    if (tables.normal[29] == '{' || tables.normal[26] == '}')
        return KeyboardLanguage::Danish;
    return KeyboardLanguage::English;
}

int cpcCharCode(char32_t c, KeyboardLanguage language)
{
    const std::span<const Substitute> own = substitutes(language);
    for (const Substitute& entry : own)
        if (entry.c == c)
            return entry.code;
    // ASCII stands for itself, even where the language draws its code as
    // something else: "{" in a listing is the byte a program wants.
    if (c == '\n' || c == '\t' || (c >= 0x20 && c < 0x7F))
        return static_cast<int>(c);
    for (const Substitute& entry : kCommon)
        if (entry.c == c && std::none_of(own.begin(), own.end(), [&](const Substitute& s) { return s.code == entry.code; }))
            return entry.code;
    return -1;
}

bool keyForCode(const KeyTables& tables, uint8_t code, KeyStroke& stroke)
{
    if (code == '\n')
        code = 0x0D;  // RETURN
    // Not the joystick's lines, nor the keys that give nothing.
    const auto look = [&](const std::array<uint8_t, 80>& table, bool shift, bool control) {
        for (int key = 0; key < 72; ++key)
            if (table[static_cast<size_t>(key)] == code && code != 0xFF) {
                stroke = {static_cast<uint8_t>(key), shift, control};
                return true;
            }
        return false;
    };
    // DEL is key 79, beyond the keyboard's own lines.
    if (code == 0x7F) {
        stroke = {79, false, false};
        return true;
    }
    return look(tables.normal, false, false) || look(tables.shift, true, false) || look(tables.control, false, true);
}

}  // namespace tuxape
