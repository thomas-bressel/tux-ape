#pragma once

#include <QString>

// The folder TuxAPE is installed in, where the folders beside it are looked
// for (roms, profile, plugins, library, dsk, img...): the program's own
// folder; on a Mac, the folder that holds TuxAPE.app, for nothing is to be
// written inside an application.
QString programFolder();
