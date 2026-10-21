#pragma once

#include <QList>
#include <QUrl>

// The windows that choose a file, TuxAPE's and its plugins': Qt's own, not
// the system's. Under GNOME the system's is GTK's, run inside TuxAPE beside
// Qt; it would at times stop answering, the windows behind it with it, and
// TuxAPE had to be killed. Qt's is drawn like the rest of TuxAPE.
//
// Called once the application is made, before any window. Each file window
// then has on its side, after Qt's places, the user's folders (Desktop,
// Documents, Music...) and the bookmarks they keep in GNOME's Files.
void useOwnFileDialogs();

// The places useOwnFileDialogs() adds: the user's folders that exist, then
// the bookmarks of GTK's file (gtk-3.0/bookmarks in the configuration
// folder) that are folders here. Read again each time.
QList<QUrl> fileDialogPlaces();
