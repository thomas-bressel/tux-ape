#pragma once

#include <QPixmap>

class QSplashScreen;

// The picture TuxAPE shows for a moment as it starts, as WinAPE shows its
// own: Tux with a CPC in his flipper, drawn by the project's author.
QPixmap welcomePicture();

// Shows it in the middle of the screen, no taller than half of it, for a
// few seconds or until it is clicked. The window does away with itself
// when its time is up. Nothing if the picture is not there.
QSplashScreen* showWelcome(int milliseconds = 2500);
