#include "settings.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "core/inifile.h"

namespace {

QString g_file;

constexpr const char* kConfiguration = "Configuration";
constexpr const char* kDrives = "Drives";
constexpr const char* kRoms = "ROMS";

std::string upperKey(int slot)
{
    return "Upper(" + std::to_string(slot) + ")";
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
    if (!ini.load(file().toStdString()))
        return;
    crtcType = std::clamp(ini.getInt(kConfiguration, "CRTC Type", crtcType), 0, 4);
    speedPercent = std::clamp(ini.getInt(kConfiguration, "Emulation Speed", speedPercent), 5, 1000);
    // As in WinAPE.ini: the number of frames, or 0 for "show every frame at
    // the chosen speed".
    const int skip = ini.getInt(kConfiguration, "Frame Skip", 0);
    displayEvery = skip > 0;
    if (displayEvery)
        displayEveryFrames = std::min(skip, 50);
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
    machine.rom32 = ini.getBool(kRoms, "Enable 32 ROMs", machine.rom32);
    machine.disableAllRoms = ini.getBool(kRoms, "Disable All", machine.disableAllRoms);
    machine.onlyLower0And7 = ini.getBool(kRoms, "Enable L07", machine.onlyLower0And7);
}

bool Settings::save() const
{
    const QString path = file();
    // What the file holds that this version does not know about is kept.
    tuxape::IniFile ini;
    ini.load(path.toStdString());
    ini.setInt(kConfiguration, "CRTC Type", crtcType);
    ini.setInt(kConfiguration, "Emulation Speed", speedPercent);
    ini.setInt(kConfiguration, "Frame Skip", displayEvery ? displayEveryFrames : 0);
    ini.setBool(kDrives, "Fast Disc", fastDisc);
    ini.setInt(kConfiguration, "Extended RAM", static_cast<int>(machine.ram));
    ini.setBool(kConfiguration, "Silicon Disc", machine.siliconDisc);
    ini.set(kRoms, "Lower", machine.lowerRom);
    for (int slot = 0; slot < tuxape::Memory::kRomSlots; ++slot)
        ini.set(kRoms, upperKey(slot), machine.upperRoms[static_cast<size_t>(slot)]);
    ini.setBool(kRoms, "Enable 32 ROMs", machine.rom32);
    ini.setBool(kRoms, "Disable All", machine.disableAllRoms);
    ini.setBool(kRoms, "Enable L07", machine.onlyLower0And7);
    QDir().mkpath(QFileInfo(path).absolutePath());
    return ini.save(path.toStdString());
}
