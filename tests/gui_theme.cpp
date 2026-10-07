// The colours of the application: the contrast WCAG 2 asks for at its
// level AAA (7 to 1 for text), held by the two looks the user switches
// between, light and dark, and by the colours fitted to them; and the
// switch itself. Runs without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_theme [prefix]   also saves pictures of the windows in each look
//                        as <prefix>theme_light.png, <prefix>theme_dark.png,
//                        <prefix>library_dark.png, <prefix>debugger_dark.png

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "check.h"
#include "contrast.h"
#include "debuggerdialog.h"
#include "emulator.h"
#include "icons.h"
#include "librarydialog.h"
#include "mainwindow.h"
#include "settings.h"
#include "theme.h"

namespace {

bool near(double value, double wanted)
{
    return value > wanted - 0.05 && value < wanted + 0.05;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    qputenv("TUXAPE_LIBRARY_DIR", folder.filePath("no library").toLocal8Bit());
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    // The measure, on values the standard's own examples give.
    CHECK(near(contrastRatio(Qt::black, Qt::white), 21.0));
    CHECK(near(contrastRatio(Qt::white, Qt::white), 1.0));
    CHECK(near(contrastRatio(QColor("#777777"), Qt::white), 4.48));
    CHECK(near(contrastRatio(QColor("#595959"), Qt::white), 7.0));
    CHECK(near(contrastRatio(QColor("#FF0000"), Qt::white), 4.0));
    CHECK(contrastRatio(QColor("#0B3D91"), Qt::white) >= kTextContrast);
    CHECK(inkOn(Qt::white) == QColor(Qt::black) && inkOn(QColor("#202020")) == QColor(Qt::white));

    // A colour fitted to what is behind it: left alone when it reads,
    // taken darker or lighter, with its hue, when it does not.
    CHECK(readable(QColor(0, 0, 128), Qt::white) == QColor(0, 0, 128));
    for (const QColor& paper : {QColor(Qt::white), QColor("#F0F0F0"), QColor("#121212"), QColor("#1E1E1E")}) {
        for (const QColor& wanted : {QColor(200, 0, 0), QColor(0, 128, 0), QColor(0, 102, 204), QColor(128, 0, 128),
                                     QColor(255, 255, 0), QColor(64, 224, 255), QColor(0x30, 0x30, 0x38)}) {
            const QColor fitted = readable(wanted, paper);
            CHECK(contrastRatio(fitted, paper) >= kTextContrast);
            CHECK(contrastRatio(readable(wanted, paper, kGraphicContrast), paper) >= kGraphicContrast);
            // Still that colour, not a grey, where there was room for it.
            if (paper == QColor(Qt::white) && wanted != QColor(255, 255, 0) && wanted != QColor(0x30, 0x30, 0x38)
                && wanted != QColor(64, 224, 255))
                CHECK(fitted.hslHue() == wanted.hslHue() || qAbs(fitted.hslHue() - wanted.hslHue()) <= 2);
        }
    }
    {
        const QColor both = readable(QColor(0, 102, 204), QColor("#FFFFFF"), QColor("#F2F2F2"));
        CHECK(contrastRatio(both, QColor("#FFFFFF")) >= kTextContrast && contrastRatio(both, QColor("#F2F2F2")) >= kTextContrast);
    }
    // On a middle grey nothing reaches 7 to 1: the best there is, black.
    CHECK(readable(QColor(200, 0, 0), QColor("#808080")) == QColor(Qt::black));
    // A background for a text that is given: the red of the debugger's
    // current line under its white letters.
    CHECK(contrastRatio(readable(QColor(255, 0, 0), Qt::white), Qt::white) >= kTextContrast);

    // The two looks: every text on what it is written on.
    for (const Theme theme : {Theme::Light, Theme::Dark}) {
        const QPalette look = themePalette(theme);
        const struct {
            QPalette::ColorRole text, back;
        } pairs[] = {{QPalette::WindowText, QPalette::Window},        {QPalette::Text, QPalette::Base},
                     {QPalette::Text, QPalette::AlternateBase},       {QPalette::ButtonText, QPalette::Button},
                     {QPalette::HighlightedText, QPalette::Highlight}, {QPalette::ToolTipText, QPalette::ToolTipBase},
                     {QPalette::Link, QPalette::Base},                {QPalette::LinkVisited, QPalette::Base},
                     {QPalette::Link, QPalette::Window},              {QPalette::PlaceholderText, QPalette::Base},
                     {QPalette::WindowText, QPalette::Button},        {QPalette::Text, QPalette::Window}};
        for (const auto& pair : pairs) {
            const double ratio = contrastRatio(look.color(QPalette::Active, pair.text), look.color(QPalette::Active, pair.back));
            if (ratio < kTextContrast)
                std::printf("theme %d: role %d on %d is %.2f to 1\n", int(theme), int(pair.text), int(pair.back), ratio);
            CHECK(ratio >= kTextContrast);
        }
        // What is selected stands out from the page it is on.
        CHECK(contrastRatio(look.color(QPalette::Highlight), look.color(QPalette::Base)) >= kGraphicContrast);
    }
    CHECK(themePalette(Theme::Light).color(QPalette::Window).lightness() > 200);
    CHECK(themePalette(Theme::Dark).color(QPalette::Window).lightness() < 60);

    // Applied, a look is the whole application's, in the same style on
    // every desktop; the pictograms are drawn again for it.
    applyTheme(Theme::Light);
    CHECK(currentTheme() == Theme::Light && QApplication::style()->objectName().toLower() == "fusion");
    CHECK(QApplication::palette().color(QPalette::Window) == themePalette(Theme::Light).color(QPalette::Window));
    const QImage lightIcon = makeIcon(IconId::Registers).pixmap(20, 20).toImage();
    applyTheme(Theme::Dark);
    CHECK(currentTheme() == Theme::Dark);
    CHECK(QApplication::palette().color(QPalette::Base) == themePalette(Theme::Dark).color(QPalette::Base));
    const QImage darkIcon = makeIcon(IconId::Registers).pixmap(20, 20).toImage();
    CHECK(lightIcon != darkIcon);
    // A button that stays down has another picture for it.
    {
        const QIcon toggle = makeToggleIcon(IconId::Disc);
        CHECK(toggle.pixmap(QSize(20, 20), QIcon::Normal, QIcon::On).toImage()
              != toggle.pixmap(QSize(20, 20), QIcon::Normal, QIcon::Off).toImage());
    }
    applyTheme(Theme::Light);

    // The switch: in the Settings menu and on the control panel, at once
    // and kept for the next time.
    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    {
        MainWindow window(&emulator);
        window.show();
        QAction* dark = nullptr;
        for (QAction* action : window.findChildren<QAction*>())
            if (action->text().remove('&') == "Dark Theme")
                dark = action;
        auto* button = window.findChild<QToolButton*>("bTheme");
        CHECK(dark && dark->isCheckable() && !dark->isChecked());
        CHECK(button && button->isCheckable() && !button->isChecked() && button->toolTip() == "Dark Theme");
        if (!dark || !button)
            return checkSummary("gui_theme");
        CHECK(!window.settings().darkTheme);
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            window.grab().save(prefix + "theme_light.png");
        }
        button->click();
        CHECK(dark->isChecked() && button->isChecked());
        CHECK(currentTheme() == Theme::Dark && window.settings().darkTheme);
        CHECK(QApplication::palette().color(QPalette::Window) == themePalette(Theme::Dark).color(QPalette::Window));
        {
            Settings kept;
            kept.load();
            CHECK(kept.darkTheme);
            QFile file(Settings::file());
            CHECK(file.open(QIODevice::ReadOnly) && file.readAll().contains("\r\nDark Theme=true\r\n"));
        }
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            window.grab().save(prefix + "theme_dark.png");
            // The windows that choose colours of their own, in the dark.
            const QString games = folder.filePath("games");
            QDir().mkpath(games);
            for (const char* name : {"Alpha (UK) (1987) [Original].dsk", "Beta (UK) (1990) [CPC+] [DEMO].dsk",
                                     "Delta (UK) (1988) [Original] [TAPE].cdt", "Epsilon (UK) (1990) [Original].cpr"}) {
                QFile file(games + '/' + name);
                CHECK(file.open(QIODevice::WriteOnly));
            }
            LibraryDialog library({games});
            library.setFilters(LibraryDialog::Discs | LibraryDialog::ForCpc);
            library.show();
            QTest::qWait(50);
            library.grab().save(prefix + "library_dark.png");
            DebuggerDialog debugger(&emulator);
            debugger.show();
            QTest::qWait(50);
            debugger.grab().save(prefix + "debugger_dark.png");
        }
        // And back, from the menu.
        dark->trigger();
        CHECK(!dark->isChecked() && !button->isChecked());
        CHECK(currentTheme() == Theme::Light && !window.settings().darkTheme);
        Settings kept;
        kept.load();
        CHECK(!kept.darkTheme);
    }
    return checkSummary("gui_theme");
}
