// The keys pressed shown on the picture, for a stream: off unless asked
// for, the keys in the order they went down with what the machine's own
// keyboard has printed on them, eight at most, gone 0.8 s after the last
// one held is let go; the joystick's too, Auto Type's not; and the Display
// page's box that sets it.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>

#include "check.h"
#include "core/keymap.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "hostjoystick.h"
#include "mainwindow.h"
#include "screenwidget.h"
#include "settings.h"
#include "setupdialog.h"

namespace {

bool waitForText(Emulator& emulator, const char* what, int ms = 15000)
{
    return QTest::qWaitFor(
        [&] {
            return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); }).find(what)
                   != std::string::npos;
        },
        ms);
}

void press(Emulator& emulator, uint8_t pcKey, bool down)
{
    emulator.pcKeyEvent(pcKey, true, down);
}

void grab(QWidget& widget, const char* name)
{
    const QByteArray dir = qgetenv("TUXAPE_TEST_GRAB_DIR");
    if (!dir.isEmpty())
        widget.grab().save(QString::fromLocal8Bit(dir) + QLatin1Char('/') + QLatin1String(name));
}

// The darkest pixel of a part of the picture.
int darkest(const QImage& image, const QRect& part)
{
    int least = 255;
    for (int y = part.top(); y <= part.bottom(); ++y)
        for (int x = part.left(); x <= part.right(); ++x)
            least = std::min(least, qGray(image.pixel(x, y)));
    return least;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // The settings are this test's own, never the user's.
    QTemporaryDir settingsFolder;
    Settings::setFile(settingsFolder.filePath("TuxAPE.ini"));

    // Off unless asked for; the box that asks greys what goes with it.
    {
        Settings settings;
        CHECK(!settings.keysOnScreen);
        SetupDialog dialog(settings);
        auto* shown = dialog.findChild<QCheckBox*>("ckKeysOnScreen");
        auto* corner = dialog.findChild<QComboBox*>("cbKeysCorner");
        auto* large = dialog.findChild<QRadioButton*>("rbKeysLarge");
        CHECK(shown && corner && large);
        if (shown && corner && large) {
            CHECK(!shown->isChecked() && !corner->isEnabled() && !large->isEnabled());
            CHECK_EQ(corner->count(), 4);
            CHECK_EQ(corner->currentIndex(), 3);
            shown->setChecked(true);
            CHECK(corner->isEnabled() && large->isEnabled());
            corner->setCurrentIndex(0);
            large->setChecked(true);
            const Settings chosen = dialog.settings();
            CHECK(chosen.keysOnScreen && chosen.keysCorner == 0 && chosen.keysLarge);
            CHECK(chosen.save());
            Settings again;
            again.load();
            CHECK(again.keysOnScreen && again.keysCorner == 0 && again.keysLarge);
        }
    }

    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; skipping\n");
        return 77;
    }
    MainWindow window(&emulator);
    window.show();
    ScreenWidget* screen = window.screen();
    emulator.start();
    CHECK(waitForText(emulator, "Ready"));
    Settings settings = window.settings();
    settings.keysOnScreen = false;
    window.applySettings(settings);
    CHECK(!screen->keysShown());

    // Not asked for: nothing.
    press(emulator, tuxape::PcLeftControl, true);
    QTest::qWait(150);
    CHECK(screen->keysOnScreen().isEmpty());
    press(emulator, tuxape::PcLeftControl, false);

    settings.keysOnScreen = true;
    settings.keysCorner = 3;
    settings.keysLarge = false;
    window.applySettings(settings);
    CHECK(screen->keysShown());
    QTest::qWait(100);
    // What was pressed before it was asked for is not shown.
    CHECK(screen->keysOnScreen().isEmpty());
    // The machine stopped, for the picture under the keys to stay as it is.
    emulator.setPaused(true);
    QTest::qWait(100);
    QImage before = screen->grab().toImage();

    // A combination: the keys in the order they went down, as the English
    // keyboard has them.
    press(emulator, tuxape::PcLeftControl, true);
    QTest::qWait(80);
    press(emulator, tuxape::PcEscape, true);
    CHECK(QTest::qWaitFor([&] { return screen->keysOnScreen().size() == 2; }, 1000));
    CHECK(screen->keysOnScreen() == QStringList({"CONTROL", "ESC"}));
    // Drawn in the bottom right corner: dark caps over the picture there.
    const QImage shown = screen->grab().toImage();
    grab(*screen, "keys_small.png");
    const QRect corner(screen->width() * 3 / 4, screen->height() * 7 / 8, screen->width() / 4 - 1, screen->height() / 8 - 1);
    CHECK(shown.copy(corner) != before.copy(corner));
    CHECK(shown.copy(QRect(0, 0, screen->width() / 2, screen->height() / 2))
          == before.copy(QRect(0, 0, screen->width() / 2, screen->height() / 2)));
    CHECK(darkest(shown, corner) <= darkest(before, corner));
    // Held keys stay; let go, they stay 0.8 s, then go.
    QTest::qWait(1000);
    CHECK_EQ(screen->keysOnScreen().size(), 2);
    press(emulator, tuxape::PcEscape, false);
    press(emulator, tuxape::PcLeftControl, false);
    QElapsedTimer letGo;
    letGo.start();
    QTest::qWait(500);
    CHECK_EQ(screen->keysOnScreen().size(), 2);
    CHECK(QTest::qWaitFor([&] { return screen->keysOnScreen().isEmpty(); }, 2000));
    CHECK(letGo.elapsed() >= ScreenWidget::kKeysStay);
    CHECK(letGo.elapsed() < ScreenWidget::kKeysStay + 400);

    // Letters, figures and signs; a key that came and went between two
    // looks is shown too; a key pressed again is not shown twice; nine
    // keys: the first goes.
    for (const uint8_t key : {tuxape::PcQ, tuxape::Pc2, tuxape::PcSemicolon, tuxape::PcQ}) {
        press(emulator, key, true);
        press(emulator, key, false);
        QTest::qWait(60);
    }
    QTest::qWait(60);
    CHECK(screen->keysOnScreen() == QStringList({"Q", "\"\n2", "*\n:"}));
    for (const uint8_t key : {tuxape::PcW, tuxape::PcE, tuxape::PcR, tuxape::PcT, tuxape::PcY, tuxape::PcU}) {
        press(emulator, key, true);
        QTest::qWait(60);
        press(emulator, key, false);
    }
    QTest::qWait(60);
    CHECK(screen->keysOnScreen() == QStringList({"\"\n2", "*\n:", "W", "E", "R", "T", "Y", "U"}));
    grab(*screen, "keys_eight.png");
    CHECK_EQ(ScreenWidget::kMostKeys, 8);
    CHECK(QTest::qWaitFor([&] { return screen->keysOnScreen().isEmpty(); }, 2000));

    // The joystick's buttons are keys of the CPC's like the others.
    emulator.withMachine([&](tuxape::Cpc&) { emulator.applyJoystick(HostJoystick::Up | HostJoystick::Fire1); });
    CHECK(QTest::qWaitFor([&] { return screen->keysOnScreen().size() == 2; }, 1000));
    CHECK(screen->keysOnScreen() == QStringList({"JOY ↑", "FIRE 1"}));
    emulator.withMachine([&](tuxape::Cpc&) { emulator.applyJoystick(0); });
    CHECK(QTest::qWaitFor([&] { return screen->keysOnScreen().isEmpty(); }, 2000));

    // Auto Type's keys are not the user's.
    emulator.setPaused(false);
    emulator.autoType("PRINT 1\n");
    CHECK(QTest::qWaitFor([&] { return !emulator.autoTyping(); }, 5000));
    QTest::qWait(150);
    CHECK(screen->keysOnScreen().isEmpty());
    emulator.setPaused(true);
    QTest::qWait(100);
    before = screen->grab().toImage();

    // The other corners, and the larger size: caps in the top left one.
    settings.keysCorner = 0;
    settings.keysLarge = true;
    window.applySettings(settings);
    press(emulator, tuxape::PcSpace, true);
    CHECK(QTest::qWaitFor([&] { return screen->keysOnScreen() == QStringList({"SPACE"}); }, 1000));
    QTest::qWait(60);
    const QImage top = screen->grab().toImage();
    grab(*screen, "keys_large.png");
    const QRect topLeft(0, 0, screen->width() / 4, screen->height() / 8);
    CHECK(top.copy(topLeft) != before.copy(topLeft));
    CHECK(top.copy(corner) == before.copy(corner));
    press(emulator, tuxape::PcSpace, false);

    // Asked no more: gone at once.
    settings.keysOnScreen = false;
    window.applySettings(settings);
    CHECK(screen->keysOnScreen().isEmpty());

    emulator.stop();
    return checkSummary("gui_keys");
}
