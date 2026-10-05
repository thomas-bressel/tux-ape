#pragma once

#include <functional>

#include <QKeySequence>
#include <QMainWindow>

class DiscManager;
class Emulator;
class QAction;
class QFrame;
class QLabel;
class QMenu;
class QToolButton;
class ScreenWidget;
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

    // Puts a disc image in a drive, reporting problems to the user.
    // Returns false if it could not be done.
    bool insertDiscFile(int drive, const QString& path);

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

    QAction* runAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* fullScreenAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* driveSetupAction_ = nullptr;
    QAction* swapAction_ = nullptr;
    struct DriveActions {
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
    void toggleFullScreen();
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
    void updateDiscActions();
    void updateDriveLights();
    void report(const QString& error);
};
