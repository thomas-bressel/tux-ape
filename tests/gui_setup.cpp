// The Setup window and the settings behind it: what is kept in TuxAPE.ini,
// what the General page shows and gives back, and what reaches the machine.
// Runs without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_setup [prefix]   also saves pictures of the Setup window's pages
//                        as <prefix>general.png, <prefix>display.png,
//                        <prefix>sound.png, <prefix>memory.png,
//                        <prefix>input.png and <prefix>profile.png

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include "check.h"
#include "core/inifile.h"
#include "core/screen_text.h"
#include "core/keymap.h"
#include "emulator.h"
#include "hostjoystick.h"
#include "mainwindow.h"
#include "screenwidget.h"
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

    // The Multiface is still to come.
    for (const char* name : {"ckEnableMultiface", "sbMultiface"}) {
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

// Profiles: WinAPE's own, read as they stand, and ones saved from here.
void testProfiles(const QString& folder, const QString& picture)
{
    using tuxape::CpcModel;
    const QString profiles = QStringLiteral(TUXAPE_WINAPE_DIR "/Profile");
    CHECK(Settings::profileFolder() == profiles);
    CHECK(Settings::profileUsable(profiles + "/CPC6128.wpf"));
    CHECK(Settings::profileUsable(profiles + "/6128 Plus.wpf"));
    CHECK(!Settings::profileUsable(profiles + "/no such profile.wpf"));

    Settings settings;
    settings.crtcType = 2;
    settings.speedPercent = 200;
    settings.fastDisc = true;
    SetupDialog dialog(settings);
    dialog.show();
    auto* combo = dialog.findChild<QComboBox*>("cbProfile");
    auto* save = dialog.findChild<QPushButton*>("bSaveProfile");
    CHECK(combo && combo->isEnabled());
    CHECK(save && save->isEnabled());
    if (!combo)
        return;

    // The list: the current settings, WinAPE's eight profiles in
    // alphabetical order, a file to choose.
    CHECK_EQ(combo->count(), 10);
    CHECK(combo->itemText(0) == "(Current Settings)");
    CHECK(combo->itemText(1) == "464 Plus");
    CHECK(combo->itemText(5) == "CPC464");
    CHECK(combo->itemText(8) == "CPC6128 with ParaDOS");
    CHECK(combo->itemText(9) == "Select File...");
    CHECK_EQ(combo->currentIndex(), 0);
    const auto* model = qobject_cast<QStandardItemModel*>(combo->model());
    CHECK(model != nullptr);
    if (model) {
        CHECK(model->item(1)->isEnabled());
        CHECK(model->item(4)->isEnabled());
        CHECK(model->item(5)->isEnabled());
        CHECK(model->item(9)->isEnabled());
    }

    // Choosing one changes what it holds and nothing else.
    auto choose = [&](const char* name) {
        const int index = combo->findText(name);
        CHECK(index > 0);
        combo->setCurrentIndex(index);
        emit combo->activated(index);
    };
    choose("CPC464");
    Settings shown = dialog.settings();
    CHECK(shown.machine == tuxape::stockMachine(CpcModel::Cpc464));
    CHECK_EQ(shown.crtcType, 0);       // the profile says type 0
    CHECK_EQ(shown.speedPercent, 200);  // and nothing of this
    CHECK(shown.fastDisc);
    choose("CPC6128 with ParaDOS");
    shown = dialog.settings();
    CHECK(shown.machine.ram == tuxape::RamExpansion::Internal);
    CHECK(shown.machine.lowerRom == "OS6128");
    CHECK(shown.machine.upperRoms[7] == "ParaDOS 1-2");
    CHECK(!tuxape::findRom(shown.machine.upperRoms[7], tuxape::defaultRomDir()).empty());
    // The pages follow.
    auto* total = dialog.findChild<QLabel*>("lTotalRAM");
    CHECK(total && total->text() == "128K");
    CHECK(dialog.rom(8) == "ParaDOS 1-2");

    // A file that is not there changes nothing.
    CHECK(!dialog.loadProfile(folder + "/missing.wpf"));
    CHECK(dialog.settings() == shown);

    // Which settings a profile is to hold: the machine, unless told
    // otherwise.
    ProfilePartsDialog partsDialog;
    partsDialog.show();
    CHECK_EQ(partsDialog.parts(), Settings::CrtcPart | Settings::RamPart | Settings::RomsPart);
    auto* tree = partsDialog.findChild<QTreeWidget*>("tvSettings");
    CHECK(tree && tree->topLevelItemCount() == 5);
    if (tree && tree->topLevelItemCount() == 5) {
        CHECK(tree->topLevelItem(2)->text(0) == "Input");
        CHECK(tree->topLevelItem(4)->text(0) == "Sound");
        tree->topLevelItem(4)->setCheckState(0, Qt::Checked);
        CHECK_EQ(partsDialog.parts(), Settings::CrtcPart | Settings::RamPart | Settings::RomsPart | Settings::SoundPart);
        tree->topLevelItem(4)->setCheckState(0, Qt::Unchecked);
        CHECK(tree->topLevelItem(0)->text(0) == "Display");
        CHECK(tree->topLevelItem(1)->text(0) == "General");
        CHECK(tree->topLevelItem(3)->text(0) == "Memory");
        CHECK(tree->topLevelItem(0)->checkState(0) == Qt::Unchecked);
        CHECK(tree->topLevelItem(1)->checkState(0) == Qt::PartiallyChecked);
        CHECK(tree->topLevelItem(3)->checkState(0) == Qt::Checked);
        // Ticking a group ticks all of it.
        for (int group : {0, 1, 2, 4})
            tree->topLevelItem(group)->setCheckState(0, Qt::Checked);
        CHECK_EQ(partsDialog.parts(), Settings::AllParts);
        for (int group : {0, 2, 3, 4})
            tree->topLevelItem(group)->setCheckState(0, Qt::Unchecked);
        CHECK_EQ(partsDialog.parts(), Settings::CrtcPart | Settings::SpeedPart | Settings::FastDiscPart);
        if (!picture.isEmpty()) {
            QApplication::processEvents();
            CHECK(partsDialog.grab().save(picture));
        }
    }

    // A profile saved with only the machine in it...
    const QString path = folder + "/My machine.wpf";
    dialog.setRom(5, "PARADOS");
    CHECK(dialog.saveProfile(path, Settings::RamPart | Settings::RomsPart));
    tuxape::IniFile ini;
    CHECK(ini.load(path.toStdString()));
    CHECK(ini.get("Configuration", "Extended RAM") == "1");
    CHECK(ini.get("ROMS", "Lower") == "OS6128");
    CHECK(ini.get("ROMS", "Upper(4)") == "PARADOS");
    CHECK(!ini.has("Configuration", "CRTC Type"));
    CHECK(!ini.has("Configuration", "Emulation Speed"));
    CHECK(!ini.has("Drives", "Fast Disc"));
    // ...brings that machine back and leaves the rest.
    Settings other;
    other.crtcType = 4;
    other.machine = tuxape::stockMachine(CpcModel::Cpc464);
    SetupDialog second(other);
    CHECK(second.loadProfile(path));
    CHECK(second.settings().machine == dialog.settings().machine);
    CHECK_EQ(second.settings().crtcType, 4);
    CHECK_EQ(second.settings().speedPercent, 100);

    // The user's own profiles are kept beside the settings file and listed
    // with the others.
    CHECK(Settings::userProfileFolder() == QFileInfo(Settings::file()).absolutePath() + "/Profile");
    CHECK(QDir().mkpath(Settings::userProfileFolder()));
    CHECK(dialog.saveProfile(Settings::userProfileFolder() + "/Big machine.wpf", Settings::AllParts));
    {
        SetupDialog later((Settings()));
        auto* list = later.findChild<QComboBox*>("cbProfile");
        CHECK(list && list->count() == 11);
        if (list && list->count() == 11) {
            CHECK(list->itemText(5) == "Big machine");  // after "6128 Plus with ParaDOS", before "CPC464"
            CHECK(list->itemText(6) == "CPC464");
            list->setCurrentIndex(5);
            emit list->activated(5);
            CHECK(later.settings() == dialog.settings());
        }
    }
    QFile::remove(Settings::userProfileFolder() + "/Big machine.wpf");
}

// The Display page: the monitor, the vertical hold, the brightness, and what
// the window shows around the picture.
void testDisplayPage(const QImage& frame, const QString& picture)
{
    // The defaults are WinAPE's: in full screen nothing but the picture.
    // But for the mouse pointer, which WinAPE hides and TuxAPE leaves.
    const Settings defaults;
    CHECK_EQ(defaults.monitorType, 0);
    CHECK(defaults.linearPalette);
    CHECK(defaults.windowed.renderBothLines && !defaults.windowed.hideMouse);
    CHECK(!defaults.windowed.hidePanel && !defaults.windowed.hideMenus && !defaults.windowed.halfSize);
    CHECK(defaults.fullScreen.hidePanel && defaults.fullScreen.hideMenus && !defaults.fullScreen.hideMouse);

    Settings settings;
    settings.verticalHold = -5;
    SetupDialog dialog(settings);
    dialog.setPreview(frame);
    dialog.showPage(SetupDialog::Display);
    dialog.show();
    auto* tabs = dialog.findChild<QTabWidget*>("PageControl");
    CHECK(tabs && tabs->currentIndex() == SetupDialog::Display);

    auto* colour = dialog.findChild<QRadioButton*>("rbColour");
    auto* green = dialog.findChild<QRadioButton*>("rbGreen");
    auto* grey = dialog.findChild<QRadioButton*>("rbGreyscale");
    auto* hold = dialog.findChild<QSlider*>("slVSync");
    auto* holdLabel = dialog.findChild<QLabel*>("lVSync");
    auto* bright = dialog.findChild<QSlider*>("slBright");
    auto* brightLabel = dialog.findChild<QLabel*>("lBright");
    auto* linear = dialog.findChild<QCheckBox*>("ckLinearPalette");
    auto* preview = dialog.findChild<QLabel*>("iScreen");
    auto* halfSize = dialog.findChild<QCheckBox*>("ckHalfSize");
    auto* hidePanel = dialog.findChild<QCheckBox*>("ckHidePanel");
    auto* hidePanelFs = dialog.findChild<QCheckBox*>("ckHidePanelFS");
    auto* bothLinesFs = dialog.findChild<QCheckBox*>("ckRenderBothFS");
    auto* noRightClick = dialog.findChild<QCheckBox*>("ckNoRightClick");
    const bool found = colour && green && grey && hold && holdLabel && bright && brightLabel && linear && preview
                       && halfSize && hidePanel && hidePanelFs && bothLinesFs && noRightClick;
    CHECK(found);
    if (!found)
        return;

    CHECK(colour->isChecked());
    CHECK_EQ(hold->value(), -5);
    CHECK(holdLabel->text() == "-5");
    CHECK_EQ(bright->value(), 0);
    CHECK(brightLabel->text() == "0");
    CHECK(linear->isChecked());
    CHECK(!halfSize->isChecked() && !hidePanel->isChecked() && hidePanelFs->isChecked());
    CHECK(bothLinesFs->isChecked());
    CHECK(dialog.settings() == settings);

    // The piece of picture shown follows the monitor chosen: on a green
    // screen nothing has any red or blue.
    auto shown = [&] { return preview->pixmap(Qt::ReturnByValue).toImage().convertToFormat(QImage::Format_RGB32); };
    auto count = [&](auto&& test) {
        const QImage image = shown();
        int matching = 0;
        for (int y = 0; y < image.height(); y += 3)
            for (int x = 0; x < image.width(); x += 3)
                matching += test(image.pixel(x, y)) ? 1 : 0;
        return matching;
    };
    CHECK(!shown().isNull());
    CHECK(count([](QRgb p) { return qBlue(p) > 0x40; }) > 100);  // the firmware's blue paper
    green->setChecked(true);
    CHECK_EQ(count([](QRgb p) { return qRed(p) != 0 || qBlue(p) != 0; }), 0);
    CHECK(count([](QRgb p) { return qGreen(p) > 0x10; }) > 100);
    grey->setChecked(true);
    CHECK_EQ(count([](QRgb p) { return qRed(p) != qGreen(p) || qGreen(p) != qBlue(p); }), 0);
    // Brighter is brighter: the paper, nearly black in grey, comes up.
    const int before = count([](QRgb p) { return qGreen(p) > 0x40; });
    bright->setValue(60);
    CHECK(brightLabel->text() == "60");
    CHECK(count([](QRgb p) { return qGreen(p) > 0x40; }) > before);

    hold->setValue(12);
    CHECK(holdLabel->text() == "12");
    linear->setChecked(false);
    halfSize->setChecked(true);
    hidePanel->setChecked(true);
    hidePanelFs->setChecked(false);
    bothLinesFs->setChecked(false);
    noRightClick->setChecked(true);
    const Settings changed = dialog.settings();
    CHECK_EQ(changed.monitorType, 2);
    CHECK_EQ(changed.brightness, 60);
    CHECK_EQ(changed.verticalHold, 12);
    CHECK(!changed.linearPalette);
    CHECK(changed.windowed.halfSize && changed.windowed.hidePanel && changed.windowed.noRightClick);
    CHECK(changed.windowed.renderBothLines && !changed.windowed.hideMouse && !changed.windowed.hideMenus);
    CHECK(!changed.fullScreen.hidePanel && !changed.fullScreen.renderBothLines && changed.fullScreen.hideMenus);

    // Kept under WinAPE's keys, and brought back.
    CHECK(changed.save());
    tuxape::IniFile ini;
    CHECK(ini.load(Settings::file().toStdString()));
    CHECK(ini.get("Configuration", "Monitor Type") == "2");
    CHECK(ini.get("Configuration", "Monitor Brightness") == "60");
    CHECK(ini.get("Configuration", "VHOLD Position") == "12");
    CHECK(ini.get("Configuration", "Linear Palette") == "false");
    CHECK(ini.get("Configuration", "Half Size") == "true");
    CHECK(ini.get("Configuration", "Half Size FS") == "false");
    CHECK(ini.get("Configuration", "Hide Panel") == "true");
    CHECK(ini.get("Configuration", "Hide Panel FS") == "false");
    CHECK(ini.get("Configuration", "Render Both FS") == "false");
    CHECK(ini.get("Configuration", "No Right Click") == "true");
    Settings again;
    again.load();
    CHECK(again == changed);
    QFile::remove(Settings::file());

    // What does not exist yet is greyed out.
    for (const char* name : {"ckDXStretch", "rb8bitFS", "rb16bitFS"}) {
        const QWidget* widget = dialog.findChild<QWidget*>(name);
        CHECK(widget && !widget->isEnabled());
    }
    // The drive's light and its cylinder are there to tick.
    for (const char* name : {"ckPAL", "ckDriveLED", "ckShowTrack"}) {
        const auto* box = dialog.findChild<QCheckBox*>(name);
        CHECK(box && box->isEnabled() && !box->isChecked());
    }

    if (!picture.isEmpty()) {
        SetupDialog fresh((Settings()));
        fresh.setPreview(frame);
        fresh.showPage(SetupDialog::Display);
        fresh.show();
        QApplication::processEvents();
        CHECK(fresh.grab().save(picture));
    }
}

// The Sound page: where the sound goes and how it is made.
void testSoundPage(const QString& picture)
{
    const Settings defaults;
    CHECK(defaults.soundOn && defaults.sound16Bit && defaults.soundStereo);
    CHECK_EQ(defaults.soundRate, 44100);
    CHECK_EQ(defaults.soundVolume, 15);

    Settings settings;
    settings.soundVolume = 9;
    settings.soundBufferSync = 4;
    SetupDialog dialog(settings);
    dialog.showPage(SetupDialog::Sound);
    dialog.show();

    auto* none = dialog.findChild<QRadioButton*>("rbNone");
    auto* speaker = dialog.findChild<QRadioButton*>("rbSpeaker");
    auto* card = dialog.findChild<QRadioButton*>("rbDirectSound");
    auto* rate22 = dialog.findChild<QRadioButton*>("rb22");
    auto* rate44 = dialog.findChild<QRadioButton*>("rb44");
    auto* bits8 = dialog.findChild<QRadioButton*>("rb8bit");
    auto* bits16 = dialog.findChild<QRadioButton*>("rb16bit");
    auto* mono = dialog.findChild<QRadioButton*>("rbMono");
    auto* stereo = dialog.findChild<QRadioButton*>("rbStereo");
    auto* volume = dialog.findChild<QSlider*>("slVolume");
    auto* volumeLabel = dialog.findChild<QLabel*>("lVolume");
    auto* sync = dialog.findChild<QSlider*>("slBufferSync");
    auto* syncLabel = dialog.findChild<QLabel*>("lBufferSync");
    const bool found = none && speaker && card && rate22 && rate44 && bits8 && bits16 && mono && stereo && volume
                       && volumeLabel && sync && syncLabel;
    CHECK(found);
    if (!found)
        return;

    CHECK(card->isChecked() && rate44->isChecked() && bits16->isChecked() && stereo->isChecked());
    CHECK(!speaker->isEnabled());  // no PC speaker here
    CHECK_EQ(volume->value(), 9);
    CHECK(volumeLabel->text() == "9");
    CHECK_EQ(sync->value(), 4);
    CHECK(syncLabel->text() == "0.4");
    CHECK(dialog.settings() == settings);

    // Without the sound card its options mean nothing.
    none->setChecked(true);
    CHECK(!rate22->isEnabled() && !bits8->isEnabled() && !mono->isEnabled() && !volume->isEnabled());
    CHECK(!dialog.settings().soundOn);
    card->setChecked(true);
    CHECK(rate22->isEnabled() && volume->isEnabled());

    rate22->setChecked(true);
    bits8->setChecked(true);
    mono->setChecked(true);
    volume->setValue(3);
    sync->setValue(15);
    CHECK(syncLabel->text() == "1.5");
    const Settings changed = dialog.settings();
    CHECK(changed.soundOn);
    CHECK_EQ(changed.soundRate, 22050);
    CHECK(!changed.sound16Bit && !changed.soundStereo);
    CHECK_EQ(changed.soundVolume, 3);
    CHECK_EQ(changed.soundBufferSync, 15);

    // Kept under WinAPE's keys where it has them.
    CHECK(changed.save());
    tuxape::IniFile ini;
    CHECK(ini.load(Settings::file().toStdString()));
    CHECK(ini.get("Configuration", "Sound Mode") == "2");
    CHECK(ini.get("Configuration", "Sound Bits") == "8");
    CHECK(ini.get("Configuration", "Sound Stereo") == "false");
    CHECK(ini.get("Configuration", "Sound Volume") == "3");
    CHECK(ini.get("Configuration", "Sound Frame Delay") == "1.5");
    Settings again;
    again.load();
    CHECK(again == changed);
    // WinAPE.ini as it comes: the sound card, 8 bits, stereo, full volume.
    QFile::remove(Settings::file());
    CHECK(QFile::copy(QStringLiteral(TUXAPE_WINAPE_DIR "/WinAPE.ini"), Settings::file()));
    again.load();
    CHECK(again.soundOn && !again.sound16Bit && again.soundStereo);
    CHECK_EQ(again.soundVolume, 15);
    CHECK_EQ(again.soundBufferSync, 0);
    QFile::remove(Settings::file());

    for (const char* name : {"ckDiscSound"}) {
        const QWidget* widget = dialog.findChild<QWidget*>(name);
        CHECK(widget && !widget->isEnabled());
    }
    // The tape can be heard while it loads.
    {
        const auto* tapeSound = dialog.findChild<QCheckBox*>("ckTapeSound");
        CHECK(tapeSound && tapeSound->isEnabled() && !tapeSound->isChecked());
    }

    if (!picture.isEmpty()) {
        SetupDialog fresh((Settings()));
        fresh.showPage(SetupDialog::Sound);
        fresh.show();
        QApplication::processEvents();
        CHECK(fresh.grab().save(picture));
    }
}

// The Input page: the CPC's keyboard to click on, the PC keys of the key
// clicked, the joystick, and layouts in WinAPE's .kbd files.
void testInputPage(const QString& folder, const QString& picture)
{
    using tuxape::CpcKey;
    using tuxape::KeyMap;

    // Every key of the standard layout has a name to show, and no two keys
    // share one.
    const KeyMap standard;
    for (int state = 0; state < 2; ++state)
        for (int key = 0; key < tuxape::kCpcKeyCount; ++key)
            for (int alternative = 0; alternative < KeyMap::kAlternatives; ++alternative) {
                const uint8_t pcKey = standard.pcKey(state != 0, static_cast<CpcKey>(key), alternative);
                CHECK(pcKey == 0 || tuxape::pcKeyName(pcKey) != nullptr);
            }
    QStringList names;
    for (const uint8_t pcKey : tuxape::namedPcKeys())
        names << QString::fromLatin1(tuxape::pcKeyName(pcKey));
    CHECK(names.size() > 80);
    CHECK_EQ(names.removeDuplicates(), 0);
    CHECK(tuxape::pcKeyName(0) == nullptr);
    CHECK(QString::fromLatin1(tuxape::pcKeyName(tuxape::PcNum4)) == "Num 4");

    Settings settings;
    CHECK(settings.joystick);  // as in WinAPE
    SetupDialog dialog(settings);
    dialog.showPage(SetupDialog::Input);
    dialog.show();
    auto* tabs = dialog.findChild<QTabWidget*>("PageControl");
    auto* load = dialog.findChild<QPushButton*>("bLoadKeys");
    auto* save = dialog.findChild<QPushButton*>("bSaveKeys");
    auto* joystick = dialog.findChild<QCheckBox*>("ckJoystick");
    auto* mouse = dialog.findChild<QCheckBox*>("ckAMXMouse");
    QComboBox* off[3] = {dialog.findChild<QComboBox*>("cbKey1"), dialog.findChild<QComboBox*>("cbKey2"),
                         dialog.findChild<QComboBox*>("cbKey3")};
    QComboBox* on[3] = {dialog.findChild<QComboBox*>("cbKeyNL1"), dialog.findChild<QComboBox*>("cbKeyNL2"),
                        dialog.findChild<QComboBox*>("cbKeyNL3")};
    auto* escape = dialog.findChild<QAbstractButton*>("k66");
    auto* shift = dialog.findChild<QAbstractButton*>("k21");
    auto* shift2 = dialog.findChild<QAbstractButton*>("j21");
    auto* letterA = dialog.findChild<QAbstractButton*>("k69");
    auto* joyUp = dialog.findChild<QAbstractButton*>("k72");
    const bool found = tabs && load && save && joystick && mouse && off[0] && off[1] && off[2] && on[0] && on[1]
                       && on[2] && escape && shift && shift2 && letterA && joyUp;
    CHECK(found);
    if (!found)
        return;
    CHECK_EQ(tabs->currentIndex(), SetupDialog::Input);
    CHECK(joystick->isChecked());
    // The AMX mouse is there to tick, and kept with the settings.
    CHECK(mouse->isEnabled() && !mouse->isChecked());
    mouse->setChecked(true);
    CHECK(dialog.settings().amxMouse);
    mouse->setChecked(false);
    CHECK(!dialog.settings().amxMouse);
    // Load and Save belong to this page.
    CHECK(load->isVisible() && save->isVisible());
    dialog.showPage(SetupDialog::General);
    CHECK(!load->isVisible() && !save->isVisible());
    dialog.showPage(SetupDialog::Input);
    // The whole keyboard is there: 73 keys, the second SHIFT, and the
    // joystick's four directions and three buttons.
    CHECK_EQ(dialog.findChild<QWidget*>("pKeyboard")->findChildren<QAbstractButton*>().size(), 81);

    // Nothing clicked: nothing to change.
    CHECK_EQ(dialog.selectedCpcKey(), -1);
    CHECK(!off[0]->isEnabled() && !on[0]->isEnabled());
    CHECK(dialog.keyMap() == standard);

    // A key clicked shows the PC keys that press it.
    letterA->click();
    CHECK_EQ(dialog.selectedCpcKey(), static_cast<int>(CpcKey::A));
    CHECK(letterA->isChecked() && !escape->isChecked());
    CHECK(off[0]->isEnabled() && on[2]->isEnabled());
    CHECK(off[0]->currentText() == "A" && on[0]->currentText() == "A");
    CHECK(off[1]->currentText().isEmpty() && on[1]->currentText().isEmpty());
    // The two SHIFT keys are one.
    shift2->click();
    CHECK(shift->isChecked() && shift2->isChecked() && !letterA->isChecked());
    CHECK(off[0]->currentText() == "Left Shift" && off[1]->currentText() == "Right Shift");
    // The joystick is on the numeric keypad when Num Lock is off.
    joyUp->click();
    CHECK(off[0]->currentText() == "Num 8" && off[1]->currentText() == "Num 7" && off[2]->currentText() == "Num 9");
    CHECK(on[0]->currentText().isEmpty());

    // Choosing a PC key changes the layout, for that Num Lock state only.
    const int home = on[0]->findText("Home");
    CHECK(home > 0);
    on[0]->setCurrentIndex(home);
    emit on[0]->activated(home);
    CHECK_EQ(dialog.keyMap().pcKey(true, CpcKey::JoyUp, 0), tuxape::PcHome);
    CHECK_EQ(dialog.keyMap().pcKey(false, CpcKey::JoyUp, 0), tuxape::PcNum8);
    CHECK(!(dialog.keyMap() == standard));
    escape->click();
    joyUp->click();
    CHECK(on[0]->currentText() == "Home");
    // ...and choosing the empty line takes it away.
    on[0]->setCurrentIndex(0);
    emit on[0]->activated(0);
    CHECK(dialog.keyMap() == standard);

    // WinAPE's own layout file is the standard layout; a layout saved is
    // read back the same.
    off[2]->setCurrentIndex(off[2]->findText("Page Up"));
    emit off[2]->activated(off[2]->currentIndex());
    const KeyMap changed = dialog.keyMap();
    const QString path = folder + "/mine.kbd";
    CHECK(dialog.saveKeyboard(path));
    CHECK(dialog.settings().keyboardFile == path);
    CHECK(dialog.loadKeyboard(QStringLiteral(TUXAPE_WINAPE_DIR "/default.kbd")));
    CHECK(dialog.keyMap() == standard);
    CHECK(off[2]->currentText() == "Num 9");  // the lists follow
    CHECK(dialog.loadKeyboard(path));
    CHECK(dialog.keyMap() == changed);
    CHECK(!dialog.loadKeyboard(folder + "/missing.kbd"));
    CHECK(!dialog.loadKeyboard(QStringLiteral(TUXAPE_WINAPE_DIR "/WinAPE.ini")));  // not a layout
    CHECK(dialog.keyMap() == changed);

    // The settings side: the joystick switch and the name of the layout,
    // and the layout in use kept beside the settings file.
    joystick->setChecked(false);
    Settings result = dialog.settings();
    CHECK(!result.joystick);
    CHECK(result.save());
    tuxape::IniFile ini;
    CHECK(ini.load(Settings::file().toStdString()));
    CHECK(ini.get("Configuration", "Joystick Enabled") == "false");
    CHECK(QString::fromStdString(ini.get("Configuration", "Keyboard File")) == path);
    Settings again;
    again.load();
    CHECK(again == result);
    KeyMap kept;
    CHECK(!Settings::loadKeyMap(kept));  // nothing saved yet
    CHECK(kept == standard);
    CHECK(Settings::saveKeyMap(changed));
    CHECK(Settings::loadKeyMap(kept));
    CHECK(kept == changed);
    QFile::remove(Settings::keyMapFile());
    QFile::remove(Settings::file());

    if (!picture.isEmpty()) {
        SetupDialog fresh((Settings()));
        fresh.showPage(SetupDialog::Input);
        fresh.show();
        fresh.selectCpcKey(static_cast<int>(CpcKey::JoyFire2));
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
    CHECK(tabs->isTabEnabled(1));
    CHECK(tabs->isTabEnabled(2));
    CHECK(tabs->isTabEnabled(3));
    CHECK(tabs->isTabEnabled(4));
    CHECK(tabs->isTabEnabled(5) && tabs->tabText(5) == "Other");
    dialog.showPage(SetupDialog::Other);
    CHECK_EQ(tabs->currentIndex(), 5);
    dialog.showPage(SetupDialog::General);
    CHECK_EQ(tabs->currentIndex(), 0);

    // The Other page: what is on the printer's port. A file's name is
    // only asked for when printing goes to one; the Symbiface and the
    // host's printer are still to come.
    {
        auto* disabled = dialog.findChild<QRadioButton*>("rbPrnDisabled");
        auto* digiblaster = dialog.findChild<QRadioButton*>("rbPrnDigiblaster");
        auto* hostPrinter = dialog.findChild<QRadioButton*>("rbPrnPrinter");
        auto* toFile = dialog.findChild<QRadioButton*>("rbPrnFile");
        auto* toAssembler = dialog.findChild<QRadioButton*>("rbPrnAssembler");
        auto* file = dialog.findChild<QLineEdit*>("edPrinterFile");
        auto* amDrum = dialog.findChild<QCheckBox*>("ckAmDrum");
        const bool there = disabled && digiblaster && hostPrinter && toFile && toAssembler && file && amDrum;
        CHECK(there);
        if (there) {
            CHECK(disabled->isChecked() && !hostPrinter->isEnabled() && !file->isEnabled());
            CHECK(amDrum->isEnabled() && !amDrum->isChecked());
            CHECK_EQ(dialog.settings().printerMode, Settings::PrinterDisabled);
            digiblaster->setChecked(true);
            CHECK_EQ(dialog.settings().printerMode, Settings::PrinterDigiblaster);
            toAssembler->setChecked(true);
            CHECK_EQ(dialog.settings().printerMode, Settings::PrinterAssembler);
            toFile->setChecked(true);
            CHECK(file->isEnabled());
            file->setText("/tmp/printed.txt");
            amDrum->setChecked(true);
            Settings chosen = dialog.settings();
            CHECK(chosen.printerMode == Settings::PrinterFile && chosen.printerFile == "/tmp/printed.txt" && chosen.amDrum);
            // Kept with the settings, and shown again.
            CHECK(chosen.save());
            Settings loaded;
            loaded.load();
            CHECK(loaded.printerMode == Settings::PrinterFile && loaded.printerFile == "/tmp/printed.txt" && loaded.amDrum);
            SetupDialog again(loaded);
            CHECK(again.findChild<QRadioButton*>("rbPrnFile")->isChecked());
            CHECK(again.findChild<QLineEdit*>("edPrinterFile")->text() == "/tmp/printed.txt");
            CHECK(again.findChild<QCheckBox*>("ckAmDrum")->isChecked());
            QFile::remove(Settings::file());
            disabled->setChecked(true);
            amDrum->setChecked(false);
            file->clear();
        }
        for (const char* name : {"ckIDE", "ckRTC", "ckPS2Mouse", "ckAdjustMouse", "cbSmartWatch"}) {
            const QWidget* widget = dialog.findChild<QWidget*>(name);
            CHECK(widget && !widget->isEnabled());
        }
    }

    // What TuxAPE cannot do yet is greyed out.
    for (const char* name :
         {"ckFlyback", "ckDisableUpdate", "bUpdate"}) {
        const QWidget* widget = dialog.findChild<QWidget*>(name);
        CHECK(widget && !widget->isEnabled());
    }
    // Turbo and the Plus's PPI are there to tick.
    for (const char* name : {"ckPlusPPI", "ckTurbo", "ckFourDrives"}) {
        const auto* box = dialog.findChild<QCheckBox*>(name);
        CHECK(box && box->isEnabled() && !box->isChecked());
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
    testSoundPage(prefix.isEmpty() ? QString() : prefix + "sound.png");
    testMemoryPage(prefix.isEmpty() ? QString() : prefix + "memory.png");
    testInputPage(folder.path(), prefix.isEmpty() ? QString() : prefix + "input.png");
    testProfiles(folder.path(), prefix.isEmpty() ? QString() : prefix + "profile.png");

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
    QAction* sound = actionNamed(window, "Sound");
    QAction* memory = actionNamed(window, "Memory");
    CHECK(general && general->isEnabled());
    CHECK(display && display->isEnabled());
    CHECK(sound && sound->isEnabled());
    CHECK(memory && memory->isEnabled());
    QAction* input = actionNamed(window, "Input");
    QAction* other = actionNamed(window, "Other");
    CHECK(input && input->isEnabled());
    CHECK(other && other->isEnabled());
    if (!prefix.isEmpty()) {
        SetupDialog picture((Settings()));
        picture.showPage(SetupDialog::Other);
        picture.show();
        QTest::qWait(50);
        picture.grab().save(prefix + "other.png");
    }
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

    // The Display settings reach the picture and the window.
    testDisplayPage(emulator.frame(), prefix.isEmpty() ? QString() : prefix + "display.png");
    settings = window.settings();
    CHECK(window.menuBar()->isVisibleTo(&window));
    CHECK(window.screen()->sizeHint() == QSize(768, 540));
    CHECK(window.screen()->renderBothLines());
    CHECK(window.screen()->cursor().shape() == Qt::ArrowCursor);  // the pointer stays over the picture
    settings.monitorType = 1;
    settings.windowed.halfSize = true;
    settings.windowed.renderBothLines = false;
    settings.windowed.hideMenus = true;
    settings.windowed.hideMouse = true;
    settings.windowed.noRightClick = true;
    window.applySettings(settings);
    CHECK(!window.menuBar()->isVisibleTo(&window));
    CHECK(window.screen()->sizeHint() == QSize(384, 270));
    CHECK(!window.screen()->renderBothLines());
    CHECK(window.screen()->cursor().shape() == Qt::BlankCursor);
    CHECK(window.screen()->contextMenuPolicy() == Qt::NoContextMenu);
    // A green monitor: the pictures that follow have no red and no blue.
    emulator.autoType(QStringLiteral("PRINT 1\n"));
    CHECK(QTest::qWaitFor(
        [&] {
            const QImage image = emulator.frame();
            for (int y = 60; y < 200; y += 7)
                for (int x = 100; x < 600; x += 7)
                    if (qRed(image.pixel(x, y)) != 0 || qBlue(image.pixel(x, y)) != 0)
                        return false;
            return true;
        },
        15000));
    settings.windowed = Settings::WindowOptions();
    settings.monitorType = 0;
    window.applySettings(settings);
    CHECK(window.menuBar()->isVisibleTo(&window));
    CHECK(window.screen()->sizeHint() == QSize(768, 540));

    // The Sound settings reach the machine: no sound made when none is
    // wanted, mono and the volume when it is.
    CHECK(emulator.soundOn());
    settings = window.settings();
    settings.soundOn = false;
    window.applySettings(settings);
    CHECK(!emulator.soundOn());
    CHECK(!emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.audio().enabled(); }));

    // The joystick: the host's stick and buttons hold the CPC's joystick
    // lines, together with whatever the keyboard holds.
    using tuxape::CpcKey;
    auto pressed = [&](CpcKey key) {
        return emulator.withMachine([key](tuxape::Cpc& cpc) { return cpc.keyboard().pressed(key); });
    };
    auto stick = [&](unsigned bits) { emulator.withMachine([&](tuxape::Cpc&) { emulator.applyJoystick(bits); }); };
    CHECK(emulator.joystickEnabled());
    emulator.releaseAllKeys();
    stick(HostJoystick::Up | HostJoystick::Fire1);
    CHECK(pressed(CpcKey::JoyUp) && pressed(CpcKey::JoyFire2));  // the main button is the firmware's "fire 2"
    CHECK(!pressed(CpcKey::JoyDown) && !pressed(CpcKey::JoyFire1));
    emulator.pcKeyEvent(tuxape::PcNum8, false, true);  // the keypad's "up" as well
    stick(HostJoystick::Right);
    CHECK(pressed(CpcKey::JoyUp) && pressed(CpcKey::JoyRight) && !pressed(CpcKey::JoyFire2));
    emulator.pcKeyEvent(tuxape::PcNum8, false, false);
    CHECK(!pressed(CpcKey::JoyUp));
    settings = window.settings();
    settings.joystick = false;
    window.applySettings(settings);
    CHECK(!emulator.joystickEnabled());
    CHECK(!pressed(CpcKey::JoyRight));  // let go with the switch
    CHECK_EQ(HostJoystick::directions(0, 0), 0u);
    CHECK_EQ(HostJoystick::directions(-32768, 4000), static_cast<unsigned>(HostJoystick::Left));
    CHECK_EQ(HostJoystick::directions(20000, -20000), static_cast<unsigned>(HostJoystick::Right | HostJoystick::Up));
    CHECK_EQ(HostJoystick::directions(0, 32767), static_cast<unsigned>(HostJoystick::Down));

    // Another keyboard layout takes effect at once.
    tuxape::KeyMap swapped;
    swapped.setPcKey(false, CpcKey::A, 0, tuxape::PcQ);
    swapped.setPcKey(false, CpcKey::Q, 0, tuxape::PcA);
    emulator.setKeyMap(swapped);
    CHECK(emulator.keyMap() == swapped);
    emulator.pcKeyEvent(tuxape::PcQ, false, true);
    CHECK(pressed(CpcKey::A) && !pressed(CpcKey::Q));
    emulator.pcKeyEvent(tuxape::PcQ, false, false);
    emulator.setKeyMap(tuxape::KeyMap());

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
