#pragma once

#include <QByteArray>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>

class QAction;
class QObject;
class QWidget;

// What a plugin and TuxAPE know of each other. A plugin is a shared library
// in a folder of its own inside the plugins folder, beside a plugin.json
// that says what it is. It is built against this file alone and needs no
// other part of TuxAPE: all it asks of the emulator goes through PluginHost.

namespace tuxape {

// The version of what this file declares. A plugin's plugin.json names the
// version it was built against: one made for a later version is not loaded;
// one made for an earlier version is, back to the oldest this one still
// stands for. A version adds to PluginHost at its end and takes nothing
// away, which is what lets a plugin built against an earlier one work.
//
//   1   the Plugins menu, the control panel, discs, snapshots, the keyboard,
//       the machine's memory and screen, the assembler
//   2   the Find Graphics window: an entry of its tiles' menu, and the
//       tiles selected there
//   3   the discs in the drives: their files, and files put on them; the
//       other plugins loaded, for what they offer one another
constexpr int kPluginApi = 3;
constexpr int kOldestPluginApi = 1;

// The structures that follow go between TuxAPE and a plugin as they are,
// by value: once a version has one, it never changes. No field is added to
// it, moved or taken away, nor does one change its type: a plugin built
// against it would read the fields where they no longer are, and nothing
// would tell. A version that has more to say adds a structure of its own,
// and the functions of PluginHost that give it. TuxAPE's test plugin_abi
// checks that their layout is still the one plugins were built with.

// The picture the machine is showing, as a program has to know it to read
// it out of memory.
struct PluginScreen {
    int mode = 1;            // 0 to 2
    QList<int> inks;         // hardware colours (0 to 31) of pens 0 to 15, then the border's
    QList<int> plusColours;  // on a Plus with its ASIC unlocked: the same 17 as 12-bit GRB; else empty
    int address = 0xC000;    // where the picture starts in memory
    // The CRTC's own start (registers 12 and 13), in characters. Its bits 10
    // and 11 are not in `address`, and say how far the picture goes before
    // it runs on into the next 16K: when both are set, after 32K, which is
    // how an overscan picture is laid out.
    int crtcStart = 0x3000;
    int columns = 40;        // characters across, two bytes each
    int rows = 25;           // rows of characters
    int rowHeight = 8;       // lines in a row
};

// The tiles selected in the Find Graphics window, as the machine's memory
// has them.
struct PluginTiles {
    QByteArray bytes;  // tile after tile, each line after line; empty if none is selected
    int count = 0;
    int width = 0;     // bytes across
    int height = 0;    // lines
    int address = 0;   // where the first starts in memory
    int mode = 1;      // 0 to 2, or 3 for a Plus's sprites, a byte to a pixel
};

// The disc in one of the machine's drives, as its directory has it.
struct PluginDisc {
    bool present = false;   // there is a disc in the drive
    // Files can be put on it: it is in one of the formats AMSDOS's files go
    // on, and it is not one that cannot be written to.
    bool writable = false;
    QString name;           // the name of the image file it came from, if it came from one
    QStringList files;      // the files of user 0, "NAME.EXT", in the order of the directory
    int freeBytes = 0;
};

// What TuxAPE does for a plugin.
class PluginHost {
public:
    // The emulator's window, for a plugin's own windows to belong to.
    virtual QWidget* window() = 0;

    // An entry of the Plugins menu, with its key if it has one. The entry is
    // TuxAPE's: it goes when the plugin is stopped.
    virtual QAction* addAction(const QString& text, const QKeySequence& shortcut = {}) = 0;
    // A button of the control panel that does what the entry does. Asked
    // again for the same entry, the button takes the new picture: that is
    // how a plugin has its pictures follow the look of the windows, light
    // or dark.
    virtual void addButton(QAction* action, const QIcon& icon) = 0;

    // Puts a disc image in a drive (0 for A:), or loads a snapshot. Problems
    // are reported to the user; false if it could not be done.
    virtual bool insertDisc(int drive, const QString& path) = 0;
    virtual bool loadSnapshot(const QString& path) = 0;
    // Types text on the CPC's keyboard (WinAPE's Auto-Type syntax).
    virtual void autoType(const QString& text) = 0;
    virtual void reset(bool cold) = 0;

    // The machine's RAM as the screen reads it, and what is on the screen.
    virtual QByteArray readMemory(int address, int size) = 0;
    virtual PluginScreen screen() = 0;

    // Shows a source file in the assembler's window.
    virtual void openInAssembler(const QString& path) = 0;

    // A folder of the plugin's own, to keep its settings in. It is made if
    // it is not there.
    virtual QString dataFolder() = 0;

    // Shows a source file in the assembler's window, assembles it into the
    // machine and starts it where its `run` says, as the assembler's Run
    // does. False if it could not: the assembler's window says why. The
    // program is entered with the ROMs as they happen to be, so one that
    // lies under &4000 may find the lower ROM in its place.
    virtual bool assembleAndRun(const QString& path) = 0;

    // ---- Since version 2 ----

    // An entry of the menu the Find Graphics window has for the tiles
    // selected in it, after its own. The entry is TuxAPE's: it can be
    // chosen while tiles are selected, and goes when the plugin is stopped.
    // Null from a host that has no such window.
    virtual QAction* addTilesAction(const QString& text)
    {
        Q_UNUSED(text);
        return nullptr;
    }
    // The tiles selected there now: what the entry is chosen for.
    virtual PluginTiles selectedTiles() { return {}; }

    // ---- Since version 3 ----

    // How many drives the machine has: 2, or 4 with "Enable Four Drives".
    virtual int drives() { return 2; }
    // The disc in a drive (0 for A:).
    virtual PluginDisc disc(int drive)
    {
        Q_UNUSED(drive);
        return {};
    }
    // Puts a file on the disc in a drive, as user 0, in place of the file of
    // that name if there is one. `content` is the whole file: with its
    // AMSDOS header, if it is to have one. An empty string if the file is
    // there; else why it is not, for the plugin to tell. The disc is then
    // one that has changed, saved to its file as TuxAPE saves the discs
    // AMSDOS writes to.
    virtual QString putFile(int drive, const QString& name, const QByteArray& content)
    {
        Q_UNUSED(drive);
        Q_UNUSED(name);
        Q_UNUSED(content);
        return QStringLiteral("This version of TuxAPE cannot put files on its discs.");
    }
    // Another plugin, by its id ("tuxdsk"), if it is loaded: the object its
    // library made. What it offers other plugins are the Q_INVOKABLE
    // functions of that object, called by name with QMetaObject. Null if it
    // is not loaded. It is not to be kept, but asked for each time: it goes
    // when that plugin is stopped.
    virtual QObject* plugin(const QString& id)
    {
        Q_UNUSED(id);
        return nullptr;
    }

protected:
    ~PluginHost() = default;
};

// What a plugin does for TuxAPE.
class Plugin {
public:
    virtual ~Plugin() = default;

    // Called once the library is loaded, for the plugin to add its entries.
    // False, with the reason, if it cannot work: it is then unloaded.
    virtual bool start(PluginHost* host, QString* error) = 0;
    // Called before the library is unloaded: the plugin closes its windows
    // and lets go of the host.
    virtual void stop() = 0;
};

}  // namespace tuxape

#define TuxApePlugin_iid "org.tuxape.Plugin/1"
Q_DECLARE_INTERFACE(tuxape::Plugin, TuxApePlugin_iid)
