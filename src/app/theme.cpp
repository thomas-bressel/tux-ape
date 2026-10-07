#include "theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

#include "icons.h"

namespace {

Theme g_theme = Theme::Light;

}  // namespace

QPalette themePalette(Theme theme)
{
    const bool dark = theme == Theme::Dark;
    const auto pick = [dark](const char* light, const char* darkOne) { return QColor(dark ? darkOne : light); };
    QPalette look;
    look.setColor(QPalette::Window, pick("#F0F0F0", "#1E1E1E"));
    look.setColor(QPalette::WindowText, pick("#000000", "#FFFFFF"));
    look.setColor(QPalette::Base, pick("#FFFFFF", "#121212"));
    look.setColor(QPalette::AlternateBase, pick("#F2F2F2", "#1C1C1C"));
    look.setColor(QPalette::Text, pick("#000000", "#FFFFFF"));
    look.setColor(QPalette::Button, pick("#E6E6E6", "#333333"));
    look.setColor(QPalette::ButtonText, pick("#000000", "#FFFFFF"));
    look.setColor(QPalette::BrightText, pick("#FFFFFF", "#FFFFFF"));
    look.setColor(QPalette::ToolTipBase, pick("#FFFFE0", "#333333"));
    look.setColor(QPalette::ToolTipText, pick("#000000", "#FFFFFF"));
    look.setColor(QPalette::PlaceholderText, pick("#595959", "#A6A6A6"));
    // What is selected: white on a deep blue, or black on a pale one.
    look.setColor(QPalette::Highlight, pick("#0B3D91", "#9CC8FF"));
    look.setColor(QPalette::HighlightedText, pick("#FFFFFF", "#000000"));
    look.setColor(QPalette::Link, pick("#0000A0", "#9CC8FF"));
    look.setColor(QPalette::LinkVisited, pick("#5A0080", "#D7A8FF"));
    // The edges the style draws frames and bevels with.
    look.setColor(QPalette::Light, pick("#FFFFFF", "#5A5A5A"));
    look.setColor(QPalette::Midlight, pick("#F7F7F7", "#454545"));
    look.setColor(QPalette::Mid, pick("#8C8C8C", "#5A5A5A"));
    look.setColor(QPalette::Dark, pick("#6E6E6E", "#101010"));
    look.setColor(QPalette::Shadow, pick("#000000", "#000000"));
    // What cannot be used is paler: it is not there to be read.
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        look.setColor(QPalette::Disabled, role, pick("#6E6E6E", "#8C8C8C"));
    look.setColor(QPalette::Disabled, QPalette::Highlight, pick("#B0B0B0", "#4A4A4A"));
    look.setColor(QPalette::Disabled, QPalette::HighlightedText, pick("#000000", "#FFFFFF"));
    return look;
}

void applyTheme(Theme theme)
{
    g_theme = theme;
    if (!QApplication::style() || QApplication::style()->objectName().compare("fusion", Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(themePalette(theme));
    refreshThemedIcons();
}

Theme currentTheme()
{
    return g_theme;
}
