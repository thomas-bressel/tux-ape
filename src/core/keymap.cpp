#include "core/keymap.h"

#include <array>
#include <cstring>
#include <iterator>

namespace tuxape {

namespace {

struct Binding {
    CpcKey cpc;
    uint8_t pc[KeyMap::kAlternatives];
};

// Keys that mean the same whatever the state of Num Lock. The layout is
// positional: a PC key maps to the CPC key in the same place. But COPY,
// which WinAPE has on Alt: that key is left to the interface, for its
// menus, and COPY is on Insert.
constexpr Binding kCommon[] = {
    {CpcKey::CursorUp, {PcUp}},          {CpcKey::CursorRight, {PcRight}},
    {CpcKey::CursorDown, {PcDown}},      {CpcKey::CursorLeft, {PcLeft}},
    {CpcKey::Enter, {PcNumEnter}},       {CpcKey::Copy, {PcInsert}},
    {CpcKey::F0, {PcNum0}},              {CpcKey::Clr, {PcBackslash}},
    {CpcKey::LeftBracket, {PcRightAlt}}, {CpcKey::Return, {PcReturn}},
    {CpcKey::RightBracket, {PcRightBracket}},
    {CpcKey::Shift, {PcLeftShift, PcRightShift}},
    {CpcKey::Backslash, {PcGrave}},      {CpcKey::Control, {PcLeftControl}},
    {CpcKey::Caret, {PcEquals}},         {CpcKey::Minus, {PcMinus}},
    {CpcKey::At, {PcLeftBracket}},       {CpcKey::Semicolon, {PcApostrophe}},
    {CpcKey::Colon, {PcSemicolon}},      {CpcKey::Slash, {PcSlash}},
    {CpcKey::Period, {PcPeriod}},        {CpcKey::Comma, {PcComma}},
    {CpcKey::Space, {PcSpace}},          {CpcKey::Escape, {PcEscape}},
    {CpcKey::Tab, {PcTab}},              {CpcKey::CapsLock, {PcCapsLock}},
    {CpcKey::Del, {PcBackspace}},
    {CpcKey::Num0, {Pc0}}, {CpcKey::Num1, {Pc1}}, {CpcKey::Num2, {Pc2}}, {CpcKey::Num3, {Pc3}},
    {CpcKey::Num4, {Pc4}}, {CpcKey::Num5, {Pc5}}, {CpcKey::Num6, {Pc6}}, {CpcKey::Num7, {Pc7}},
    {CpcKey::Num8, {Pc8}}, {CpcKey::Num9, {Pc9}},
    {CpcKey::A, {PcA}}, {CpcKey::B, {PcB}}, {CpcKey::C, {PcC}}, {CpcKey::D, {PcD}}, {CpcKey::E, {PcE}},
    {CpcKey::F, {PcF}}, {CpcKey::G, {PcG}}, {CpcKey::H, {PcH}}, {CpcKey::I, {PcI}}, {CpcKey::J, {PcJ}},
    {CpcKey::K, {PcK}}, {CpcKey::L, {PcL}}, {CpcKey::M, {PcM}}, {CpcKey::N, {PcN}}, {CpcKey::O, {PcO}},
    {CpcKey::P, {PcP}}, {CpcKey::Q, {PcQ}}, {CpcKey::R, {PcR}}, {CpcKey::S, {PcS}}, {CpcKey::T, {PcT}},
    {CpcKey::U, {PcU}}, {CpcKey::V, {PcV}}, {CpcKey::W, {PcW}}, {CpcKey::X, {PcX}}, {CpcKey::Y, {PcY}},
    {CpcKey::Z, {PcZ}},
};

// Num Lock off: the keypad is the joystick, corners giving diagonals.
constexpr Binding kNumLockOff[] = {
    {CpcKey::JoyUp, {PcNum8, PcNum7, PcNum9}},
    {CpcKey::JoyDown, {PcNum2, PcNum1, PcNum3}},
    {CpcKey::JoyLeft, {PcNum4, PcNum1, PcNum7}},
    {CpcKey::JoyRight, {PcNum6, PcNum3, PcNum9}},
    {CpcKey::JoyFire2, {PcNum5}},
    {CpcKey::JoyFire1, {PcNumPeriod}},
    {CpcKey::JoyFire3, {PcNumPlus}},
    {CpcKey::Enter, {PcNumEnter, PcEnd}},
};

// Num Lock on: the keypad is the CPC's function keypad.
constexpr Binding kNumLockOn[] = {
    {CpcKey::F1, {PcNum1}}, {CpcKey::F2, {PcNum2}}, {CpcKey::F3, {PcNum3}},
    {CpcKey::F4, {PcNum4}}, {CpcKey::F5, {PcNum5}}, {CpcKey::F6, {PcNum6}},
    {CpcKey::F7, {PcNum7}}, {CpcKey::F8, {PcNum8}}, {CpcKey::F9, {PcNum9}},
    {CpcKey::FDot, {PcNumPeriod}},
};

constexpr size_t kFileSize = 2 + 2 * 10 * KeyMap::kAlternatives * 8;

}  // namespace

namespace {

struct PcKeyName {
    uint8_t key;
    const char* name;
};

// Letters, digits, punctuation by its place on a UK/US keyboard, then the
// keys around them, the cursor block and the numeric keypad.
constexpr PcKeyName kPcKeyNames[] = {
    {PcA, "A"}, {PcB, "B"}, {PcC, "C"}, {PcD, "D"}, {PcE, "E"}, {PcF, "F"}, {PcG, "G"}, {PcH, "H"},
    {PcI, "I"}, {PcJ, "J"}, {PcK, "K"}, {PcL, "L"}, {PcM, "M"}, {PcN, "N"}, {PcO, "O"}, {PcP, "P"},
    {PcQ, "Q"}, {PcR, "R"}, {PcS, "S"}, {PcT, "T"}, {PcU, "U"}, {PcV, "V"}, {PcW, "W"}, {PcX, "X"},
    {PcY, "Y"}, {PcZ, "Z"},
    {Pc0, "0"}, {Pc1, "1"}, {Pc2, "2"}, {Pc3, "3"}, {Pc4, "4"}, {Pc5, "5"}, {Pc6, "6"}, {Pc7, "7"},
    {Pc8, "8"}, {Pc9, "9"},
    {PcGrave, "` (left of 1)"}, {PcMinus, "- (right of 0)"}, {PcEquals, "= (left of Backspace)"},
    {PcLeftBracket, "[ (right of P)"}, {PcRightBracket, "] (left of Return)"},
    {PcSemicolon, "; (right of L)"}, {PcApostrophe, "' (two right of L)"}, {PcBackslash, "# or \\ (by Return)"},
    {PcOem102, "\\ (right of left Shift)"}, {PcComma, ", (right of M)"}, {PcPeriod, ". (two right of M)"},
    {PcSlash, "/ (left of right Shift)"},
    {PcEscape, "Escape"}, {PcTab, "Tab"}, {PcCapsLock, "Caps Lock"}, {PcBackspace, "Backspace"},
    {PcReturn, "Return"}, {PcSpace, "Space"},
    {PcLeftShift, "Left Shift"}, {PcRightShift, "Right Shift"}, {PcLeftControl, "Left Control"},
    {PcRightControl, "Right Control"}, {PcLeftAlt, "Left Alt"}, {PcRightAlt, "Right Alt (Alt Gr)"},
    {PcUp, "Up"}, {PcDown, "Down"}, {PcLeft, "Left"}, {PcRight, "Right"},
    {PcInsert, "Insert"}, {PcDelete, "Delete"}, {PcHome, "Home"}, {PcEnd, "End"},
    {PcPageUp, "Page Up"}, {PcPageDown, "Page Down"},
    {PcNum0, "Num 0"}, {PcNum1, "Num 1"}, {PcNum2, "Num 2"}, {PcNum3, "Num 3"}, {PcNum4, "Num 4"},
    {PcNum5, "Num 5"}, {PcNum6, "Num 6"}, {PcNum7, "Num 7"}, {PcNum8, "Num 8"}, {PcNum9, "Num 9"},
    {PcNumPeriod, "Num ."}, {PcNumPlus, "Num +"}, {PcNumMinus, "Num -"}, {PcNumMultiply, "Num *"},
    {PcNumDivide, "Num /"}, {PcNumEnter, "Num Enter"},
    {PcScrollLock, "Scroll Lock"},
};

constexpr auto kNamedPcKeys = [] {
    std::array<uint8_t, std::size(kPcKeyNames)> keys{};
    for (size_t i = 0; i < keys.size(); ++i)
        keys[i] = kPcKeyNames[i].key;
    return keys;
}();

}  // namespace

const char* pcKeyName(uint8_t pcKey)
{
    for (const PcKeyName& entry : kPcKeyNames)
        if (entry.key == pcKey)
            return entry.name;
    return nullptr;
}

std::span<const uint8_t> namedPcKeys()
{
    return kNamedPcKeys;
}

bool KeyMap::operator==(const KeyMap& other) const
{
    return std::memcmp(table_, other.table_, sizeof table_) == 0;
}

void KeyMap::setDefault()
{
    std::memset(table_, 0, sizeof table_);
    auto apply = [this](int state, const Binding& b) {
        for (int alt = 0; alt < kAlternatives; ++alt)
            table_[state][static_cast<int>(b.cpc)][alt] = b.pc[alt];
    };
    for (const Binding& b : kCommon) {
        apply(0, b);
        apply(1, b);
    }
    for (const Binding& b : kNumLockOff)
        apply(0, b);
    for (const Binding& b : kNumLockOn)
        apply(1, b);
}

std::vector<CpcKey> KeyMap::cpcKeys(uint8_t pcKey, bool numLock) const
{
    std::vector<CpcKey> keys;
    if (pcKey == PcNone)
        return keys;
    for (int k = 0; k < kCpcKeyCount; ++k)
        for (int alt = 0; alt < kAlternatives; ++alt)
            if (table_[numLock][k][alt] == pcKey) {
                keys.push_back(static_cast<CpcKey>(k));
                break;
            }
    return keys;
}

// A .kbd file is "KF" followed, for Num Lock off then on, by the ten matrix
// lines; each line holds three rows of eight scan codes, one row per
// alternative, one code per matrix bit.

bool KeyMap::load(std::span<const uint8_t> file)
{
    if (file.size() != kFileSize || file[0] != 'K' || file[1] != 'F')
        return false;
    size_t pos = 2;
    for (int state = 0; state < 2; ++state)
        for (int line = 0; line < 10; ++line)
            for (int alt = 0; alt < kAlternatives; ++alt)
                for (int bit = 0; bit < 8; ++bit)
                    table_[state][line * 8 + bit][alt] = file[pos++];
    return true;
}

std::vector<uint8_t> KeyMap::save() const
{
    std::vector<uint8_t> file = {'K', 'F'};
    file.reserve(kFileSize);
    for (int state = 0; state < 2; ++state)
        for (int line = 0; line < 10; ++line)
            for (int alt = 0; alt < kAlternatives; ++alt)
                for (int bit = 0; bit < 8; ++bit)
                    file.push_back(table_[state][line * 8 + bit][alt]);
    return file;
}

}  // namespace tuxape
