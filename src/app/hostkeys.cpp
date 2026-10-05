#include "hostkeys.h"

#include <QKeyEvent>

#include "core/keymap.h"

using namespace tuxape;

namespace {

#if defined(Q_OS_LINUX)

// Qt reports X11-style key codes on both X11 and Wayland: the Linux input
// event code plus 8. Event codes up to F12 are the PC scan codes themselves;
// the keys that DirectInput marks as "extended" are numbered differently.
uint8_t fromNativeScanCode(quint32 native)
{
    if (native < 8)
        return PcNone;
    const quint32 code = native - 8;
    if (code <= 0x58)
        return static_cast<uint8_t>(code);
    switch (code) {
    case 96: return PcNumEnter;
    case 97: return PcRightControl;
    case 98: return PcNumDivide;
    case 100: return PcRightAlt;
    case 102: return PcHome;
    case 103: return PcUp;
    case 104: return PcPageUp;
    case 105: return PcLeft;
    case 106: return PcRight;
    case 107: return PcEnd;
    case 108: return PcDown;
    case 109: return PcPageDown;
    case 110: return PcInsert;
    case 111: return PcDelete;
    default: return PcNone;
    }
}

#elif defined(Q_OS_WIN)

// Windows gives the scan code with bit 8 set for extended keys, which
// DirectInput folds into bit 7.
uint8_t fromNativeScanCode(quint32 native)
{
    const uint8_t code = native & 0x7F;
    return (native & 0x100) ? code | 0x80 : code;
}

#else

uint8_t fromNativeScanCode(quint32)
{
    return PcNone;
}

#endif

// Used when the platform gives no scan code. This follows what the key
// produces rather than where it is, so it only matches the intended layout
// on a US or UK keyboard.
uint8_t fromQtKey(const QKeyEvent* event)
{
    const int key = event->key();
    const bool keypad = event->modifiers() & Qt::KeypadModifier;

    if (keypad) {
        switch (key) {
        case Qt::Key_0: case Qt::Key_Insert: return PcNum0;
        case Qt::Key_1: case Qt::Key_End: return PcNum1;
        case Qt::Key_2: case Qt::Key_Down: return PcNum2;
        case Qt::Key_3: case Qt::Key_PageDown: return PcNum3;
        case Qt::Key_4: case Qt::Key_Left: return PcNum4;
        case Qt::Key_5: case Qt::Key_Clear: return PcNum5;
        case Qt::Key_6: case Qt::Key_Right: return PcNum6;
        case Qt::Key_7: case Qt::Key_Home: return PcNum7;
        case Qt::Key_8: case Qt::Key_Up: return PcNum8;
        case Qt::Key_9: case Qt::Key_PageUp: return PcNum9;
        case Qt::Key_Period: case Qt::Key_Comma: case Qt::Key_Delete: return PcNumPeriod;
        case Qt::Key_Plus: return PcNumPlus;
        case Qt::Key_Minus: return PcNumMinus;
        case Qt::Key_Asterisk: return PcNumMultiply;
        case Qt::Key_Slash: return PcNumDivide;
        case Qt::Key_Enter: return PcNumEnter;
        }
    }

    static constexpr uint8_t letters[26] = {
        PcA, PcB, PcC, PcD, PcE, PcF, PcG, PcH, PcI, PcJ, PcK, PcL, PcM,
        PcN, PcO, PcP, PcQ, PcR, PcS, PcT, PcU, PcV, PcW, PcX, PcY, PcZ,
    };
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return letters[key - Qt::Key_A];
    if (key >= Qt::Key_1 && key <= Qt::Key_9)
        return static_cast<uint8_t>(Pc1 + (key - Qt::Key_1));

    switch (key) {
    case Qt::Key_0: return Pc0;
    case Qt::Key_Escape: return PcEscape;
    case Qt::Key_Tab: case Qt::Key_Backtab: return PcTab;
    case Qt::Key_Backspace: return PcBackspace;
    case Qt::Key_Return: return PcReturn;
    case Qt::Key_Enter: return PcNumEnter;
    case Qt::Key_Space: return PcSpace;
    case Qt::Key_Shift: return PcLeftShift;
    case Qt::Key_Control: return PcLeftControl;
    case Qt::Key_Alt: return PcLeftAlt;
    case Qt::Key_AltGr: return PcRightAlt;
    case Qt::Key_CapsLock: return PcCapsLock;
    case Qt::Key_Up: return PcUp;
    case Qt::Key_Down: return PcDown;
    case Qt::Key_Left: return PcLeft;
    case Qt::Key_Right: return PcRight;
    case Qt::Key_Home: return PcHome;
    case Qt::Key_End: return PcEnd;
    case Qt::Key_PageUp: return PcPageUp;
    case Qt::Key_PageDown: return PcPageDown;
    case Qt::Key_Insert: return PcInsert;
    case Qt::Key_Delete: return PcDelete;
    case Qt::Key_Minus: case Qt::Key_Underscore: return PcMinus;
    case Qt::Key_Equal: case Qt::Key_Plus: return PcEquals;
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return PcLeftBracket;
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: return PcRightBracket;
    case Qt::Key_Semicolon: case Qt::Key_Colon: return PcSemicolon;
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return PcApostrophe;
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return PcGrave;
    case Qt::Key_Backslash: case Qt::Key_Bar: return PcBackslash;
    case Qt::Key_Comma: case Qt::Key_Less: return PcComma;
    case Qt::Key_Period: case Qt::Key_Greater: return PcPeriod;
    case Qt::Key_Slash: case Qt::Key_Question: return PcSlash;
    default: return PcNone;
    }
}

}  // namespace

uint8_t pcKeyFromEvent(const QKeyEvent* event)
{
    if (const uint8_t code = fromNativeScanCode(event->nativeScanCode()))
        return code;
    return fromQtKey(event);
}

int numLockFromEvent(const QKeyEvent* event)
{
    if (!(event->modifiers() & Qt::KeypadModifier))
        return -1;
    // With Num Lock on the keypad sends digits; off, it sends navigation keys.
    switch (event->key()) {
    case Qt::Key_0: case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4:
    case Qt::Key_5: case Qt::Key_6: case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
    case Qt::Key_Period: case Qt::Key_Comma:
        return 1;
    case Qt::Key_Insert: case Qt::Key_End: case Qt::Key_Down: case Qt::Key_PageDown:
    case Qt::Key_Left: case Qt::Key_Clear: case Qt::Key_Right: case Qt::Key_Home:
    case Qt::Key_Up: case Qt::Key_PageUp: case Qt::Key_Delete:
        return 0;
    default:
        return -1;
    }
}
