#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTimer>

#include "audiooutput.h"
#include "core/version.h"
#include "emulator.h"
#include "mainwindow.h"

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
    const QString error = emulator.setupMachine(tuxape::CpcModel::Cpc6128);
    if (!error.isEmpty()) {
        QMessageBox::critical(nullptr, QApplication::applicationName(),
                              QObject::tr("%1\n\nSet the TUXAPE_ROM_DIR environment variable to the folder "
                                          "holding WinAPE's ROM images.")
                                  .arg(error));
        return 1;
    }

    // Without a sound device the emulator runs silent.
    if (audio.open(44100))
        emulator.setAudioOutput(&audio);

    MainWindow window(&emulator);
    window.show();
    if (!parser.positionalArguments().isEmpty())
        window.insertDiscFile(0, parser.positionalArguments().first());
    emulator.start();

    if (parser.isSet(grabOption)) {
        QTimer::singleShot(parser.value(grabDelayOption).toInt(), &window, [&] {
            window.grab().save(parser.value(grabOption));
            window.close();
        });
    }

    return app.exec();
}
