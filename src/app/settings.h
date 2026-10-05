#pragma once

#include <QString>

#include "core/setup.h"

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

    bool operator==(const Settings&) const = default;

    // The file read and written: TuxAPE.ini in the user's configuration
    // folder, unless a test has pointed it elsewhere.
    static QString file();
    static void setFile(const QString& path);
};
