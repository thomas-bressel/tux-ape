// Snapshots through the application: saving the machine to a file, loading
// it back after the machine has moved on, and the menu entry that goes with
// them. Runs without a display (QT_QPA_PLATFORM=offscreen).

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "check.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "mainwindow.h"

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

    emulator.stop();
    return checkSummary("gui_snapshot");
}
