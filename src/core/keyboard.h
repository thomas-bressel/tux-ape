#pragma once

#include <cstdint>

namespace tuxape {

// Keys of the CPC keyboard and joysticks. The value is the firmware key
// number, which is also the position in the scan matrix: line = value / 8,
// bit = value % 8.
enum class CpcKey : uint8_t {
    CursorUp = 0, CursorRight, CursorDown, F9, F6, F3, Enter, FDot,
    CursorLeft = 8, Copy, F7, F8, F5, F1, F2, F0,
    Clr = 16, LeftBracket, Return, RightBracket, F4, Shift, Backslash, Control,
    Caret = 24, Minus, At, P, Semicolon, Colon, Slash, Period,
    Num0 = 32, Num9, O, I, L, K, M, Comma,
    Num8 = 40, Num7, U, Y, H, J, N, Space,
    Num6 = 48, Num5, R, T, G, F, B, V,
    Num4 = 56, Num3, E, W, S, D, C, X,
    Num1 = 64, Num2, Escape, Q, Tab, A, CapsLock, Z,
    JoyUp = 72, JoyDown, JoyLeft, JoyRight, JoyFire2, JoyFire1, JoyFire3, Del,

    // The second joystick shares matrix line 6 with the keyboard.
    Joy2Up = Num6, Joy2Down = Num5, Joy2Left = R, Joy2Right = T,
    Joy2Fire2 = G, Joy2Fire1 = F, Joy2Fire3 = B,
};

inline constexpr int kCpcKeyCount = 80;

// State of the 10 x 8 key matrix. A pressed key reads as a 0 bit.
class Keyboard {
public:
    Keyboard() { releaseAll(); }

    void releaseAll()
    {
        for (uint8_t& line : lines_)
            line = 0xFF;
    }

    void set(CpcKey key, bool pressed)
    {
        const int n = static_cast<int>(key);
        const uint8_t bit = static_cast<uint8_t>(1 << (n & 7));
        if (pressed)
            lines_[n >> 3] &= ~bit;
        else
            lines_[n >> 3] |= bit;
    }

    bool pressed(CpcKey key) const
    {
        const int n = static_cast<int>(key);
        return !(lines_[n >> 3] & (1 << (n & 7)));
    }

    // What the PSG's I/O port reads with the given matrix line selected.
    uint8_t line(int n) const { return n < 10 ? lines_[n] : 0xFF; }

private:
    uint8_t lines_[10];
};

}  // namespace tuxape
