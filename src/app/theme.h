#pragma once

#include <QPalette>

// The application's two looks, for the user to switch between: light and
// dark. Each is a palette of its own, drawn in the "Fusion" style so that
// it is the same on every desktop, and each keeps WCAG 2's level AAA: any
// text stands out from what is behind it by 7 to 1 at least (see
// contrast.h, which fits to them the few colours chosen elsewhere).
enum class Theme { Light, Dark };

QPalette themePalette(Theme theme);
// Gives the whole application that look, pictograms included.
void applyTheme(Theme theme);
Theme currentTheme();
