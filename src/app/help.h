#pragma once

#include <functional>

class QAbstractButton;

// The help window is the main window's. The Help buttons of the other
// windows, which know nothing of it, ask for it through here.

// Who shows the help: the main window says so as it is made, and takes
// its word back (an empty function) as it goes.
void setHelpHandler(std::function<void()> show);
// Shows the help, if there is anyone to show it.
void requestHelp();
// Makes a Help button, or a menu's entry, ask for it.
void wireHelp(QAbstractButton* button);
