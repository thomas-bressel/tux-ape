// The Setup window and the settings behind it: what is kept in TuxAPE.ini,
// what the General page shows and gives back, and what reaches the machine.
// Runs without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_setup [prefix]   also saves pictures of the Setup window's pages
//                        as <prefix>general.png and <prefix>memory.png

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
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

// RAM and ROMs, under the names WinAPE.ini and its profiles give them.
void testMachineInSettingsFile(const QString& path)
{
    using tuxape::CpcModel;
    using tuxape::RamExpansion;

    Settings settings;
    settings.load();
    CHECK(settings.machine == tuxape::stockMachine(CpcModel::Cpc6128));
    CHECK(settings.save());
    tuxape::IniFile ini;
    CHECK(ini.load(path.toStdString()));
    CHECK(ini.get("Configuration", "Extended RAM") == "1");
    CHECK(ini.get("Configuration", "Silicon Disc") == "false");
    CHECK(ini.get("ROMS", "Lower") == "OS6128");
    CHECK(ini.get("ROMS", "Upper(0)") == "BASIC1-1");
    CHECK(ini.has("ROMS", "Upper(1)") && ini.get("ROMS", "Upper(1)").empty());
    CHECK(ini.get("ROMS", "Upper(7)") == "AMSDOS");
    CHECK(ini.has("ROMS", "Upper(31)"));
    CHECK(ini.get("ROMS", "Enable 32 ROMs") == "false");
    CHECK(ini.get("ROMS", "Disable All") == "false");
    CHECK(ini.get("ROMS", "Enable L07") == "false");

    settings.machine = tuxape::stockMachine(CpcModel::Cpc464);
    settings.machine.ram = RamExpansion::Dk256;
    settings.machine.siliconDisc = true;
    settings.machine.upperRoms[6] = "/somewhere/Maxam 1.5.rom";
    settings.machine.upperRoms[20] = "PARADOS";
    settings.machine.rom32 = true;
    settings.machine.onlyLower0And7 = true;
    CHECK(settings.save());
    Settings again;
    again.load();
    CHECK(again == settings);
    CHECK(ini.load(path.toStdString()));
    CHECK(ini.get("Configuration", "Extended RAM") == "2");
    CHECK(ini.get("ROMS", "Upper(7)").empty());  // the 464 has no disc ROM

    // One of WinAPE's profiles, as it stands: what it leaves out is empty.
    QFile::remove(path);
    CHECK(QFile::copy(QStringLiteral(TUXAPE_WINAPE_DIR "/Profile/CPC464 with ParaDOS.wpf"), path));
    again = settings;
    again.load();
    CHECK(again.machine.ram == RamExpansion::None);
    CHECK(!again.machine.siliconDisc);
    CHECK(again.machine.lowerRom == "OS464");
    CHECK(again.machine.upperRoms[0] == "BASIC1-0");
    CHECK(again.machine.upperRoms[6].empty());
    CHECK(again.machine.upperRoms[7] == "ParaDOS 1-2");
    CHECK(!again.machine.rom32);
    CHECK(!again.machine.onlyLower0And7);

    // A file that says nothing of the ROMs leaves the CPC6128's.
    ini = tuxape::IniFile();
    ini.setInt("Configuration", "Extended RAM", 3);
    CHECK(ini.save(path.toStdString()));
    again.load();
    CHECK(again.machine.ram == RamExpansion::Yarek4M);
    CHECK(again.machine.lowerRom == "OS6128");
    CHECK(again.machine.upperRoms[7] == "AMSDOS");

    QFile::remove(path);
}

