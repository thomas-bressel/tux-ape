#pragma once

// Profiles of the tests' own, in a folder of theirs, which TuxAPE then
// offers in place of the ones it ships in profile/: the tests check what is
// done with profiles, not which ones TuxAPE comes with.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>

inline QString useTestProfiles(const QString& folder)
{
    const QString profiles = folder + "/test profiles";
    QDir().mkpath(profiles);
    const auto write = [&](const char* name, const char* text) {
        QFile file(profiles + '/' + name + ".tpf");
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(text);
    };
    write("CPC464", "[ROMS]\nLower=OS464\nUpper(0)=BASIC1-0\nUpper(7)=\nCartridge Enabled=false\n"
                    "[Configuration]\nCRTC Type=0\nEnable Plus=false\nExtended RAM=0\nSilicon Disc=false\n");
    write("CPC6128", "[ROMS]\nLower=OS6128\nUpper(0)=BASIC1-1\nUpper(7)=AMSDOS\nCartridge Enabled=false\n"
                     "[Configuration]\nCRTC Type=0\nEnable Plus=false\nExtended RAM=1\nSilicon Disc=false\n");
    write("CPC6128 with ParaDOS", "[ROMS]\nLower=OS6128\nUpper(0)=BASIC1-1\nUpper(7)=PARADOS_1-2\nCartridge Enabled=false\n"
                                  "[Configuration]\nCRTC Type=0\nEnable Plus=false\nExtended RAM=1\nSilicon Disc=false\n");
    write("464 Plus", "[ROMS]\nCartridge=CPC_PLUS.CPR\nLower=\nUpper(0)=\nUpper(7)=\nCartridge Enabled=true\n"
                      "[Configuration]\nCRTC Type=3\nEnable Plus=true\nPlus PPI=true\nExtended RAM=0\nSilicon Disc=false\n");
    write("6128 Plus", "[ROMS]\nCartridge=CPC_PLUS.CPR\nLower=\nUpper(0)=\nUpper(7)=\nCartridge Enabled=true\n"
                       "[Configuration]\nCRTC Type=3\nEnable Plus=true\nPlus PPI=true\nExtended RAM=1\nSilicon Disc=false\n");
    qputenv("TUXAPE_PROFILE_DIR", QFile::encodeName(profiles));
    return profiles;
}
