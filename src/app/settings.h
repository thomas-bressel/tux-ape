#pragma once

#include <QString>

#include "core/setup.h"

namespace tuxape {
class IniFile;
}

// What the user chose in the Setup window, kept from one run to the next in
// TuxAPE.ini. The file has the format of WinAPE.ini and uses its section and
// key names wherever WinAPE has the same setting.
struct Settings {
    int crtcType = 0;             // 0 to 4, numbered as in WinAPE
    bool fastDisc = false;
    int speedPercent = 100;       // 5 to 1000
    bool displayEvery = false;    // run flat out and show one picture in...
    int displayEveryFrames = 50;  // ...this many (1 to 50)
    // RAM and ROMs: a CPC6128 until the user says otherwise.
    tuxape::MachineConfig machine = tuxape::stockMachine(tuxape::CpcModel::Cpc6128);

    // Values out of range are brought back into it; a missing file or key
    // leaves the default.
    void load();
    bool save() const;

    // The settings as an INI file holds them. Reading replaces what the
    // file speaks of and leaves the rest; writing can be limited to some
    // parts, which is what a profile is.
    enum Part : unsigned {
        CrtcPart = 1,
        SpeedPart = 2,
        FastDiscPart = 4,
        RamPart = 8,
        RomsPart = 16,
        AllParts = 31,
    };
    void read(const tuxape::IniFile& ini);
    void write(tuxape::IniFile& ini, unsigned parts = AllParts) const;

    // Profiles: WinAPE's .wpf files, each a part of the settings. Loading
    // one changes the settings it holds. Those that ask for a Plus machine
    // or a cartridge cannot be used yet.
    bool loadProfile(const QString& path);
    bool saveProfile(const QString& path, unsigned parts) const;
    static bool profileUsable(const QString& path);
    // Where profiles are looked for: the folder of those that come with
    // the program, and the user's own, beside the settings file.
    static QString profileFolder();
    static QString userProfileFolder();

    bool operator==(const Settings&) const = default;

    // The file read and written: TuxAPE.ini in the user's configuration
    // folder, unless a test has pointed it elsewhere.
    static QString file();
    static void setFile(const QString& path);
};
