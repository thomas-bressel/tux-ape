#pragma once

#include <QIcon>

// Toolbar pictograms, drawn in code so that they stay sharp at any display
// scale and the project ships no third-party artwork.
enum class IconId {
    Run,
    Pause,
    SingleStep,
    StepOver,
    Registers,
    Assembler,
    Library,
    Disc,
    Cartridge,
    Tape,
    LoadSnapshot,
    SaveSnapshot,
    Settings,
    FullScreen,
    Help,
    Photo,
    // The debugger's buttons.
    LoadData,
    SaveData,
    GoTo,
    Find,
    Breakpoints,
    DataAreas,
    Timers,
    Graphics,
    // The switch between the light look and the dark one.
    Theme,
};

class QAbstractButton;

// The pictograms are drawn for the look in use: their outlines in a
// colour that stands out from the window's.
QIcon makeIcon(IconId id);
// For a button that stays down: the same, and for when it is down the
// pictogram on a plate of the page's colour with a tick in its corner, so
// that the state shows by more than a change of colour.
QIcon makeToggleIcon(IconId id);
// Gives a button its pictogram and has it drawn again whenever the look
// changes (refreshThemedIcons(), which applyTheme() calls).
void setThemedIcon(QAbstractButton* button, IconId id, bool toggle = false);
void refreshThemedIcons();
