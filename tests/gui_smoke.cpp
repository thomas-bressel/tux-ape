// Drives the real application window: boots the machine, types on the
// emulated keyboard through Qt key events, and checks the CPC's screen.
// Runs without a display (QT_QPA_PLATFORM=offscreen).

#include <QApplication>
#include <QTest>

#include "check.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "mainwindow.h"
#include "screenwidget.h"

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
    return checkSummary("gui_smoke");
}