void testMemoryPage(const QString& picture)
{
    using tuxape::RamExpansion;

    Settings settings;
    settings.machine.upperRoms[20] = "PARADOS";
    SetupDialog dialog(settings);
    dialog.showPage(SetupDialog::Memory);
    dialog.show();

    auto* tabs = dialog.findChild<QTabWidget*>("PageControl");
    QRadioButton* ram[4] = {dialog.findChild<QRadioButton*>("rb64K"), dialog.findChild<QRadioButton*>("rb128K"),
                            dialog.findChild<QRadioButton*>("rb256K"), dialog.findChild<QRadioButton*>("rb4M")};
    auto* siliconDisc = dialog.findChild<QCheckBox*>("ckSiliDisc");
    auto* total = dialog.findChild<QLabel*>("lTotalRAM");
    auto* roms = dialog.findChild<QTableWidget*>("ogROMs");
    auto* rom32 = dialog.findChild<QCheckBox*>("ckEnable32");
    auto* disableRoms = dialog.findChild<QCheckBox*>("ckDisableROMs");
    auto* onlyL07 = dialog.findChild<QCheckBox*>("ckEnableL07");
    const bool found = tabs && ram[0] && ram[1] && ram[2] && ram[3] && siliconDisc && total && roms && rom32
                       && disableRoms && onlyL07;
    CHECK(found);
    if (!found)
        return;
    CHECK(tabs->isTabEnabled(SetupDialog::Memory));
    CHECK_EQ(tabs->currentIndex(), SetupDialog::Memory);

    // RAM: the four choices, the Silicon Disc, and what they come to.
    CHECK(ram[1]->isChecked());
    CHECK(ram[2]->text() == "64K + 256K RAM Expansion");
    CHECK(!siliconDisc->isChecked());
    CHECK(total->text() == "128K");
    ram[0]->setChecked(true);
    CHECK(total->text() == "64K");
    ram[2]->setChecked(true);
    CHECK(total->text() == "320K");
    siliconDisc->setChecked(true);
    CHECK(total->text() == "576K");
    ram[3]->setChecked(true);
    CHECK(total->text() == "4160K");

    // ROMs: the firmware and sixteen slots, or thirty-two.
    CHECK_EQ(roms->rowCount(), 17);
    CHECK(roms->item(0, 0)->text() == "Lower");
    CHECK(roms->item(1, 0)->text() == "Upper 0");
    CHECK(roms->item(16, 0)->text() == "Upper 15");
    CHECK(dialog.rom(0) == "OS6128");
    CHECK(dialog.rom(1) == "BASIC1-1");
    CHECK(dialog.rom(2).isEmpty());
    CHECK(dialog.rom(8) == "AMSDOS");
    CHECK(SetupDialog::emptyRomText(0) == "(Empty)");
    CHECK(SetupDialog::emptyRomText(1) == "(Empty)");
    CHECK(SetupDialog::emptyRomText(5) == "(Empty - Same as Upper 0)");
    rom32->setChecked(true);
    CHECK_EQ(roms->rowCount(), 33);
    CHECK(roms->item(32, 0)->text() == "Upper 31");
    CHECK(dialog.rom(21) == "PARADOS");  // was there all along
    rom32->setChecked(false);
    CHECK_EQ(roms->rowCount(), 17);

    // A cell is edited with a list: nothing, the images of the ROM folder,
    // or a file to be chosen.
    roms->setCurrentCell(3, 1);  // going to a cell opens its list
    auto* combo = roms->findChild<QComboBox*>();
    CHECK(combo != nullptr);
    if (combo) {
        CHECK(combo->itemText(0) == "(Empty - Same as Upper 0)");
        CHECK(combo->itemText(combo->count() - 1) == "Select File...");
        CHECK_EQ(combo->currentIndex(), 0);
        const int amsdos = combo->findText("AMSDOS");
        CHECK(amsdos > 0);
        CHECK(combo->findText("ParaDOS 1-2+") > 0);
        CHECK(combo->findText("CPC_PLUS") < 0);
        combo->setCurrentIndex(amsdos);
        emit combo->activated(amsdos);
        CHECK(dialog.rom(3) == "AMSDOS");
    }

    // An image of the ROM folder goes by its bare name; any other file by
    // its path.
    const QString romDir = QString::fromStdString(tuxape::defaultRomDir().string());
    dialog.setRom(9, romDir + "/PARADOS.ROM");
    CHECK(dialog.rom(9) == "PARADOS");
    dialog.setRom(10, "/elsewhere/Maxam.rom");
    CHECK(dialog.rom(10) == "/elsewhere/Maxam.rom");
    dialog.setRom(8, QString());
    dialog.setRom(0, "OS464");
    dialog.setRom(1, "BASIC1-0");
    disableRoms->setChecked(true);
    onlyL07->setChecked(true);

    const tuxape::MachineConfig machine = dialog.settings().machine;
    CHECK(machine.ram == RamExpansion::Yarek4M);
    CHECK(machine.siliconDisc);
    CHECK(machine.lowerRom == "OS464");
    CHECK(machine.upperRoms[0] == "BASIC1-0");
    CHECK(machine.upperRoms[2] == "AMSDOS");
    CHECK(machine.upperRoms[7].empty());
    CHECK(machine.upperRoms[8] == "PARADOS");
    CHECK(machine.upperRoms[9] == "/elsewhere/Maxam.rom");
    CHECK(machine.upperRoms[20] == "PARADOS");
    CHECK(!machine.rom32);
    CHECK(machine.disableAllRoms);
    CHECK(machine.onlyLower0And7);
    // The other pages' settings come back untouched.
    CHECK_EQ(dialog.settings().crtcType, settings.crtcType);
    CHECK_EQ(dialog.settings().speedPercent, settings.speedPercent);

    // Cartridges and the Multiface are still to come.
    for (const char* name : {"ckEnableCart", "sbCartridge", "ckEnableMultiface", "sbMultiface"}) {
        const QWidget* widget = dialog.findChild<QWidget*>(name);
        CHECK(widget && !widget->isEnabled());
    }

    if (!picture.isEmpty()) {
        SetupDialog fresh((Settings()));
        fresh.showPage(SetupDialog::Memory);
        fresh.show();
        QApplication::processEvents();
        CHECK(fresh.grab().save(picture));
    }
}

