// The Setup window and the settings behind it: what is kept in TuxAPE.ini,
// what the General page shows and gives back, and what reaches the machine.
// Runs without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_setup [picture]   also saves a picture of the Setup window

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#include "check.h"
#include "core/inifile.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "mainwindow.h"
#include "settings.h"
#include "setupdialog.h"

namespace {

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text))
            return action;
    return nullptr;
}

void testSettingsFile(const QString& path)
{
    // Nothing saved yet: the defaults.
    Settings settings;
    settings.crtcType = 3;
    settings.load();
    CHECK(settings == Settings());
    CHECK_EQ(settings.crtcType, 0);
    CHECK_EQ(settings.speedPercent, 100);
    CHECK(!settings.fastDisc);
    CHECK(!settings.displayEvery);

    // The file is one WinAPE could have written.
    CHECK(settings.save());
    tuxape::IniFile ini;
    CHECK(ini.load(path.toStdString()));
    CHECK(ini.get("Configuration", "CRTC Type") == "0");
    CHECK(ini.get("Configuration", "Emulation Speed") == "100");
    CHECK(ini.get("Configuration", "Frame Skip") == "0");
    CHECK(ini.get("Drives", "Fast Disc") == "false");

    settings.crtcType = 1;
    settings.fastDisc = true;
    settings.speedPercent = 250;
    settings.displayEvery = true;
    settings.displayEveryFrames = 7;
    CHECK(settings.save());
    Settings again;
    again.load();
    CHECK(again == settings);

    // Values out of range are brought back into it, and what this version
    // does not know about survives a save.
    ini.setInt("Configuration", "CRTC Type", 9);
    ini.setInt("Configuration", "Emulation Speed", 1);
    ini.setInt("Configuration", "Frame Skip", 400);
    ini.set("Configuration", "Monitor Type", "2");
    ini.set("ROMS", "Lower", "OS6128");
    CHECK(ini.save(path.toStdString()));
    again.load();
    CHECK_EQ(again.crtcType, 4);
    CHECK_EQ(again.speedPercent, 5);
    CHECK(again.displayEvery);
    CHECK_EQ(again.displayEveryFrames, 50);
    CHECK(again.save());
    CHECK(ini.load(path.toStdString()));
    CHECK(ini.get("Configuration", "Monitor Type") == "2");
    CHECK(ini.get("ROMS", "Lower") == "OS6128");
    CHECK(ini.get("Configuration", "CRTC Type") == "4");

    QFile::remove(path);
}

