#pragma once

#include <functional>
#include <span>

#include <QKeySequence>
#include <QMainWindow>

#include "settings.h"

class DiscManager;
class Emulator;
class QAction;
class QFrame;
class QLabel;
class QMenu;
class QToolButton;
class AssemblerDialog;
class BreakpointsDialog;
class TimersDialog;
class DebuggerDialog;
class RegistersDialog;
class ScreenWidget;
class TapeDialog;
enum class IconId;

// The emulator window, laid out like WinAPE's: the menus, the emulated
// screen, and the control panel with its buttons, drive lights and timing
// read-out along the bottom.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Emulator* emulator, QWidget* parent = nullptr);

    ScreenWidget* screen() const { return screen_; }
    DiscManager* discs() const { return discs_; }
    // The debugger's window, once the machine has been paused; else null.
    DebuggerDialog* debugger() const { return debugger_; }
    AssemblerDialog* assembler() const { return assembler_; }
    RegistersDialog* registers() const { return registers_; }
    BreakpointsDialog* breakpoints() const { return breakpoints_; }
    TimersDialog* timers() const { return timers_; }

    // Puts a disc image in a drive, reporting problems to the user.
    // Returns false if it could not be done.
    bool insertDiscFile(int drive, const QString& path);
    // Reads or writes a snapshot of the machine, reporting problems to the
    // user. Return false if it could not be done.
    bool loadSnapshotFile(const QString& path);
    bool saveSnapshotFile(const QString& path);
    // Puts a tape (a CDT file) in the deck, rewound and with Play pressed:
    // it runs when the CPC starts the motor.
    bool insertTapeFile(const QString& path);
    // Records a session to a file, from the machine's present state or
    // from a cold reset, until stopSessionRecording(); and plays one back.
    // Problems are reported to the user.
    void startSessionRecording(const QString& path, bool fromColdReset);
    bool stopSessionRecording();
    bool playSessionFile(const QString& path);
    // Plugs a cartridge (a CPR file) in. The machine becomes a Plus if it
    // was not one, and starts afresh, as one does when its cartridge is
    // changed.
    bool insertCartridgeFile(const QString& path);

    // Puts the user's settings into effect. The window starts with the
    // defaults; it is for the application to load the saved ones.
    void applySettings(const Settings& settings);
    const Settings& settings() const { return settings_; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    Emulator* emulator_;
    DiscManager* discs_;
    ScreenWidget* screen_ = nullptr;
    QWidget* controlPanel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QFrame* driveLed_[2] = {};
    int litDrive_ = -1;
    QString discFolder_;  // where the last disc image was opened
    QString snapshotFolder_;  // where the last snapshot was read or written
    QString snapshotPath_;    // the file "Update Snapshot" writes to
    Settings settings_;
    QString autoTypeText_;  // what the Auto-Type window held last

    QAction* runAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* stepAction_ = nullptr;
    QAction* stepOverAction_ = nullptr;
    DebuggerDialog* debugger_ = nullptr;
    AssemblerDialog* assembler_ = nullptr;
    RegistersDialog* registers_ = nullptr;
    BreakpointsDialog* breakpoints_ = nullptr;
    TimersDialog* timers_ = nullptr;
    QAction* fullScreenAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* driveSetupAction_ = nullptr;
    QAction* swapAction_ = nullptr;
    QAction* loadSnapshotAction_ = nullptr;
    QAction* saveSnapshotAction_ = nullptr;
    QAction* updateSnapshotAction_ = nullptr;
    QAction* setupAction_ = nullptr;
    QAction* libraryAction_ = nullptr;
    QAction* assemblerAction_ = nullptr;
    QAction* registersAction_ = nullptr;
    QAction* cartridgeAction_ = nullptr;
    QAction* recordWavAction_ = nullptr;
    QAction* recordYmAction_ = nullptr;
    QAction* recordAviAction_ = nullptr;
    QAction* recordSessionAction_ = nullptr;
    QAction* playSessionAction_ = nullptr;
    QString sessionPath_;  // the file the session being recorded goes to
    QString recordingFolder_;
    QAction* tapeControlAction_ = nullptr;
    QAction* rewindTapeAction_ = nullptr;
    QAction* removeTapeAction_ = nullptr;
    QAction* playTapeAction_ = nullptr;
    TapeDialog* tapeDialog_ = nullptr;
    QString tapePath_;    // the file the tape in the deck came from
    QString tapeFolder_;  // where the last tape image was opened
    QString cartridgeFolder_;
    QString librarySearch_;  // what the Library window was last searching for
    struct DriveActions {
        QAction* edit = nullptr;
        QAction* format = nullptr;
        QAction* flip = nullptr;
        QAction* remove = nullptr;
    } driveActions_[2];

    // Adds a menu entry. Entries given no handler stand for WinAPE functions
    // that are not written yet; they show greyed out.
    QAction* addItem(QMenu* menu, const QString& text, const QKeySequence& shortcut = {},
                     std::function<void()> handler = nullptr);
    QToolButton* addButton(IconId icon, const QString& hint, QAction* action);

    void createMenus();
    void createControlPanel();
    void showContextMenu(const QPoint& pos);

    void setPaused(bool paused);
    // The machine paused by itself, on a breakpoint or at the end of a step.
    void machineStopped();
    void showDebugger();
    void showAssembler();
    void showRegisters();
    void showBreakpoints();
    void showDataAreas();
    void showTimers();
    void updateDebugActions();
    // Shows the Setup window on one of its pages (SetupDialog::Page).
    void showSetup(int page);
    void setSpeed(int percent);
    void toggleFullScreen();
    // What surrounds the picture and how it is drawn, from the settings
    // for the window or for full screen, whichever it is now.
    void applyWindowOptions();
    void paste();
    void updateStats(int speedPercent, int framesPerSecond);

    // Disc drives.
    void chooseDisc(int drive);
    void newBlankDisc(int drive);
    void formatDisc(int drive);
    void removeDisc(int drive);
    void flipDisc(int drive);
    void swapDiscs();
    void driveSetup();
    // Deals with unsaved changes before a disc leaves its drive. Returns
    // false if the user chose to cancel.
    bool saveBeforeLeaving(int drive);
    // Snapshots.
    void chooseSnapshot();
    void saveSnapshotAs();
    // Screenshots and Auto-Type.
    void saveScreenshot();
    void autoType();
    void showLibrary();
    void chooseCartridge();
    void editDisc(int drive);
    // File > Record WAV and Record YM: a file is asked for and the
    // recording starts; chosen again, the entry ends it.
    void toggleWavRecording();
    void toggleYmRecording();
    void toggleAviRecording();
    // File > Record Session and Playback Session, each ended by choosing
    // its entry again.
    void toggleSessionRecording();
    void toggleSessionPlayback();
    // The cassette deck.
    void chooseTape();
    void removeTape();
    void showTapeControl();
    void updateTapeActions();
    // A snapshot or a tape given as what its file holds; `name` is what the
    // user is told it is.
    bool loadSnapshotData(std::span<const uint8_t> data, const QString& name);
    bool insertTapeData(std::span<const uint8_t> data, const QString& name);
    void updateDiscActions();
    void updateDriveLights();
    void report(const QString& error);
};
