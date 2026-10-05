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
    QDir().mkpath(QFileInfo(path).absolutePath());
    return ini.save(path.toStdString());
}
