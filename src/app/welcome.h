#pragma once

#include <functional>

#include <QPixmap>

class QSplashScreen;

// The picture TuxAPE shows for a moment as it starts, as WinAPE shows its
// own: Tux with a CPC in his flipper, drawn by the project's author.
QPixmap welcomePicture();

// Shows it in the middle of the screen, no taller than half of it, for
// three seconds or until it is clicked; `gone` is then called, once: the
// program's window comes after the picture, not with it. The picture's
// window does away with itself. If the picture is not there, `gone` is
// called at once and nothing is shown.
QSplashScreen* showWelcome(std::function<void()> gone = {}, int milliseconds = 3000);
