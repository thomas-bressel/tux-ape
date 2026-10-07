#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTimer>

#include "audiooutput.h"
#include "core/version.h"
#include "emulator.h"
#include "mainwindow.h"
#include "theme.h"
#include "welcome.h"

int main(int argc, char* argv[])
{
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
    parser.addOptions({grabOption, grabDelayOption});
    parser.addPositionalArgument("disc", QObject::tr("Disc image to put in drive A:."), "[disc]");
    parser.process(app);

    // Declared before the emulator, which uses it until it is destroyed.
    AudioOutput audio;
    Emulator emulator;
    Settings settings;
    settings.load();
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
    const auto open = [&] {
        window.show();
        if (!parser.positionalArguments().isEmpty())
            window.insertDiscFile(0, parser.positionalArguments().first());
        emulator.start();
    };
    // The welcome picture stays three seconds on the screen, by itself;
    // the window and the machine come after it. Not if the user would
    // rather not see it, or if a picture of the window is what was asked
    // for.
    if (settings.welcomePicture && !parser.isSet(grabOption))
        showWelcome(open);
    else
        open();

    if (parser.isSet(grabOption)) {
        QTimer::singleShot(parser.value(grabDelayOption).toInt(), &window, [&] {
            window.grab().save(parser.value(grabOption));
            window.close();
        });
    }

    return app.exec();
}
