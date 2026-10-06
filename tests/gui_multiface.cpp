// The Multiface II in the application: its box and its ROM on the Memory
// page, the settings that keep them, and "Multiface Stop" (F11), its red
// button. Runs without a display (QT_QPA_PLATFORM=offscreen), with a ROM of
// the test's own.

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
#include "emulator.h"
#include "mainwindow.h"
#include "settings.h"
#include "setupdialog.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    // The user's own ROMs are no part of this.
    qputenv("TUXAPE_USER_ROM_DIR", folder.filePath("no roms").toLocal8Bit());

    // A ROM of 8K that stays in a loop at its entry.
    const QString rom = folder.filePath("stopper.rom");
    {
        QByteArray image(0x2000, '\0');
        image[0x66] = char(0x18);
        image[0x67] = char(0xFE);
        QFile file(rom);
        CHECK(file.open(QIODevice::WriteOnly) && file.write(image) == image.size());
    }

    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();
    QAction* stop = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text() == "M&ultiface Stop")
            stop = action;
    CHECK(stop && stop->shortcut() == QKeySequence(Qt::Key_F11));
    if (!stop)
        return checkSummary("gui_multiface");
    // No Multiface: no button to press.
    CHECK(!emulator.hasMultiface() && !stop->isEnabled());

    // The Memory page: a box, and the ROM's file.
    Settings settings = window.settings();
    CHECK(!settings.machine.multifaceEnabled && settings.machine.multifaceRom.empty());
    {
        SetupDialog dialog(settings);
        auto* box = dialog.findChild<QCheckBox*>("ckEnableMultiface");
        auto* file = dialog.findChild<QLabel*>("edMultiface");
        auto* browse = dialog.findChild<QToolButton*>("sbMultiface");
        CHECK(box && box->isEnabled() && !box->isChecked());
        CHECK(file && file->text().isEmpty());
        CHECK(browse && browse->isEnabled());
        if (box)
            box->setChecked(true);
        CHECK(dialog.settings().machine.multifaceEnabled);
    }
    settings.machine.multifaceEnabled = true;
    settings.machine.multifaceRom = QDir::toNativeSeparators(rom).toStdString();
    {
        SetupDialog dialog(settings);
        auto* box = dialog.findChild<QCheckBox*>("ckEnableMultiface");
        auto* file = dialog.findChild<QLabel*>("edMultiface");
        CHECK(box && box->isChecked());
        CHECK(file && file->text() == "stopper.rom" && file->toolTip() == QDir::toNativeSeparators(rom));
        CHECK(dialog.settings().machine == settings.machine);
    }
    // Kept in the settings, under WinAPE's names.
    CHECK(settings.save());
    {
        Settings read;
        read.load();
        CHECK(read.machine.multifaceEnabled && read.machine.multifaceRom == settings.machine.multifaceRom);
        QFile file(Settings::file());
        CHECK(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        CHECK(text.contains("Multiface Enabled=true") && text.contains("Multiface="));
    }

    // Fitted: the button is there, and stops the machine into the
    // Multiface's ROM.
    window.applySettings(settings);
    CHECK(emulator.hasMultiface() && stop->isEnabled());
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.out(0x7F00, 0x8D);
        cpc.memory().write(0x9000, 0x18);
        cpc.memory().write(0x9001, 0xFE);
        cpc.cpu().pc = 0x9000;
        cpc.cpu().sp = 0xBFF0;
        cpc.cpu().iff1 = cpc.cpu().iff2 = false;
    });
    stop->trigger();
    const bool paged = emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.run(200);
        return cpc.memory().multifacePaged();
    });
    CHECK(paged);
    const int pc = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.cpu().pc; });
    CHECK(pc == 0x0066 || pc == 0x0067);
    // A reset gives the machine back.
    emulator.reset(false);
    CHECK(!emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().multifacePaged(); }));
    CHECK(emulator.hasMultiface());

    // Taken out again: no button. A ROM that is not there is said, and
    // leaves the machine without a Multiface.
    settings.machine.multifaceEnabled = false;
    window.applySettings(settings);
    CHECK(!emulator.hasMultiface() && !stop->isEnabled());
    return checkSummary("gui_multiface");
}
