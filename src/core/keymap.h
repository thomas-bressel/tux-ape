#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/keyboard.h"

namespace tuxape {

// Physical keys of a PC keyboard, identified as WinAPE does: by their
// DirectInput scan code. Only the keys the default layout uses are named.
enum PcKey : uint8_t {
    PcNone = 0x00,
    PcEscape = 0x01, Pc1, Pc2, Pc3, Pc4, Pc5, Pc6, Pc7, Pc8, Pc9, Pc0,
    PcMinus = 0x0C, PcEquals, PcBackspace, PcTab,
    PcQ = 0x10, PcW, PcE, PcR, PcT, PcY, PcU, PcI, PcO, PcP,
    PcLeftBracket = 0x1A, PcRightBracket, PcReturn, PcLeftControl,
    PcA = 0x1E, PcS, PcD, PcF, PcG, PcH, PcJ, PcK, PcL,
    PcSemicolon = 0x27, PcApostrophe, PcGrave, PcLeftShift, PcBackslash,
    PcZ = 0x2C, PcX, PcC, PcV, PcB, PcN, PcM,
    PcComma = 0x33, PcPeriod, PcSlash, PcRightShift, PcNumMultiply, PcLeftAlt, PcSpace, PcCapsLock,
    PcNum7 = 0x47, PcNum8, PcNum9, PcNumMinus, PcNum4, PcNum5, PcNum6, PcNumPlus,
    PcNum1 = 0x4F, PcNum2, PcNum3, PcNum0, PcNumPeriod,
    PcOem102 = 0x56,
    PcNumEnter = 0x9C, PcRightControl = 0x9D, PcNumDivide = 0xB5, PcRightAlt = 0xB8,
    PcHome = 0xC7, PcUp, PcPageUp, PcLeft = 0xCB, PcRight = 0xCD, PcEnd = 0xCF,
    PcDown = 0xD0, PcPageDown, PcInsert, PcDelete,
};

// Which PC keys press which CPC keys. Each CPC key can be reached by up to
// three PC keys, and there is one full set for each state of Num Lock, so
// that the numeric keypad can serve as the function keys or as the joystick.
// This is the content of WinAPE's .kbd files.
class KeyMap {
public:
    static constexpr int kAlternatives = 3;

    // Starts with WinAPE's default layout.
    KeyMap() { setDefault(); }
    void setDefault();

    uint8_t pcKey(bool numLock, CpcKey key, int alternative) const
    {
        return table_[numLock][static_cast<int>(key)][alternative];
    }
    void setPcKey(bool numLock, CpcKey key, int alternative, uint8_t pcKey)
    {
        table_[numLock][static_cast<int>(key)][alternative] = pcKey;
    }

    // CPC keys a PC key presses.
    std::vector<CpcKey> cpcKeys(uint8_t pcKey, bool numLock) const;

    // .kbd file format.
    bool load(std::span<const uint8_t> file);
    std::vector<uint8_t> save() const;

private:
    uint8_t table_[2][kCpcKeyCount][kAlternatives];
};

}  // namespace tuxape
