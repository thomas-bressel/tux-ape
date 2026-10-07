#pragma once

#include <QColor>

// The colours the application chooses itself are held to WCAG 2's level
// AAA: text stands out from what is behind it by 7 to 1 at least, and what
// is drawn to tell a state (a mark, a light, a button's frame) by 3 to 1.
// The colours of the user's desktop are not the application's to choose;
// those it picks are fitted to them with the functions below, whatever the
// theme, light or dark.

inline constexpr double kTextContrast = 7.0;     // WCAG 1.4.6, contrast (enhanced)
inline constexpr double kGraphicContrast = 3.0;  // WCAG 1.4.11, non-text contrast

// WCAG's relative luminance of a colour, 0 for black to 1 for white, and
// the contrast of two colours, 1 (none) to 21 (black on white).
double relativeLuminance(const QColor& colour);
double contrastRatio(const QColor& one, const QColor& other);

// The colour nearest to `wanted`, of the same hue, that stands out from
// `against` by `ratio` at least: `wanted` itself if it does, darker or
// lighter otherwise, down to black or white (which is as far as it goes:
// on a middle grey nothing reaches 7 to 1). It serves both ways: a
// colour for text on a background that is given, and a background for a
// text whose colour is given.
QColor readable(const QColor& wanted, const QColor& against, double ratio = kTextContrast);
// The same against two backgrounds at once, such as the rows of a list
// that are shaded in turn.
QColor readable(const QColor& wanted, const QColor& against, const QColor& andAgainst, double ratio = kTextContrast);

// Black or white: the one that reads better on a background.
QColor inkOn(const QColor& background);
