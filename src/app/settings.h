#pragma once

#include <QString>
#include <QStringList>

#include "core/setup.h"
#include "crtlook.h"

namespace tuxape {
class IniFile;
class KeyMap;
}

// What the user chose in the Setup window, kept from one run to the next in
// TuxAPE.ini. The file has the format of WinAPE.ini and uses its section and
// key names wherever WinAPE has the same setting.
struct Settings {
    int crtcType = 0;             // 0 to 4, numbered as in WinAPE
    bool fastDisc = false;
    bool fourDrives = false;      // drives C: and D: as well
    int speedPercent = 100;       // 5 to 1000
    bool turbo = false;           // every instruction in a microsecond
    bool plusPpi = false;         // the PPI as a Plus has it, on any machine
    bool displayEvery = false;    // run flat out and show one picture in...
    int displayEveryFrames = 50;  // ...this many (1 to 50)
    // RAM and ROMs: a CPC6128 until the user says otherwise.
    tuxape::MachineConfig machine = tuxape::stockMachine(tuxape::CpcModel::Cpc6128);

    // The Display page. The defaults are WinAPE's.
    int monitorType = 0;        // 0 colour, 1 green, 2 greyscale
    int brightness = 0;         // -100 to 100
    int verticalHold = 0;       // lines the picture is moved by, -32 to 32
    bool linearPalette = true;
    bool palEmulation = false;        // pixels run into their neighbours, every other line dimmed
    bool crtShader = true;            // the picture as a CTM644 shows it, drawn by the graphics card
    CrtLook crtLook;                  // and how that is set
    bool displaySync = true;          // a frame to each picture of a screen that shows fifty a second
    bool driveLed = false;            // a light on the picture while a drive is at work
    bool showDriveCylinders = false;  // with the cylinder its head is on
    // What the window shows around the picture, and how the picture is
    // drawn: one set of choices for the window, one for full screen.
    struct WindowOptions {
        bool halfSize = false;
        bool renderBothLines = true;
        bool hideMouse = false;  // WinAPE hides it; here the pointer stays unless asked
        bool hidePanel = false;
        bool hideMenus = false;
        bool noRightClick = false;
        bool operator==(const WindowOptions&) const = default;
    };
    WindowOptions windowed;
    WindowOptions fullScreen{false, true, false, true, true, false};

    // The Sound page.
    bool soundOn = true;        // WinAPE's "DirectSound", as against "None"
    int soundRate = 44100;      // or 22050
    bool sound16Bit = true;
    bool soundStereo = true;
    int soundVolume = 15;       // 0 to 15
    bool tapeSounds = false;    // the tape is heard while it loads
    bool amDrum = false;        // the AmDrum sound converter, on ports &FFxx

    // The Other page: what is on the printer's port.
    enum PrinterMode { PrinterDisabled, PrinterDigiblaster, PrinterHost, PrinterFile, PrinterAssembler };
    int printerMode = PrinterDisabled;
    QString printerFile;        // where printing goes, with PrinterFile
    int soundBufferSync = 0;    // tenths of a frame of extra sound kept in hand, 0 to 20

    // The Input page. The keyboard layout itself is kept in a file of its
    // own, in WinAPE's .kbd format.
    bool joystick = true;       // the host's joystick or game pad is the CPC's
    bool amxMouse = false;      // the host's mouse is an AMX mouse on the joystick's port
    QString keyboardFile;       // the layout last loaded or saved, for the record

    // Screenshots: the choices of the Save Screenshot window and where the
    // last one went.
    bool screenshotHalfSize = false;
    bool screenshotHalfHeight = false;
    QString screenshotFolder;

    // The folders the Library window finds its programs in.
    QStringList libraryFolders;
    // And whether it shows them as their pictures, not as a list.
    bool libraryThumbnailView = false;
    int libraryThumbnailSize = 360;  // the side of a picture's box there

    // The assembler's options: where files named by read and incbin are
    // looked for (folders between semicolons), whether Run pushes the
    // program counter first, and whether the output window stays away
    // when all went well.
    QString assemblerLibraryPath;
    bool assemblerPushPc = true;
    bool assemblerHideOutput = false;

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
        MonitorPart = 32,
        WindowedPart = 64,
        FullScreenPart = 128,
        SoundPart = 256,
        InputPart = 512,
        AllParts = 1023,
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

    // The keyboard layout in use, kept beside the settings file. Loading
    // leaves the layout alone if there is no such file yet.
    static QString keyMapFile();
    static bool loadKeyMap(tuxape::KeyMap& map);
    static bool saveKeyMap(const tuxape::KeyMap& map);

    bool operator==(const Settings&) const = default;

    // The file read and written: TuxAPE.ini in the user's configuration
    // folder, unless a test has pointed it elsewhere.
    static QString file();
    static void setFile(const QString& path);
};