void testDialog(const char* picture)
{
    Settings settings;
    settings.crtcType = 2;
    settings.fastDisc = true;
    settings.speedPercent = 300;
    SetupDialog dialog(settings);
    dialog.show();

    auto* crtc = dialog.findChild<QComboBox*>("cbCRTCType");
    auto* fastDisc = dialog.findChild<QCheckBox*>("ckFastDisc");
    auto* speed = dialog.findChild<QSlider*>("slSpeed");
    auto* speedLabel = dialog.findChild<QLabel*>("lbSpeed");
    auto* every = dialog.findChild<QCheckBox*>("ckUseRate");
    auto* frames = dialog.findChild<QSpinBox*>("edRate");
    auto* tabs = dialog.findChild<QTabWidget*>("PageControl");
    CHECK(crtc && fastDisc && speed && speedLabel && every && frames && tabs);
    if (!(crtc && fastDisc && speed && speedLabel && every && frames && tabs))
        return;

    // It shows what it was given, under WinAPE's names.
    CHECK_EQ(crtc->count(), 5);
    CHECK(crtc->itemText(0) == "0 - HD6845S/UM6845");
    CHECK(crtc->itemText(3) == "3 - CPC+ ASIC");
    CHECK_EQ(crtc->currentIndex(), 2);
    CHECK(fastDisc->isChecked());
    CHECK_EQ(speed->value(), 300);
    CHECK_EQ(speed->minimum(), 5);
    CHECK_EQ(speed->maximum(), 1000);
    CHECK(speedLabel->text() == "300%");
    CHECK(!every->isChecked());
    CHECK_EQ(frames->value(), 50);
    CHECK(dialog.settings() == settings);

    // Six pages as in WinAPE; only the first is there yet.
    CHECK_EQ(tabs->count(), 6);
    CHECK(tabs->tabText(0) == "General");
    CHECK(tabs->tabText(3) == "Memory");
    CHECK(tabs->isTabEnabled(0));
    CHECK(!tabs->isTabEnabled(3));
    dialog.showPage(SetupDialog::Memory);
    CHECK_EQ(tabs->currentIndex(), 0);

    // What TuxAPE cannot do yet is greyed out.
    for (const char* name : {"ckEnablePlus", "ckPlusPPI", "ckFourDrives", "ckFlyback", "ckDisableUpdate", "bUpdate",
                             "ckTurbo", "cbProfile", "bSaveProfile"}) {
        const QWidget* widget = dialog.findChild<QWidget*>(name);
        CHECK(widget && !widget->isEnabled());
    }

    // Either the speed or the number of frames is in charge.
    CHECK(speed->isEnabled());
    CHECK(!frames->isEnabled());
    every->setChecked(true);
    CHECK(!speed->isEnabled());
    CHECK(frames->isEnabled());
    frames->setValue(12);
    every->setChecked(false);
    CHECK(speed->isEnabled());

    crtc->setCurrentIndex(1);
    fastDisc->setChecked(false);
    speed->setValue(55);
    CHECK(speedLabel->text() == "55%");
    every->setChecked(true);
    const Settings changed = dialog.settings();
    CHECK_EQ(changed.crtcType, 1);
    CHECK(!changed.fastDisc);
    CHECK_EQ(changed.speedPercent, 55);
    CHECK(changed.displayEvery);
    CHECK_EQ(changed.displayEveryFrames, 12);

    if (picture) {
        every->setChecked(false);
        speed->setValue(100);
        crtc->setCurrentIndex(0);
        QApplication::processEvents();
        CHECK(dialog.grab().save(QString::fromLocal8Bit(picture)));
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Never the user's own settings.
    QTemporaryDir folder;
    const QString path = folder.filePath("sub/TuxAPE.ini");
    Settings::setFile(path);
    CHECK(Settings::file() == path);

    testSettingsFile(path);
    testDialog(argc > 1 ? argv[1] : nullptr);

    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; the rest is skipped\n");
        return checkSummary("gui_setup");
    }
    MainWindow window(&emulator);
    window.show();

    // The window starts with the defaults and reads no file by itself.
    CHECK(window.settings() == Settings());
    CHECK(emulator.crtcType() == tuxape::CrtcType::HD6845S);
    CHECK(!emulator.fastDisc());
    CHECK_EQ(emulator.speedPercent(), 100);
    CHECK_EQ(emulator.displayEvery(), 0);

    QAction* general = actionNamed(window, "General");
    QAction* display = actionNamed(window, "Display");
    CHECK(general && general->isEnabled());
    CHECK(display && !display->isEnabled());

    Settings settings;
    settings.crtcType = 1;
    settings.fastDisc = true;
    settings.speedPercent = 400;
    window.applySettings(settings);
    CHECK(window.settings() == settings);
    CHECK(emulator.crtcType() == tuxape::CrtcType::UM6845R);
    CHECK(emulator.fastDisc());
    CHECK_EQ(emulator.speedPercent(), 400);
    CHECK_EQ(emulator.displayEvery(), 0);

    // "Display Every": the machine runs flat out and still gets to its
    // prompt, on the chosen CRTC.
    settings.displayEvery = true;
    settings.displayEveryFrames = 10;
    window.applySettings(settings);
    CHECK_EQ(emulator.displayEvery(), 10);
    emulator.start();
    CHECK(QTest::qWaitFor(
        [&] {
            return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); }).find("Ready")
                   != std::string::npos;
        },
        15000));
    CHECK(emulator.crtcType() == tuxape::CrtcType::UM6845R);

    // The two speeds of the Settings menu leave that mode, and are kept.
    QAction* normal = actionNamed(window, "Normal Speed (100%)");
    CHECK(normal && normal->isEnabled());
    if (normal)
        normal->trigger();
    CHECK_EQ(emulator.displayEvery(), 0);
    CHECK_EQ(emulator.speedPercent(), 100);
    Settings saved;
    saved.load();
    CHECK_EQ(saved.speedPercent, 100);
    CHECK(!saved.displayEvery);
    CHECK_EQ(saved.crtcType, 1);
    CHECK(saved.fastDisc);

    emulator.stop();
    return checkSummary("gui_setup");
}
