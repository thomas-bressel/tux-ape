#include "settings.h"

#include <algorithm>
#include <cstdlib>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "core/files.h"
#include "core/inifile.h"
#include "core/keymap.h"

namespace {

QString g_file;

constexpr const char* kConfiguration = "Configuration";
constexpr const char* kDrives = "Drives";
constexpr const char* kRoms = "ROMS";

std::string upperKey(int slot)
{
    return "Upper(" + std::to_string(slot) + ")";
}

// The window's options have the same key names for full screen, with "FS"
// after them.
void readWindowOptions(const tuxape::IniFile& ini, const std::string& suffix, Settings::WindowOptions& options)
{
    options.halfSize = ini.getBool(kConfiguration, "Half Size" + suffix, options.halfSize);
    options.renderBothLines = ini.getBool(kConfiguration, "Render Both" + suffix, options.renderBothLines);
    options.hideMouse = ini.getBool(kConfiguration, "Hide Mouse" + suffix, options.hideMouse);
    options.hidePanel = ini.getBool(kConfiguration, "Hide Panel" + suffix, options.hidePanel);
    options.hideMenus = ini.getBool(kConfiguration, "Hide Menus" + suffix, options.hideMenus);
    options.noRightClick = ini.getBool(kConfiguration, "No Right Click" + suffix, options.noRightClick);
}

void writeWindowOptions(tuxape::IniFile& ini, const std::string& suffix, const Settings::WindowOptions& options)
{
    ini.setBool(kConfiguration, "Half Size" + suffix, options.halfSize);
    ini.setBool(kConfiguration, "Render Both" + suffix, options.renderBothLines);
    ini.setBool(kConfiguration, "Hide Mouse" + suffix, options.hideMouse);
    ini.setBool(kConfiguration, "Hide Panel" + suffix, options.hidePanel);
    ini.setBool(kConfiguration, "Hide Menus" + suffix, options.hideMenus);
    ini.setBool(kConfiguration, "No Right Click" + suffix, options.noRightClick);
}

}  // namespace

QString Settings::file()
{
    if (!g_file.isEmpty())
        return g_file;
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/TuxAPE/TuxAPE.ini";
}

void Settings::setFile(const QString& path)
{
    g_file = path;
}

void Settings::load()
{
    *this = Settings();
    tuxape::IniFile ini;
    if (ini.load(file().toStdString()))
        read(ini);
}

bool Settings::save() const
{
    const QString path = file();
    // What the file holds that this version does not know about is kept.
    tuxape::IniFile ini;
    ini.load(path.toStdString());
    write(ini);
    QDir().mkpath(QFileInfo(path).absolutePath());
    return ini.save(path.toStdString());
}

