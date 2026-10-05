#pragma once

#include <functional>

#include <QKeySequence>
#include <QMainWindow>

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

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    Emulator* emulator_;
    ScreenWidget* screen_ = nullptr;
    QWidget* controlPanel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QFrame* driveLed_[2] = {};

    QAction* runAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* fullScreenAction_ = nullptr;
    QAction* pasteAction_ = nullptr;

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
};
