#include "contrast.h"

#include <algorithm>
#include <cmath>

namespace {

double linear(int channel)
{
    const double c = channel / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// `colour` with its lightness moved a share of the way to black (share
// below 0) or to white (above 0).
QColor moved(const QColor& colour, double share)
{
    const QColor rgb = colour.toRgb();
    const int target = share < 0 ? 0 : 255;
    const double by = std::abs(share);
    return QColor(static_cast<int>(std::lround(rgb.red() + (target - rgb.red()) * by)),
                  static_cast<int>(std::lround(rgb.green() + (target - rgb.green()) * by)),
                  static_cast<int>(std::lround(rgb.blue() + (target - rgb.blue()) * by)));
}

}  // namespace

double relativeLuminance(const QColor& colour)
{
    const QColor rgb = colour.toRgb();
    return 0.2126 * linear(rgb.red()) + 0.7152 * linear(rgb.green()) + 0.0722 * linear(rgb.blue());
}

double contrastRatio(const QColor& one, const QColor& other)
{
    const double a = relativeLuminance(one), b = relativeLuminance(other);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

QColor inkOn(const QColor& background)
{
    return contrastRatio(Qt::black, background) >= contrastRatio(Qt::white, background) ? QColor(Qt::black) : QColor(Qt::white);
}

QColor readable(const QColor& wanted, const QColor& against, double ratio)
{
    if (contrastRatio(wanted, against) >= ratio)
        return wanted.toRgb();
    // Towards the end that is furthest from what it has to stand out from.
    const double way = inkOn(against) == QColor(Qt::black) ? -1.0 : 1.0;
    double low = 0.0, high = 1.0;
    for (int step = 0; step < 16; ++step) {
        const double middle = (low + high) / 2;
        if (contrastRatio(moved(wanted, way * middle), against) >= ratio)
            high = middle;
        else
            low = middle;
    }
    return moved(wanted, way * high);
}

QColor readable(const QColor& wanted, const QColor& against, const QColor& andAgainst, double ratio)
{
    // The harder of the two first; what suits it nearly always suits the
    // other, and the second pass sees to it when it does not.
    const bool firstHarder = contrastRatio(wanted, against) <= contrastRatio(wanted, andAgainst);
    QColor colour = readable(wanted, firstHarder ? against : andAgainst, ratio);
    colour = readable(colour, firstHarder ? andAgainst : against, ratio);
    return readable(colour, firstHarder ? against : andAgainst, ratio);
}
