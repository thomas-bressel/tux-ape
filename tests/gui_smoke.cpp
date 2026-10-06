// Drives the real application window: boots the machine, types on the
// emulated keyboard through Qt key events, and checks the CPC's screen.
// Runs without a display (QT_QPA_PLATFORM=offscreen).

#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QPointer>
#include <QSplashScreen>
#include <QTemporaryDir>
#include <QTest>

#include "check.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "mainwindow.h"
#include "screenwidget.h"
#include "welcome.h"
#include "setupdialog.h"
#include "settings.h"

namespace {

std::string screenText(Emulator& emulator)
{
    return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); });
}

// Waits until the CPC screen shows `what`, for at most `ms` of real time.
bool waitForText(Emulator& emulator, const char* what, int ms)
{
    return QTest::qWaitFor([&] { return screenText(emulator).find(what) != std::string::npos; }, ms);
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; skipping\n");
        return 77;
    }
    MainWindow window(&emulator);
    window.show();
    emulator.setSpeedPercent(1000);
    emulator.start();

    CHECK(waitForText(emulator, "Ready", 10000));
    CHECK_EQ(window.screen()->width(), 768);
    CHECK_EQ(window.screen()->height(), 540);

    // Synthetic key events carry no scan code, which exercises the fallback
    // that maps by key meaning.
    ScreenWidget* screen = window.screen();
    const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyPress(screen, key, modifiers);
        QTest::qWait(30);  // several emulated frames at this speed
        QTest::keyRelease(screen, key, modifiers);
        QTest::qWait(30);
    };
    for (Qt::Key key : {Qt::Key_P, Qt::Key_R, Qt::Key_I, Qt::Key_N, Qt::Key_T, Qt::Key_Space, Qt::Key_6})
        press(key);
    QTest::keyPress(screen, Qt::Key_Shift);
    press(Qt::Key_Colon, Qt::ShiftModifier);  // SHIFT + ':' is '*' on the CPC
    QTest::keyRelease(screen, Qt::Key_Shift);
    press(Qt::Key_7);
    press(Qt::Key_Return);
    CHECK(waitForText(emulator, " 42", 5000));

    // Pausing stops the machine; the same picture stays up.
    QTest::keyClick(&window, Qt::Key_F7);
    CHECK(emulator.isPaused());
    const uint64_t before = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.clock(); });
    QTest::qWait(100);
    CHECK_EQ(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.clock(); }), before);
    QTest::keyClick(&window, Qt::Key_F9);
    CHECK(!emulator.isPaused());
    // Pausing brought the debugger's window up; running hid it again, and
    // the keyboard is back with the main window.
    CHECK(QTest::qWaitFor([&] { return window.isActiveWindow(); }, 2000));

    // Reset brings the banner back.
    QTest::keyClick(&window, Qt::Key_F9, Qt::ControlModifier);
    QTest::qWait(200);
    CHECK(waitForText(emulator, "Ready", 5000));
    CHECK(screenText(emulator).find(" 42") == std::string::npos);

    // Paste types the clipboard.
    emulator.autoType("?\"pasted\"\n");
    CHECK(waitForText(emulator, "\npasted", 5000));

    if (g_failures)
        std::printf("screen was:\n%s\n", screenText(emulator).c_str());
    emulator.stop();
    // The welcome picture: in the program itself, shown over the screen
    // for a moment as TuxAPE starts, and gone at a click; a setting, with
    // its box on the General page, says whether it is wanted.
    {
        const QPixmap picture = welcomePicture();
        CHECK(!picture.isNull() && picture.width() > 200 && picture.height() > picture.width());
        // It tells when it has gone, once: the program's window is to
        // come after it. A click sends it away before its time.
        int gone = 0;
        QPointer<QSplashScreen> splash = showWelcome([&] { ++gone; }, 60000);
        CHECK(splash && splash->isVisible() && splash->objectName() == "Welcome");
        if (splash) {
            CHECK(!splash->pixmap().isNull() && splash->pixmap().height() <= picture.height());
            QTest::qWait(100);
            CHECK_EQ(gone, 0);
            QTest::mouseClick(splash, Qt::LeftButton);
            CHECK(QTest::qWaitFor([&] { return !splash || !splash->isVisible(); }, 2000));
            CHECK_EQ(gone, 1);
            delete splash.data();
            CHECK_EQ(gone, 1);
        }
        // Left alone it stays its time, three seconds unless said
        // otherwise, then goes by itself.
        gone = 0;
        QElapsedTimer shown;
        shown.start();
        QPointer<QSplashScreen> brief = showWelcome([&] { ++gone; }, 400);
        CHECK(brief && brief->isVisible());
        CHECK(QTest::qWaitFor([&] { return gone == 1; }, 3000));
        CHECK(shown.elapsed() >= 380);
        CHECK(QTest::qWaitFor([&] { return brief.isNull(); }, 2000));
        CHECK_EQ(gone, 1);

        Settings settings;
        CHECK(settings.welcomePicture);
        SetupDialog dialog(settings);
        auto* box = dialog.findChild<QCheckBox*>("ckWelcome");
        CHECK(box && box->isChecked() && box->isEnabled());
        if (box)
            box->setChecked(false);
        CHECK(!dialog.settings().welcomePicture);
        QTemporaryDir folder;
        Settings::setFile(folder.filePath("TuxAPE.ini"));
        settings.welcomePicture = false;
        CHECK(settings.save());
        Settings read;
        read.load();
        CHECK(!read.welcomePicture);
    }
    return checkSummary("gui_smoke");
}