void testDialog(const QString& picture)
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
    CHECK(tabs->isTabEnabled(3));
    CHECK(!tabs->isTabEnabled(1));
    dialog.showPage(SetupDialog::Display);
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

    if (!picture.isEmpty()) {
        every->setChecked(false);
        speed->setValue(100);
        crtc->setCurrentIndex(0);
        QApplication::processEvents();
        CHECK(dialog.grab().save(picture));
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

    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    testSettingsFile(path);
    testMachineInSettingsFile(path);
    testDialog(prefix.isEmpty() ? QString() : prefix + "general.png");
    testMemoryPage(prefix.isEmpty() ? QString() : prefix + "memory.png");

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
    QAction* memory = actionNamed(window, "Memory");
    CHECK(general && general->isEnabled());
    CHECK(display && !display->isEnabled());
    CHECK(memory && memory->isEnabled());
    CHECK(emulator.machine() == tuxape::stockMachine(tuxape::CpcModel::Cpc6128));

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

    // Another machine is fitted without a reset, as in WinAPE: the CPC6128
    // goes on running, and the CPC464 shows at the next reset.
    auto screen = [&] {
        return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); });
    };
    emulator.setSpeedPercent(1000);
    settings = window.settings();
    settings.machine = tuxape::stockMachine(tuxape::CpcModel::Cpc464);
    window.applySettings(settings);
    CHECK(emulator.machine() == settings.machine);
    CHECK(emulator.model() == tuxape::CpcModel::Cpc464);
    CHECK_EQ(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().ramSizeKb(); }), 64);
    CHECK(screen().find("BASIC 1.1") != std::string::npos);
    emulator.reset(true);
    CHECK(QTest::qWaitFor([&] { return screen().find("BASIC 1.0") != std::string::npos; }, 15000));
    CHECK(QTest::qWaitFor([&] { return screen().find("Ready") != std::string::npos; }, 15000));

    // A ROM image that cannot be read is named, and its place left empty.
    tuxape::MachineConfig broken = settings.machine;
    broken.upperRoms[4] = "NOPE";
    const QString error = emulator.setupMachine(broken, false);
    CHECK(error.contains("NOPE"));
    CHECK(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().hasUpperRom(0); }));
    CHECK(!emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().hasUpperRom(4); }));

    emulator.stop();
    return checkSummary("gui_setup");
}
