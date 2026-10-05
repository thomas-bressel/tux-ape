#pragma once

#include <cstdint>

class QKeyEvent;

// Identifies the physical key behind a Qt key event as a DirectInput scan
// code (see core/keymap.h), which is what WinAPE's keyboard layouts are
// written in. Returns 0 for keys that have no such code.
uint8_t pcKeyFromEvent(const QKeyEvent* event);

// Tells whether a key event from the numeric keypad shows Num Lock to be on
// or off. Returns -1 when the event says nothing about it.
int numLockFromEvent(const QKeyEvent* event);