void Settings::read(const tuxape::IniFile& ini)
{
    crtcType = std::clamp(ini.getInt(kConfiguration, "CRTC Type", crtcType), 0, 4);
    speedPercent = std::clamp(ini.getInt(kConfiguration, "Emulation Speed", speedPercent), 5, 1000);
    // As in WinAPE.ini: the number of frames, or 0 for "show every frame at
    // the chosen speed".
    if (ini.has(kConfiguration, "Frame Skip")) {
        const int skip = ini.getInt(kConfiguration, "Frame Skip", 0);
        displayEvery = skip > 0;
        if (displayEvery)
            displayEveryFrames = std::min(skip, 50);
    }
    fastDisc = ini.getBool(kDrives, "Fast Disc", fastDisc);

    // "Extended RAM" counts the choices of the Memory page from the top.
    machine.ram = static_cast<tuxape::RamExpansion>(
        std::clamp(ini.getInt(kConfiguration, "Extended RAM", static_cast<int>(machine.ram)), 0, 3));
    machine.siliconDisc = ini.getBool(kConfiguration, "Silicon Disc", machine.siliconDisc);
    // A file that names the firmware ROM, even to say there is none, names
    // them all: a slot it leaves out is empty.
    if (ini.has(kRoms, "Lower")) {
        machine.lowerRom = ini.get(kRoms, "Lower");
        for (int slot = 0; slot < tuxape::Memory::kRomSlots; ++slot)
            machine.upperRoms[static_cast<size_t>(slot)] = ini.get(kRoms, upperKey(slot));
    }
    if (ini.has(kRoms, "Cartridge"))
        machine.cartridge = ini.get(kRoms, "Cartridge");
    machine.cartridgeEnabled = ini.getBool(kRoms, "Cartridge Enabled", machine.cartridgeEnabled);
    machine.plus = ini.getBool(kConfiguration, "Enable Plus", machine.plus);
    machine.rom32 = ini.getBool(kRoms, "Enable 32 ROMs", machine.rom32);
    machine.disableAllRoms = ini.getBool(kRoms, "Disable All", machine.disableAllRoms);
    machine.onlyLower0And7 = ini.getBool(kRoms, "Enable L07", machine.onlyLower0And7);

    monitorType = std::clamp(ini.getInt(kConfiguration, "Monitor Type", monitorType), 0, 2);
    brightness = std::clamp(ini.getInt(kConfiguration, "Monitor Brightness", brightness), -100, 100);
    verticalHold = std::clamp(ini.getInt(kConfiguration, "VHOLD Position", verticalHold), -32, 32);
    linearPalette = ini.getBool(kConfiguration, "Linear Palette", linearPalette);
    palEmulation = ini.getBool(kConfiguration, "PAL Emulation", palEmulation);
    driveLed = ini.getBool(kConfiguration, "On Screen Drive LED", driveLed);
    showDriveCylinders = ini.getBool(kConfiguration, "Show Drive Cylinders", showDriveCylinders);
    readWindowOptions(ini, "", windowed);
    readWindowOptions(ini, " FS", fullScreen);

    // WinAPE's "Sound Mode": 0 none, 1 the PC speaker (there is none here:
    // silence), 2 the sound card.
    soundOn = ini.getInt(kConfiguration, "Sound Mode", soundOn ? 2 : 0) == 2;
    soundRate = ini.getInt(kConfiguration, "Sound Rate", soundRate) < 33000 ? 22050 : 44100;
    sound16Bit = ini.getInt(kConfiguration, "Sound Bits", sound16Bit ? 16 : 8) > 8;
    soundStereo = ini.getBool(kConfiguration, "Sound Stereo", soundStereo);
    soundVolume = std::clamp(ini.getInt(kConfiguration, "Sound Volume", soundVolume), 0, 15);
    tapeSounds = ini.getBool(kConfiguration, "Tape Sound", tapeSounds);
    // Written like WinAPE's "0.4": frames, with one decimal.
    if (ini.has(kConfiguration, "Sound Frame Delay")) {
        // Read by hand: the C library would want a comma where the user's
        // language has one.
        const std::string delay = ini.get(kConfiguration, "Sound Frame Delay");
        const size_t point = delay.find('.');
        const int whole = std::atoi(delay.substr(0, point).c_str());
        const int tenth = point != std::string::npos && point + 1 < delay.size() && delay[point + 1] >= '0'
                              && delay[point + 1] <= '9'
                              ? delay[point + 1] - '0'
                              : 0;
        soundBufferSync = std::clamp(whole * 10 + tenth, 0, 20);
    }

    joystick = ini.getBool(kConfiguration, "Joystick Enabled", joystick);
    if (ini.has(kConfiguration, "Keyboard File"))
        keyboardFile = QString::fromStdString(ini.get(kConfiguration, "Keyboard File"));

    screenshotHalfSize = ini.getBool("Screenshots", "Half Size", screenshotHalfSize);
    screenshotHalfHeight = ini.getBool("Screenshots", "Half Height", screenshotHalfHeight);
    if (ini.has("Screenshots", "Path"))
        screenshotFolder = QString::fromStdString(ini.get("Screenshots", "Path"));

    if (ini.has("Assembler", "Library Path"))
        assemblerLibraryPath = QString::fromStdString(ini.get("Assembler", "Library Path"));
    assemblerPushPc = ini.getBool("Assembler", "Push PC On Run", assemblerPushPc);
    assemblerHideOutput = ini.getBool("Assembler", "Hide Output", assemblerHideOutput);

    if (!ini.keys("Library").empty()) {
        libraryFolders.clear();
        for (int n = 1; ini.has("Library", "Folder" + std::to_string(n)); ++n)
            libraryFolders << QString::fromStdString(ini.get("Library", "Folder" + std::to_string(n)));
    }
}

