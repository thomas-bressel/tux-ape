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
};

QIcon makeIcon(IconId id);
