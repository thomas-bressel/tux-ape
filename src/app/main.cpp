#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>

#include "audiooutput.h"
#include "core/version.h"
#include "emulator.h"
#include "language.h"
#include "mainwindow.h"
#include "theme.h"
#include "welcome.h"

int main(int argc, char* argv[])
{
    // Under Wayland the system places the windows; through XWayland TuxAPE
    // puts them back where they were, if the user asked for it (Settings,
    // General) and nothing else was asked for.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && Settings::windowsThroughX11Possible()
        && Settings::windowsThroughX11Wanted())
        qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    QApplication::setApplicationName("TuxAPE");
    QApplication::setApplicationVersion(tuxape::versionString());
    QApplication::setOrganizationName("TuxAPE");

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Amstrad CPC / Plus emulator"));
    parser.addHelpOption();
    parser.addVersionOption();
    // For automated checks of the user interface: save a picture of the
    // window after a delay, then quit.
    const QCommandLineOption grabOption("grab", QObject::tr("Save a picture of the window to <file> and quit."),
                                        "file");
    const QCommandLineOption grabDelayOption("grab-after", QObject::tr("Milliseconds to wait before --grab."),
                                             "ms", "3000");
    // What a TuxAPE gives the one it starts again (Settings > Language):
    // the machine's state, the discs in the drives, the tape.
    QCommandLineOption resumeOption("resume", QObject::tr("A state to take the machine back to, then delete."), "file");
    QCommandLineOption insertOption("insert", QObject::tr("A disc to put in a drive, as A:file."), "drive:file");
    QCommandLineOption tapeOption("tape", QObject::tr("A tape to put in the deck."), "file");
    for (QCommandLineOption* option : {&resumeOption, &insertOption, &tapeOption})
        option->setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOptions({grabOption, grabDelayOption, resumeOption, insertOption, tapeOption});
    parser.addPositionalArgument("disc", QObject::tr("Disc image to put in drive A:."), "[disc]");
    parser.process(app);

    // A folder made by tools/make-dist.sh has the ROMs and the profiles
    // beside the program, as WinAPE's has: they are taken from there, unless
    // the variables say otherwise.
    const QDir beside(QCoreApplication::applicationDirPath());
    for (const auto& [variable, folder] : {std::pair{"TUXAPE_ROM_DIR", "ROM"},
                                           std::pair{"TUXAPE_USER_ROM_DIR", "roms"},
                                           std::pair{"TUXAPE_PROFILE_DIR", "Profile"}})
        if (qEnvironmentVariableIsEmpty(variable) && beside.exists(folder))
            qputenv(variable, QFile::encodeName(beside.filePath(folder)));

    // Filled when TuxAPE is to start again, once everything below is
    // closed and written.
    QStringList restart;
    int result = 0;
    {
        // Declared before the emulator, which uses it until it is destroyed.
        AudioOutput audio;
        Emulator emulator;
        Settings settings;
        settings.load();
        // The language of the windows, before any is made.
        if (!useLanguage(settings.language))
            settings.language = kSourceLanguage;
        // The look of the windows, before any is made.
        applyTheme(settings.darkTheme ? Theme::Dark : Theme::Light);
        const QString error = emulator.setupMachine(settings.machine, true);
        if (!error.isEmpty()) {
            const QString message = error.left(1).toUpper() + error.mid(1) + ".";
            if (settings.machine == tuxape::stockMachine(tuxape::CpcModel::Cpc6128)) {
                QMessageBox::critical(nullptr, QApplication::applicationName(),
                                      QObject::tr("%1\n\nSet the TUXAPE_ROM_DIR environment variable to the folder "
                                                  "holding WinAPE's ROM images.")
                                          .arg(message));
                return 1;
            }
            // The machine may not start, but the Setup window is there to put
            // that right.
            QMessageBox::warning(nullptr, QApplication::applicationName(),
                                 QObject::tr("%1\n\nChoose other ROM images in Settings > Memory.").arg(message));
        }

        // Without a sound device the emulator runs silent.
        if (audio.open(44100))
            emulator.setAudioOutput(&audio);

        // The keyboard layout as the user left it, if it was ever changed.
        tuxape::KeyMap keys;
        if (Settings::loadKeyMap(keys))
            emulator.setKeyMap(keys);

        MainWindow window(&emulator);
        window.applySettings(settings);
        // The plugins of the plugins folder, but for those the user turned off.
        window.loadPlugins();
        const auto open = [&] {
            // Where the windows were when TuxAPE last left, and those that were
            // open open again.
            window.placeMainWindow();
            window.show();
            window.reopenWindows();
            if (!parser.positionalArguments().isEmpty())
                window.insertDiscFile(0, parser.positionalArguments().first());
            for (const QString& disc : parser.values(insertOption)) {
                const int drive = disc.isEmpty() ? -1 : disc.at(0).toUpper().unicode() - 'A';
                if (drive >= 0 && drive < 4 && disc.mid(1, 1) == ":")
                    window.insertDiscFile(drive, disc.mid(2));
            }
            if (parser.isSet(tapeOption))
                window.insertTapeFile(parser.value(tapeOption));
            if (parser.isSet(resumeOption))
                window.resumeFrom(parser.value(resumeOption));
            emulator.start();
        };
        // The welcome picture stays three seconds on the screen, by itself;
        // the window and the machine come after it. Not if the user would
        // rather not see it, if a picture of the window is what was asked
        // for, or if TuxAPE is only starting again.
        if (settings.welcomePicture && !parser.isSet(grabOption) && !parser.isSet(resumeOption))
            showWelcome(open);
        else
            open();

        if (parser.isSet(grabOption)) {
            QTimer::singleShot(parser.value(grabDelayOption).toInt(), &window, [&] {
                window.grab().save(parser.value(grabOption));
                window.close();
            });
        }

        result = app.exec();
        restart = window.restartArguments();
    }
    // Everything is closed and written: the TuxAPE that comes next can
    // start, in the language now chosen.
    if (!restart.isEmpty())
        QProcess::startDetached(QCoreApplication::applicationFilePath(), restart);
    return result;
}