void Settings::write(tuxape::IniFile& ini, unsigned parts) const
{
    if (parts & CrtcPart)
        ini.setInt(kConfiguration, "CRTC Type", crtcType);
    if (parts & SpeedPart) {
        ini.setInt(kConfiguration, "Emulation Speed", speedPercent);
        ini.setInt(kConfiguration, "Frame Skip", displayEvery ? displayEveryFrames : 0);
    }
    if (parts & FastDiscPart)
        ini.setBool(kDrives, "Fast Disc", fastDisc);
    if (parts & RamPart) {
        ini.setInt(kConfiguration, "Extended RAM", static_cast<int>(machine.ram));
        ini.setBool(kConfiguration, "Silicon Disc", machine.siliconDisc);
    }
    if (parts & RomsPart) {
        ini.set(kRoms, "Lower", machine.lowerRom);
        for (int slot = 0; slot < tuxape::Memory::kRomSlots; ++slot)
            ini.set(kRoms, upperKey(slot), machine.upperRoms[static_cast<size_t>(slot)]);
        ini.set(kRoms, "Cartridge", machine.cartridge);
        ini.setBool(kRoms, "Cartridge Enabled", machine.cartridgeEnabled);
        ini.setBool(kConfiguration, "Enable Plus", machine.plus);
        ini.setBool(kRoms, "Enable 32 ROMs", machine.rom32);
        ini.setBool(kRoms, "Disable All", machine.disableAllRoms);
        ini.setBool(kRoms, "Enable L07", machine.onlyLower0And7);
    }
    if (parts & MonitorPart) {
        ini.setInt(kConfiguration, "Monitor Type", monitorType);
        ini.setInt(kConfiguration, "Monitor Brightness", brightness);
        ini.setInt(kConfiguration, "VHOLD Position", verticalHold);
        ini.setBool(kConfiguration, "Linear Palette", linearPalette);
        ini.setBool(kConfiguration, "PAL Emulation", palEmulation);
        ini.setBool(kConfiguration, "On Screen Drive LED", driveLed);
        ini.setBool(kConfiguration, "Show Drive Cylinders", showDriveCylinders);
    }
    if (parts & WindowedPart)
        writeWindowOptions(ini, "", windowed);
    if (parts & FullScreenPart)
        writeWindowOptions(ini, " FS", fullScreen);
    if (parts & SoundPart) {
        ini.setInt(kConfiguration, "Sound Mode", soundOn ? 2 : 0);
        ini.setInt(kConfiguration, "Sound Rate", soundRate);
        ini.setInt(kConfiguration, "Sound Bits", sound16Bit ? 16 : 8);
        ini.setBool(kConfiguration, "Sound Stereo", soundStereo);
        ini.setInt(kConfiguration, "Sound Volume", soundVolume);
        ini.setBool(kConfiguration, "Tape Sound", tapeSounds);
        ini.set(kConfiguration, "Sound Frame Delay",
                std::to_string(soundBufferSync / 10) + "." + std::to_string(soundBufferSync % 10));
    }
    if (parts & InputPart) {
        ini.setBool(kConfiguration, "Joystick Enabled", joystick);
        ini.set(kConfiguration, "Keyboard File", keyboardFile.toStdString());
    }
    // Not part of any profile: only the settings file itself has these.
    if (parts == AllParts) {
        ini.setBool("Screenshots", "Half Size", screenshotHalfSize);
        ini.setBool("Screenshots", "Half Height", screenshotHalfHeight);
        ini.set("Screenshots", "Path", screenshotFolder.toStdString());
        ini.set("Assembler", "Library Path", assemblerLibraryPath.toStdString());
        ini.setBool("Assembler", "Push PC On Run", assemblerPushPc);
        ini.setBool("Assembler", "Hide Output", assemblerHideOutput);
        for (const std::string& key : ini.keys("Library"))
            ini.remove("Library", key);
        ini.setInt("Library", "Folders", static_cast<int>(libraryFolders.size()));
        for (qsizetype n = 0; n < libraryFolders.size(); ++n)
            ini.set("Library", "Folder" + std::to_string(n + 1), libraryFolders[n].toStdString());
    }
}

bool Settings::loadProfile(const QString& path)
{
    tuxape::IniFile ini;
    if (!ini.load(path.toStdString()))
        return false;
    read(ini);
    return true;
}

bool Settings::saveProfile(const QString& path, unsigned parts) const
{
    tuxape::IniFile ini;
    write(ini, parts);
    return ini.save(path.toStdString());
}

bool Settings::profileUsable(const QString& path)
{
    tuxape::IniFile ini;
    return ini.load(path.toStdString());
}

QString Settings::profileFolder()
{
    return QString::fromStdString(tuxape::defaultProfileDir().string());
}

QString Settings::userProfileFolder()
{
    return QFileInfo(file()).absolutePath() + "/Profile";
}

QString Settings::keyMapFile()
{
    return QFileInfo(file()).absolutePath() + "/TuxAPE.kbd";
}

bool Settings::loadKeyMap(tuxape::KeyMap& map)
{
    const auto data = tuxape::readFile(keyMapFile().toStdString());
    return data && map.load(*data);
}

bool Settings::saveKeyMap(const tuxape::KeyMap& map)
{
    QDir().mkpath(QFileInfo(keyMapFile()).absolutePath());
    return tuxape::writeFile(keyMapFile().toStdString(), map.save());
}
