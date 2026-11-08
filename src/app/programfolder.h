#pragma once

#include <QString>

// The folder TuxAPE is installed in, where what it comes with is looked
// for (roms, profile, languages...): the program's own folder; on a Mac,
// the folder that holds TuxAPE.app, for nothing is to be written inside an
// application.
QString programFolder();

// Where the user's own files go (their discs, quick saves, saved profiles,
// the cards' files, the Library, assembler sources, plug-ins): the
// program's folder when TuxAPE may write there, as in a folder unpacked
// from its archive; else, installed where only the system writes (by the
// package, in /opt), a TuxAPE folder in the user's home, made the first
// time with the folders and the files TuxAPE comes with. Never a hidden
// folder: what the user makes is to be found, and backed up.
QString userFolder();

// What userFolder() does to `home` from `program`'s folders: the first time
// (no `home` yet), its folders and the files TuxAPE comes with; at the first
// start of another `version`, the files it does not have yet, no folder
// made again; otherwise nothing. The version done is kept in
// home/.tuxape-version.
void fillUserFolder(const QString& program, const QString& home, const QString& version);
