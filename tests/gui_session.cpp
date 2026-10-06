// Sessions in the application: what is typed is recorded to a file and
// played back, with the keyboard out of the user's hands meanwhile. Runs
// without a display (QT_QPA_PLATFORM=offscreen).
//
// Needs the ROM images; exits with code 77 (skipped) without them.

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "check.h"
#include "core/cpc.h"
#include "core/screen_text.h"
#include "core/session.h"
#include "core/files.h"
#include "emulator.h"
#include "mainwindow.h"
#include "screenwidget.h"
#include "settings.h"

namespace {

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text))
            return action;
    return nullptr;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));

    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; skipping\n");
        return 77;
    }
    MainWindow window(&emulator);
    window.show();
    emulator.setSpeedPercent(1000);
    emulator.start();
    auto screenText = [&] { return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); }); };
    auto shows = [&](const char* text) {
        return QTest::qWaitFor([&] { return screenText().find(text) != std::string::npos; }, 20000);
    };
    CHECK(shows("Ready"));
    QTest::qWait(100);

    QAction* record = actionNamed(window, "Record Session...");
    QAction* play = actionNamed(window, "Playback Session...");
    CHECK(record && record->isEnabled() && record->isCheckable() && !record->isChecked());
    CHECK(play && play->isEnabled() && play->isCheckable() && !play->isChecked());
    if (!record || !play)
        return checkSummary("gui_session");

    // A session recorded from where the machine stands.
    const QString path = folder.filePath("typed.snr");
    window.startSessionRecording(path, false);
    CHECK(emulator.recordingSession() && record->isChecked());
    CHECK(shows("Ready"));  // the machine went on from its own snapshot
    emulator.autoType(QStringLiteral("~PAUSE 10~PRINT 6*7~RETURN~"));
    CHECK(shows(" 42"));
    QTest::qWait(100);
    CHECK(window.stopSessionRecording());
    CHECK(!emulator.recordingSession() && !record->isChecked());
    const std::string recorded = screenText();
    const auto data = tuxape::readFile(path.toStdString());
    const auto session = data ? tuxape::Session::parse(*data) : std::nullopt;
    CHECK(session.has_value());
    CHECK(session && session->frames > 20 && session->events.size() >= 16);

    // Something else is done; then the session is played back: the screen
    // ends as it did, and the keys pressed meanwhile count for nothing.
    emulator.autoType(QStringLiteral("CLS~RETURN~PRINT \"other\"~RETURN~"));
    CHECK(shows("other"));
    CHECK(window.playSessionFile(path));
    CHECK(emulator.playingSession() && play->isChecked() && !emulator.recordingSession());
    QTest::keyClick(window.screen(), Qt::Key_X);
    CHECK(QTest::qWaitFor([&] { return !emulator.playingSession(); }, 20000));
    // The window hears of it from the machine's thread, a moment later.
    CHECK(QTest::qWaitFor([&] { return !play->isChecked(); }, 2000));
    CHECK(screenText() == recorded);
    CHECK(screenText().find("other") == std::string::npos);
    // The keyboard is the user's again.
    emulator.autoType(QStringLiteral("PRINT 7*7~RETURN~"));
    CHECK(shows(" 49"));

    // A recording from a cold reset starts with the machine's banner.
    const QString coldPath = folder.filePath("cold.snr");
    window.startSessionRecording(coldPath, true);
    CHECK(QTest::qWaitFor([&] { return screenText().find(" 49") == std::string::npos; }, 5000));
    CHECK(shows("Ready"));
    QTest::qWait(100);
    emulator.autoType(QStringLiteral("PRINT 8*8~RETURN~"));
    CHECK(shows(" 64"));
    CHECK(window.stopSessionRecording());
    emulator.autoType(QStringLiteral("CLS~RETURN~"));
    QTest::qWait(100);
    CHECK(window.playSessionFile(coldPath));
    // Stopped half-way by the user: the entry chosen again.
    CHECK(shows("Amstrad 128K Microcomputer"));
    emulator.stopPlayback();
    CHECK(!emulator.playingSession());

    // A file that is no session: a word to the user, and nothing played.
    const QString notOne = folder.filePath("plain.snr");
    {
        QFile file(notOne);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write("not a session");
    }
    QTimer::singleShot(0, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->close();
    });
    CHECK(!window.playSessionFile(notOne));
    CHECK(!emulator.playingSession() && !play->isChecked());

    emulator.stop();
    return checkSummary("gui_session");
}
