// Snapshots through the application: saving the machine to a file, loading
// it back after the machine has moved on, and the menu entry that goes with
// them. Runs without a display (QT_QPA_PLATFORM=offscreen).

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "check.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "mainwindow.h"
#include "screenwidget.h"
#include "settings.h"

namespace {

std::string screenText(Emulator& emulator)
{
    return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); });
}

bool waitForText(Emulator& emulator, const char* what, int ms = 15000)
{
    return QTest::qWaitFor([&] { return screenText(emulator).find(what) != std::string::npos; }, ms);
}

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')).startsWith(QLatin1String(text)))
            return action;
    return nullptr;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // The settings are this test's own, never the user's.
    QTemporaryDir settingsFolder;
    Settings::setFile(settingsFolder.filePath("TuxAPE.ini"));

    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; skipping\n");
        return 77;
    }
    MainWindow window(&emulator);
    window.show();
    emulator.setSpeedPercent(1000);
    emulator.start();
    CHECK(waitForText(emulator, "Ready"));

    QAction* load = actionNamed(window, "Load Snapshot");
    QAction* save = actionNamed(window, "Save Snapshot");
    QAction* update = actionNamed(window, "Update Snapshot");
    CHECK(load && load->isEnabled());
    CHECK(save && save->isEnabled());
    // Nothing to update until a snapshot has been loaded or saved.
    CHECK(update && !update->isEnabled());

    // What is waited for must come from the program, not from the echo of
    // the keys: a snapshot taken half-way through a line would hold that
    // half line.
    QTest::qWait(500);
    emulator.autoType(QStringLiteral("a=1234:CLS:PRINT a+1\n"));
    CHECK(waitForText(emulator, "1235"));
    CHECK(waitForText(emulator, "Ready"));

    QTemporaryDir folder;
    const QString path = folder.filePath("state.sna");
    CHECK(window.saveSnapshotFile(path));
    CHECK_EQ(QFileInfo(path).size(), 0x100 + 128 * 1024);
    CHECK(update && update->isEnabled());

    // The machine moves on...
    emulator.autoType(QStringLiteral("a=5:CLS:PRINT \"CHANGED\";a\n"));
    CHECK(waitForText(emulator, "CHANGED 5"));
    CHECK(waitForText(emulator, "Ready"));

    // ...and comes back to where it was.
    CHECK(window.loadSnapshotFile(path));
    QTest::qWait(300);
    CHECK(screenText(emulator).find("CHANGED") == std::string::npos);
    CHECK(screenText(emulator).find("1235") != std::string::npos);
    emulator.autoType(QStringLiteral("PRINT a*2\n"));
    CHECK(waitForText(emulator, "2468"));
    if (g_failures)
        std::printf("screen after loading:\n%s\n", screenText(emulator).c_str());

    // Update Snapshot writes to the same file.
    const QDateTime before = QFileInfo(path).lastModified();
    QTest::qWait(1100);
    update->trigger();
    CHECK(QFileInfo(path).lastModified() > before);

    // The keys: Ctrl for the snapshots' windows, F5 and F6 for the quick
    // state, Ctrl+Alt+F5 for Auto Type.
    QAction* quickSave = actionNamed(window, "Quick Save State");
    QAction* quickLoad = actionNamed(window, "Quick Load State");
    CHECK(load && load->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F5));
    CHECK(save && save->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F6));
    CHECK(update && update->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F6));
    CHECK(quickSave && quickSave->shortcut() == QKeySequence(Qt::Key_F5));
    CHECK(quickLoad && quickLoad->shortcut() == QKeySequence(Qt::Key_F6));
    QAction* autoType = actionNamed(window, "Auto Type");
    CHECK(autoType && autoType->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F5));

    // The snapshots' windows stop the machine while they are up, and let
    // it go on after.
    {
        bool pausedThen = false;
        QTimer::singleShot(300, [&] {
            pausedThen = emulator.isPaused();
            if (QWidget* modal = QApplication::activeModalWidget())
                modal->close();
        });
        CHECK(!emulator.isPaused());
        load->trigger();
        CHECK(pausedThen && !emulator.isPaused());
        pausedThen = false;
        QTimer::singleShot(300, [&] {
            pausedThen = emulator.isPaused();
            if (QWidget* modal = QApplication::activeModalWidget())
                modal->close();
        });
        save->trigger();
        CHECK(pausedThen && !emulator.isPaused());
    }

    // F5 keeps the state, in the savestates folder, under the program's
    // name (a snapshot was loaded last: its name); F6 takes it back there.
    {
        const QString state = window.quickStatePath();
        CHECK(state == Settings::saveStatesFolder() + "/" + QFileInfo(path).completeBaseName() + ".sna");
        CHECK(Settings::saveStatesFolder() == QFileInfo(Settings::file()).absolutePath() + "/savestates");
        QFile::remove(state);
        CHECK(!window.quickLoadState() && window.screen()->note().contains("F5 saves one"));
        emulator.autoType(QStringLiteral("CLS:a=77:PRINT \"KEPT\";a\n"));
        CHECK(waitForText(emulator, "KEPT 77"));
        CHECK(waitForText(emulator, "Ready"));
        QTest::qWait(300);
        quickSave->trigger();
        CHECK(QFile::exists(state) && window.screen()->note().startsWith("State saved:"));
        emulator.autoType(QStringLiteral("CLS:a=3:PRINT \"LATER\";a\n"));
        CHECK(waitForText(emulator, "LATER 3"));
        quickLoad->trigger();
        CHECK(window.screen()->note().startsWith("State loaded:"));
        QTest::qWait(300);
        CHECK(screenText(emulator).find("LATER") == std::string::npos);
        emulator.autoType(QStringLiteral("PRINT a+1\n"));
        CHECK(waitForText(emulator, "78"));
        // The note goes after a moment.
        CHECK(QTest::qWaitFor([&] { return window.screen()->note().isEmpty(); }, 4000));
    }

    emulator.stop();
    return checkSummary("gui_snapshot");
}
