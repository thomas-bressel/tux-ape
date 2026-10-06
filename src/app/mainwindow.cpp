#include "mainwindow.h"

#include <QTextCursor>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QMimeData>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "assemblerdialog.h"
#include "breakpointsdialog.h"
#include "debuggerdialog.h"
#include "graphicsdialog.h"
#include "registersdialog.h"
#include "discdialogs.h"
#include "disceditordialog.h"
#include "discmanager.h"
#include "emulator.h"
#include "icons.h"
#include "librarydialog.h"
#include "screenwidget.h"
#include "setupdialog.h"
#include "tapedialog.h"
#include "tooldialogs.h"

#include "core/cartridge.h"
#include "core/files.h"
#include "core/session.h"
#include "core/setup.h"
#include "core/snapshot.h"

namespace {

constexpr int kPanelHeight = 30;
constexpr int kButtonSize = 26;

const char kLedOff[] = "background-color: #500000;";
const char kLedOn[] = "background-color: #ff2020;";

QString discFilter()
{
    return MainWindow::tr("Disc files (*.dsk);;All files (*)");
}

QString snapshotFilter()
{
    return MainWindow::tr("Snapshot files (*.sna);;All files (*)");
}

QChar driveLetter(int drive)
{
    return QLatin1Char(static_cast<char>('A' + drive));
}

QFrame* separator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setFixedHeight(kButtonSize);
    return line;
}

}  // namespace

MainWindow::MainWindow(Emulator* emulator, QWidget* parent)
    : QMainWindow(parent)
    , emulator_(emulator)
    , discs_(new DiscManager(emulator, this))
{
    setWindowTitle(tr("TuxAPE"));
    setAcceptDrops(true);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    screen_ = new ScreenWidget(emulator_, central);
    screen_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(screen_, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    layout->addWidget(screen_, 1);

    createMenus();
    createControlPanel();
    layout->addWidget(controlPanel_);
    setCentralWidget(central);

    connect(emulator_, &Emulator::statsChanged, this, &MainWindow::updateStats, Qt::QueuedConnection);
    connect(emulator_, &Emulator::stopped, this, &MainWindow::machineStopped, Qt::QueuedConnection);
    connect(emulator_, &Emulator::playbackFinished, this, [this] { playSessionAction_->setChecked(false); },
            Qt::QueuedConnection);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
            [this] { pasteAction_->setEnabled(!QApplication::clipboard()->text().isEmpty()); });
    connect(discs_, &DiscManager::changed, this, &MainWindow::updateDiscActions);
    updateDiscActions();

    auto* lights = new QTimer(this);
    connect(lights, &QTimer::timeout, this, &MainWindow::updateDriveLights);
    lights->start(50);

    setPaused(false);
    screen_->setFocus();
    // Open at the picture's natural size. (Left to itself Qt caps a new
    // window at two thirds of the screen.)
    resize(sizeHint());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int drive = 0; drive < DiscManager::kDrives; ++drive) {
        if (!saveBeforeLeaving(drive)) {
            event->ignore();
            return;
        }
        report(discs_->saveSpare(drive));
    }
    if (assembler_ && !assembler_->saveBeforeLeaving()) {
        event->ignore();
        return;
    }
    emulator_->stop();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    // A dropped disc image goes into drive A:, a second one into B:. A
    // snapshot is loaded, a tape put in the deck.
    int drive = 0;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (QFileInfo(path).suffix().compare(QLatin1String("sna"), Qt::CaseInsensitive) == 0) {
            loadSnapshotFile(path);
            continue;
        }
        if (QStringList{"cdt", "tzx", "wav", "voc"}.contains(QFileInfo(path).suffix().toLower())) {
            insertTapeFile(path);
            continue;
        }
        if (QFileInfo(path).suffix().compare(QLatin1String("cpr"), Qt::CaseInsensitive) == 0) {
            insertCartridgeFile(path);
            continue;
        }
        if (drive >= DiscManager::kDrives)
            continue;
        if (saveBeforeLeaving(drive) && insertDiscFile(drive, path))
            ++drive;
    }
    event->acceptProposedAction();
}

QAction* MainWindow::addItem(QMenu* menu, const QString& text, const QKeySequence& shortcut,
                             std::function<void()> handler)
{
    QAction* action = menu->addAction(text);
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
        // Registered on the window too, so the shortcut still works when
        // the menu bar is hidden.
        addAction(action);
    }
    if (handler)
        connect(action, &QAction::triggered, this, std::move(handler));
    else
        action->setEnabled(false);
    return action;
}

void MainWindow::createMenus()
{
    using Qt::CTRL;
    using Qt::SHIFT;

    // ---- File ----
    QMenu* file = menuBar()->addMenu(tr("&File"));
    // TuxAPE's own: WinAPE has no library.
    libraryAction_ = addItem(file, tr("Li&brary..."), CTRL | Qt::Key_L, [this] { showLibrary(); });
    file->addSeparator();
    const char* driveNames[2] = {QT_TR_NOOP("Drive &A:"), QT_TR_NOOP("Drive &B:")};
    for (int drive = 0; drive < 2; ++drive) {
        const Qt::Key key = drive == 0 ? Qt::Key_F1 : Qt::Key_F2;
        QMenu* menu = file->addMenu(tr(driveNames[drive]));
        addItem(menu, tr("&Insert Disc Image..."), CTRL | key, [this, drive] { chooseDisc(drive); });
        addItem(menu, tr("&New Blank Disc..."), {}, [this, drive] { newBlankDisc(drive); });
        driveActions_[drive].format =
            addItem(menu, tr("&Format Disc Image..."), {}, [this, drive] { formatDisc(drive); });
        driveActions_[drive].edit =
            addItem(menu, tr("&Edit Disc..."), SHIFT | CTRL | key, [this, drive] { editDisc(drive); });
        driveActions_[drive].flip = addItem(menu, tr("F&lip Disc"), SHIFT | key, [this, drive] { flipDisc(drive); });
        driveActions_[drive].remove = addItem(menu, tr("&Remove Disc"), {}, [this, drive] { removeDisc(drive); });
    }
    swapAction_ = addItem(file, tr("&Swap Discs A: and B:"), SHIFT | CTRL | Qt::Key_F3, [this] { swapDiscs(); });
    driveSetupAction_ = addItem(file, tr("&Drive Setup..."), Qt::Key_F2, [this] { driveSetup(); });
    cartridgeAction_ = addItem(file, tr("Load &Cartridge..."), CTRL | Qt::Key_F3, [this] { chooseCartridge(); });
    QMenu* tape = file->addMenu(tr("&Tape"));
    tapeControlAction_ = addItem(tape, tr("&Show Tape Control"), {}, [this] { showTapeControl(); });
    addItem(tape, tr("&Insert Tape Image..."), CTRL | Qt::Key_F4, [this] { chooseTape(); });
    auto deck = [this](auto&& act) {
        return [this, act] {
            emulator_->withMachine([&](tuxape::Cpc& cpc) { act(cpc.tape(), cpc.microseconds()); });
            updateTapeActions();
        };
    };
    rewindTapeAction_ =
        addItem(tape, tr("Re&wind Tape"), {}, deck([](tuxape::TapeDeck& d, uint64_t now) { d.rewind(now); }));
    removeTapeAction_ = addItem(tape, tr("R&emove Tape"), {}, [this] { removeTape(); });
    playTapeAction_ = addItem(tape, tr("&Press Play"), {}, deck([](tuxape::TapeDeck& d, uint64_t now) { d.play(now); }));
    addItem(tape, tr("Press &Record"));
    connect(tape, &QMenu::aboutToShow, this, &MainWindow::updateTapeActions);
    updateTapeActions();
    file->addSeparator();
    loadSnapshotAction_ = addItem(file, tr("&Load Snapshot..."), Qt::Key_F5, [this] { chooseSnapshot(); });
    saveSnapshotAction_ = addItem(file, tr("Save S&napshot..."), Qt::Key_F6, [this] { saveSnapshotAs(); });
    // Writes again to the snapshot last loaded or saved.
    updateSnapshotAction_ =
        addItem(file, tr("&Update Snapshot"), CTRL | Qt::Key_F6, [this] { saveSnapshotFile(snapshotPath_); });
    updateSnapshotAction_->setEnabled(false);
    file->addSeparator();
    playSessionAction_ = addItem(file, tr("Playbac&k Session..."), {}, [this] { toggleSessionPlayback(); });
    recordSessionAction_ = addItem(file, tr("&Record Session..."), {}, [this] { toggleSessionRecording(); });
    playSessionAction_->setCheckable(true);
    recordSessionAction_->setCheckable(true);
    file->addSeparator();
    addItem(file, tr("Save Screens&hot..."), CTRL | Qt::Key_F7, [this] { saveScreenshot(); });
    recordAviAction_ = addItem(file, tr("R&ecord AVI..."), {}, [this] { toggleAviRecording(); });
    recordAviAction_->setCheckable(true);
    recordWavAction_ = addItem(file, tr("Record &WAV..."), {}, [this] { toggleWavRecording(); });
    recordYmAction_ = addItem(file, tr("Record &YM..."), {}, [this] { toggleYmRecording(); });
    // Ticked while the recording runs.
    recordWavAction_->setCheckable(true);
    recordYmAction_->setCheckable(true);
    file->addSeparator();
    addItem(file, tr("Aut&o Type..."), CTRL | Qt::Key_F5, [this] { autoType(); });
    pasteAction_ = addItem(file, tr("&Paste"), CTRL | Qt::Key_F11, [this] { paste(); });
    pasteAction_->setEnabled(!QApplication::clipboard()->text().isEmpty());
    file->addSeparator();
    addItem(file, tr("E&xit"), {}, [this] { close(); });

    // ---- Settings ----
    QMenu* settings = menuBar()->addMenu(tr("&Settings"));
    setupAction_ = addItem(settings, tr("&General"), {}, [this] { showSetup(SetupDialog::General); });
    // WinAPE opens its Setup window on F12 without saying so in the menu.
    auto* setupKey = new QAction(this);
    setupKey->setShortcut(Qt::Key_F12);
    connect(setupKey, &QAction::triggered, setupAction_, &QAction::trigger);
    addAction(setupKey);
    addItem(settings, tr("&Display"), {}, [this] { showSetup(SetupDialog::Display); });
    addItem(settings, tr("&Sound"), {}, [this] { showSetup(SetupDialog::Sound); });
    addItem(settings, tr("&Memory"), {}, [this] { showSetup(SetupDialog::Memory); });
    addItem(settings, tr("&Input"), {}, [this] { showSetup(SetupDialog::Input); });
    addItem(settings, tr("&Other"));
    settings->addSeparator();
    addItem(settings, tr("&Normal Speed (100%)"), SHIFT | Qt::Key_F3, [this] { setSpeed(100); });
    addItem(settings, tr("&High Speed (1000%)"), SHIFT | Qt::Key_F4, [this] { setSpeed(1000); });
    settings->addSeparator();
    fullScreenAction_ = addItem(settings, tr("&Full Screen"), Qt::Key_F10, [this] { toggleFullScreen(); });
    addItem(settings, tr("&Reset"), CTRL | Qt::Key_F9, [this] { emulator_->reset(false); });
    addItem(settings, tr("&Cold Reset"), SHIFT | CTRL | Qt::Key_F9, [this] { emulator_->reset(true); });
    addItem(settings, tr("&AMX Mouse"), CTRL | Qt::Key_F12);
    addItem(settings, tr("M&ultiface Stop"), Qt::Key_F11);

    // ---- Debug ----
    QMenu* debug = menuBar()->addMenu(tr("&Debug"));
    runAction_ = addItem(debug, tr("&Run"), Qt::Key_F9, [this] { setPaused(false); });
    pauseAction_ = addItem(debug, tr("&Pause"), Qt::Key_F7, [this] { setPaused(true); });
    // F8 pauses too; paused, the two keys step.
    pauseAction_->setShortcuts({QKeySequence(Qt::Key_F7), QKeySequence(Qt::Key_F8)});
    stepAction_ = new QAction(tr("Single Step"), this);
    stepAction_->setShortcut(Qt::Key_F7);
    connect(stepAction_, &QAction::triggered, this, [this] { emulator_->stepInto(); });
    stepOverAction_ = new QAction(tr("Step Over"), this);
    stepOverAction_->setShortcut(Qt::Key_F8);
    connect(stepOverAction_, &QAction::triggered, this, [this] {
        // It may set the machine running, up to the instruction after a
        // call.
        emulator_->stepOver();
        updateDebugActions();
    });
    for (QAction* action : {stepAction_, stepOverAction_}) {
        action->setEnabled(false);
        addAction(action);
    }
    registersAction_ = addItem(debug, tr("&Registers"), {}, [this] { showRegisters(); });
    addItem(debug, tr("&Breakpoints"), {}, [this] { showBreakpoints(); });
    addItem(debug, tr("&Data Areas"), {}, [this] { showDataAreas(); });
    addItem(debug, tr("&Timers"), {}, [this] { showTimers(); });
    addItem(debug, tr("&Find Graphics"), {}, [this] { showGraphics(); });

    // ---- Assembler ----
    QMenu* assembler = menuBar()->addMenu(tr("&Assembler"));
    assemblerAction_ = addItem(assembler, tr("Show &Assembler"), Qt::Key_F3, [this] { showAssembler(); });

    // ---- Help ----
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    addItem(help, tr("&Contents"), Qt::Key_F1);
}

QToolButton* MainWindow::addButton(IconId icon, const QString& hint, QAction* action)
{
    auto* button = new QToolButton(controlPanel_);
    button->setIcon(makeIcon(icon));
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setAutoRaise(true);
    button->setToolTip(hint);
    // The buttons must not take the keyboard away from the emulated machine.
    button->setFocusPolicy(Qt::NoFocus);
    if (action) {
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button, [button, action] { button->setEnabled(action->isEnabled()); });
        button->setEnabled(action->isEnabled());
    } else {
        button->setEnabled(false);
    }
    static_cast<QHBoxLayout*>(controlPanel_->layout())->addWidget(button);
    return button;
}

void MainWindow::createControlPanel()
{
    controlPanel_ = new QWidget(this);
    controlPanel_->setFixedHeight(kPanelHeight);
    auto* row = new QHBoxLayout(controlPanel_);
    row->setContentsMargins(3, 2, 6, 2);
    row->setSpacing(0);

    addButton(IconId::Run, tr("Run (F9)"), runAction_);
    addButton(IconId::Pause, tr("Pause (F7/F8)"), pauseAction_);
    addButton(IconId::SingleStep, tr("Single Step (F7)"), stepAction_);
    addButton(IconId::StepOver, tr("Step Over (F8)"), stepOverAction_);
    row->addWidget(separator(controlPanel_));
    addButton(IconId::Registers, tr("Registers"), registersAction_);
    addButton(IconId::Assembler, tr("Assembler (F3)"), assemblerAction_);
    row->addWidget(separator(controlPanel_));
    addButton(IconId::Library, tr("Library (CTRL+L)"), libraryAction_);
    addButton(IconId::Disc, tr("Change Disc (F2)"), driveSetupAction_);
    addButton(IconId::Cartridge, tr("Change Cartridge (CTRL+F3)"), cartridgeAction_);
    addButton(IconId::Tape, tr("Tape Control"), tapeControlAction_);
    addButton(IconId::LoadSnapshot, tr("Load Snapshot (F5)"), loadSnapshotAction_);
    addButton(IconId::SaveSnapshot, tr("Save Snapshot (F6)"), saveSnapshotAction_);
    row->addWidget(separator(controlPanel_));
    addButton(IconId::Settings, tr("Settings (F12)"), setupAction_);
    addButton(IconId::FullScreen, tr("Toggle Full Screen (F10)"), fullScreenAction_);
    row->addWidget(separator(controlPanel_));
    addButton(IconId::Help, tr("Help (F1)"), nullptr);

    row->addStretch(1);

    // Drive activity lights.
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    for (int drive = 0; drive < 2; ++drive) {
        auto* column = new QVBoxLayout;
        column->setContentsMargins(4, 0, 4, 0);
        column->setSpacing(0);
        auto* label = new QLabel(drive == 0 ? tr("A:") : tr("B:"), controlPanel_);
        label->setFont(small);
        label->setAlignment(Qt::AlignHCenter);
        driveLed_[drive] = new QFrame(controlPanel_);
        driveLed_[drive]->setFixedSize(18, 9);
        driveLed_[drive]->setFrameShape(QFrame::Panel);
        driveLed_[drive]->setFrameShadow(QFrame::Sunken);
        driveLed_[drive]->setAutoFillBackground(true);
        driveLed_[drive]->setStyleSheet(QLatin1String(kLedOff));
        column->addWidget(label);
        column->addWidget(driveLed_[drive], 0, Qt::AlignHCenter);
        row->addLayout(column);
    }

    statusLabel_ = new QLabel(controlPanel_);
    statusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusLabel_->setMinimumWidth(110);
    row->addWidget(statusLabel_);
}

void MainWindow::showContextMenu(const QPoint& pos)
{
    // WinAPE's right-click menu is the main menu again.
    QMenu menu(this);
    for (QAction* top : menuBar()->actions())
        menu.addAction(top);
    menu.exec(screen_->mapToGlobal(pos));
}

// Paused, the machine is the debugger's: its window comes up, as WinAPE's
// does, and goes when the machine runs again if it is set to.
void MainWindow::setPaused(bool paused)
{
    emulator_->setPaused(paused);
    updateDebugActions();
    if (paused) {
        showDebugger();
    } else if (debugger_ && debugger_->hideOnRun() && debugger_->isVisible()) {
        debugger_->hide();
        // The keyboard is the CPC's again.
        activateWindow();
        screen_->setFocus();
    }
}

void MainWindow::updateDebugActions()
{
    const bool paused = emulator_->isPaused();
    runAction_->setEnabled(paused);
    pauseAction_->setEnabled(!paused);
    stepAction_->setEnabled(paused);
    stepOverAction_->setEnabled(paused);
    if (paused)
        statusLabel_->setText(tr("Paused"));
}

void MainWindow::showRegisters()
{
    if (!registers_)
        registers_ = new RegistersDialog(emulator_, this);
    registers_->show();
    registers_->raise();
}

void MainWindow::showGraphics()
{
    if (!graphics_)
        graphics_ = new GraphicsDialog(emulator_, this);
    graphics_->show();
    graphics_->raise();
}

void MainWindow::showTimers()
{
    if (!timers_)
        timers_ = new TimersDialog(emulator_, this);
    timers_->refresh();
    timers_->show();
    timers_->raise();
}

// The areas are the debugger's, whether it is up or not.
void MainWindow::showDataAreas()
{
    if (!debugger_) {
        showDebugger();
        debugger_->hide();
    }
    debugger_->showDataAreas();
}

void MainWindow::showBreakpoints()
{
    if (!breakpoints_) {
        breakpoints_ = new BreakpointsDialog(emulator_, this);
        connect(breakpoints_, &BreakpointsDialog::changed, this, [this] {
            if (debugger_)
                debugger_->refresh();
        });
    }
    breakpoints_->refresh();
    breakpoints_->show();
    breakpoints_->raise();
}

void MainWindow::machineStopped()
{
    updateDebugActions();
    if (breakpoints_ && breakpoints_->isVisible())
        breakpoints_->refresh();
    if (timers_ && timers_->isVisible())
        timers_->refresh();
    if (graphics_ && graphics_->isVisible())
        graphics_->refresh();
    if (registers_ && registers_->isVisible())
        registers_->refresh();
    if (emulator_->isPaused())
        showDebugger();
}

void MainWindow::showDebugger()
{
    if (!debugger_) {
        debugger_ = new DebuggerDialog(emulator_, this);
        connect(debugger_, &DebuggerDialog::runRequested, this, [this] { setPaused(false); });
        connect(debugger_, &DebuggerDialog::runningOn, this, &MainWindow::updateDebugActions);
        connect(debugger_, &DebuggerDialog::breakpointsChanged, this, [this] {
            if (breakpoints_ && breakpoints_->isVisible())
                breakpoints_->refresh();
        });
        // A disassembly asked for as a source goes to a new tab of the
        // assembler.
        connect(debugger_, &DebuggerDialog::sourceProduced, this, [this](const QString& source) {
            showAssembler();
            QTextCursor cursor(assembler_->editor(assembler_->newFile())->document());
            cursor.insertText(source);
        });
    }
    debugger_->refresh();
    debugger_->show();
}

void MainWindow::showAssembler()
{
    if (!assembler_) {
        assembler_ = new AssemblerDialog(emulator_, this);
        assembler_->setOptions(settings_.assemblerLibraryPath, settings_.assemblerPushPc, settings_.assemblerHideOutput);
        connect(assembler_, &AssemblerDialog::optionsChanged, this, [this] {
            settings_.assemblerLibraryPath = assembler_->libraryPath();
            settings_.assemblerPushPc = assembler_->pushPcOnRun();
            settings_.assemblerHideOutput = assembler_->hideOutput();
            if (!settings_.save())
                report(tr("Cannot save the settings to %1.").arg(QDir::toNativeSeparators(Settings::file())));
        });
        // The program just assembled runs in the machine's own window.
        connect(assembler_, &AssemblerDialog::runRequested, this, [this] {
            setPaused(false);
            activateWindow();
            screen_->setFocus();
        });
        connect(assembler_, &AssemblerDialog::breakpointsChanged, this, [this] {
            if (breakpoints_ && breakpoints_->isVisible())
                breakpoints_->refresh();
            if (debugger_ && debugger_->isVisible())
                debugger_->refresh();
        });
    }
    assembler_->show();
    assembler_->raise();
    assembler_->activateWindow();
}

void MainWindow::applySettings(const Settings& settings)
{
    settings_ = settings;
    // RAM and ROMs change under the running machine, as in WinAPE: it is
    // for the user to reset it (CTRL+F9) when the firmware has to notice.
    // A cartridge put in, changed or taken out starts the machine afresh:
    // nothing that was running could go on.
    const tuxape::MachineConfig before = emulator_->machine();
    if (settings.machine != before) {
        const bool otherCartridge = settings.machine.isPlus() != before.isPlus()
                                    || (before.isPlus() && settings.machine.cartridge != before.cartridge);
        const QString error = emulator_->setupMachine(settings.machine, otherCartridge);
        if (!error.isEmpty())
            report(tr("%1.").arg(error.left(1).toUpper() + error.mid(1)));
    }
    emulator_->setCrtcType(static_cast<tuxape::CrtcType>(settings.crtcType));
    emulator_->setFastDisc(settings.fastDisc);
    emulator_->setSpeedPercent(settings.speedPercent);
    emulator_->setTurbo(settings.turbo);
    emulator_->setPlusPpi(settings.plusPpi);
    emulator_->setDisplayEvery(settings.displayEvery ? settings.displayEveryFrames : 0);
    emulator_->setMonitor(settings.monitorType, settings.linearPalette, settings.brightness);
    screen_->setDriveLight(settings.driveLed, settings.showDriveCylinders);
    screen_->setPalEmulation(settings.palEmulation);
    emulator_->setVerticalHold(settings.verticalHold);
    emulator_->setSound(settings.soundOn, settings.soundRate, settings.sound16Bit, settings.soundStereo,
                        settings.soundVolume, settings.soundBufferSync / 10.0);
    emulator_->setTapeSounds(settings.tapeSounds);
    emulator_->setJoystickEnabled(settings.joystick);
    applyWindowOptions();
}

void MainWindow::applyWindowOptions()
{
    const Settings::WindowOptions& options = isFullScreen() ? settings_.fullScreen : settings_.windowed;
    // With the menus hidden their shortcuts still work, and the right-click
    // menu, which is the main menu again, brings everything back.
    menuBar()->setVisible(!options.hideMenus);
    controlPanel_->setVisible(!options.hidePanel);
    screen_->setCursor(options.hideMouse ? Qt::BlankCursor : Qt::ArrowCursor);
    screen_->setContextMenuPolicy(options.noRightClick ? Qt::NoContextMenu : Qt::CustomContextMenu);
    screen_->setRenderBothLines(options.renderBothLines);
    if (screen_->halfSize() != options.halfSize) {
        screen_->setHalfSize(options.halfSize);
        // The window follows the picture's new size.
        if (!isFullScreen() && !isMaximized())
            QTimer::singleShot(0, this, [this] { adjustSize(); });
    }
}

void MainWindow::showSetup(int page)
{
    // A snapshot may have brought its own CRTC and RAM with it.
    settings_.crtcType = static_cast<int>(emulator_->crtcType());
    settings_.machine = emulator_->machine();
    SetupDialog dialog(settings_, this);
    dialog.setPreview(emulator_->frame());
    const tuxape::KeyMap keysBefore = emulator_->keyMap();
    dialog.setKeyMap(keysBefore);
    dialog.showPage(static_cast<SetupDialog::Page>(page));
    if (dialog.exec() != QDialog::Accepted)
        return;
    applySettings(dialog.settings());
    // The keyboard layout is kept as it now is, for the next run too.
    if (!(dialog.keyMap() == keysBefore)) {
        emulator_->setKeyMap(dialog.keyMap());
        if (!Settings::saveKeyMap(dialog.keyMap()))
            report(tr("Cannot save the keyboard layout to %1.").arg(QDir::toNativeSeparators(Settings::keyMapFile())));
    }
    if (!settings_.save())
        report(tr("Cannot save the settings to %1.").arg(QDir::toNativeSeparators(Settings::file())));
}

// The two speeds of the Settings menu: they set the speed of the Setup
// window and leave its "Display Every" mode.
void MainWindow::setSpeed(int percent)
{
    Settings settings = settings_;
    settings.speedPercent = percent;
    settings.displayEvery = false;
    applySettings(settings);
    settings_.save();
}

// The picture is taken when the window opens: what is saved is what was on
// screen when the user asked.
void MainWindow::saveScreenshot()
{
    ScreenshotDialog dialog(screen_->screenshot(), this);
    dialog.setHalfSize(settings_.screenshotHalfSize);
    dialog.setHalfHeight(settings_.screenshotHalfHeight);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings_.screenshotHalfSize = dialog.halfSize();
    settings_.screenshotHalfHeight = dialog.halfHeight();
    const QString folder = settings_.screenshotFolder.isEmpty() ? QDir::homePath() : settings_.screenshotFolder;
    QString path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), folder, ScreenshotDialog::fileFilter());
    if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().isEmpty())
            path += ".png";
        if (!ScreenshotDialog::save(dialog.result(), path))
            report(tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
        settings_.screenshotFolder = QFileInfo(path).absolutePath();
    }
    settings_.save();
}

// ---- sessions --------------------------------------------------------------------

void MainWindow::startSessionRecording(const QString& path, bool fromColdReset)
{
    sessionPath_ = path;
    emulator_->startSessionRecording(fromColdReset);
    recordSessionAction_->setChecked(true);
    playSessionAction_->setChecked(false);
}

bool MainWindow::stopSessionRecording()
{
    const tuxape::Session session = emulator_->stopSessionRecording();
    recordSessionAction_->setChecked(false);
    if (tuxape::writeFile(sessionPath_.toStdString(), session.serialise()))
        return true;
    report(tr("Cannot write %1.").arg(QDir::toNativeSeparators(sessionPath_)));
    return false;
}

bool MainWindow::playSessionFile(const QString& path)
{
    const auto data = tuxape::readFile(path.toStdString());
    const auto session = data ? tuxape::Session::parse(*data) : std::nullopt;
    QString error = tr("it is not a session recorded by TuxAPE");
    const bool playing = session && emulator_->playSession(*session, &error);
    if (!playing)
        report(tr("Cannot play %1:\n%2.").arg(QDir::toNativeSeparators(path), error));
    playSessionAction_->setChecked(playing);
    recordSessionAction_->setChecked(emulator_->recordingSession());
    updateDebugActions();
    return playing;
}

void MainWindow::toggleSessionRecording()
{
    if (emulator_->recordingSession()) {
        stopSessionRecording();
        return;
    }
    recordSessionAction_->setChecked(false);
    QString path = QFileDialog::getSaveFileName(this, tr("Session Recording"), recordingFolder_,
                                                tr("Session Recording Files (*.snr)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1String(".snr");
    recordingFolder_ = QFileInfo(path).absolutePath();
    // From where the machine stands, or from a machine just switched on.
    QMessageBox question(QMessageBox::Question, tr("Session Recording"), tr("Where is the recording to start from?"),
                         QMessageBox::NoButton, this);
    question.addButton(tr("Current State"), QMessageBox::AcceptRole);
    QAbstractButton* cold = question.addButton(tr("Cold Reset"), QMessageBox::DestructiveRole);
    QAbstractButton* cancel = question.addButton(QMessageBox::Cancel);
    question.exec();
    if (question.clickedButton() != cancel)
        startSessionRecording(path, question.clickedButton() == cold);
}

void MainWindow::toggleSessionPlayback()
{
    if (emulator_->playingSession()) {
        emulator_->stopPlayback();
        playSessionAction_->setChecked(false);
        return;
    }
    playSessionAction_->setChecked(false);
    const QString path = QFileDialog::getOpenFileName(this, tr("Session Recording"), recordingFolder_,
                                                      tr("Session Recording Files (*.snr);;All files (*)"));
    if (path.isEmpty())
        return;
    recordingFolder_ = QFileInfo(path).absolutePath();
    playSessionFile(path);
}

// The disc editor: the files and the sectors of the disc in a drive.
void MainWindow::editDisc(int drive)
{
    DiscEditorDialog dialog(emulator_, drive, this);
    dialog.exec();
    updateDiscActions();
}

// ---- recording -------------------------------------------------------------------

void MainWindow::toggleWavRecording()
{
    if (emulator_->recordingWav()) {
        emulator_->stopWavRecording();
    } else {
        QString path = QFileDialog::getSaveFileName(this, tr("Record WAV"), recordingFolder_, tr("WAV Files (*.wav)"));
        if (!path.isEmpty()) {
            if (QFileInfo(path).suffix().isEmpty())
                path += QLatin1String(".wav");
            recordingFolder_ = QFileInfo(path).absolutePath();
            if (!emulator_->startWavRecording(path))
                report(tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
        }
    }
    recordWavAction_->setChecked(emulator_->recordingWav());
}

void MainWindow::toggleAviRecording()
{
    if (emulator_->recordingAvi()) {
        emulator_->stopAviRecording();
    } else {
        QString path = QFileDialog::getSaveFileName(this, tr("Record AVI"), recordingFolder_, tr("AVI Files (*.avi)"));
        if (!path.isEmpty()) {
            if (QFileInfo(path).suffix().isEmpty())
                path += QLatin1String(".avi");
            recordingFolder_ = QFileInfo(path).absolutePath();
            if (!emulator_->startAviRecording(path))
                report(tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
        }
    }
    recordAviAction_->setChecked(emulator_->recordingAvi());
}

void MainWindow::toggleYmRecording()
{
    if (emulator_->recordingYm()) {
        if (!emulator_->stopYmRecording())
            report(tr("The YM file could not be written."));
    } else {
        QString path = QFileDialog::getSaveFileName(this, tr("Record YM"), recordingFolder_, tr("YM Files (*.ym)"));
        if (!path.isEmpty()) {
            if (QFileInfo(path).suffix().isEmpty())
                path += QLatin1String(".ym");
            recordingFolder_ = QFileInfo(path).absolutePath();
            if (!emulator_->startYmRecording(path))
                report(tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
        }
    }
    recordYmAction_->setChecked(emulator_->recordingYm());
}

// ---- cartridges ------------------------------------------------------------------

bool MainWindow::insertCartridgeFile(const QString& path)
{
    const auto data = tuxape::readFile(path.toStdString());
    if (!data || !tuxape::Cartridge::parseCpr(*data)) {
        report(tr("%1 is not a cartridge image.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    Settings settings = settings_;
    settings.machine = emulator_->machine();
    if (!settings.machine.isPlus()) {
        // A CPC's own ROMs have no place in a Plus: its firmware, BASIC
        // and AMSDOS are in the cartridge. Other ROMs stay on their board.
        settings.machine.lowerRom.clear();
        settings.machine.upperRoms[0].clear();
        settings.machine.upperRoms[7].clear();
    }
    settings.machine.cartridge = QDir::toNativeSeparators(path).toStdString();
    settings.machine.cartridgeEnabled = settings.machine.plus = true;
    settings.crtcType = static_cast<int>(tuxape::CrtcType::AsicPlus);
    const QString error = emulator_->setupMachine(settings.machine, true);
    emulator_->setCrtcType(tuxape::CrtcType::AsicPlus);
    settings_ = settings;
    cartridgeFolder_ = QFileInfo(path).absolutePath();
    if (!error.isEmpty())
        report(tr("%1.").arg(error.left(1).toUpper() + error.mid(1)));
    if (!settings_.save())
        report(tr("Cannot save the settings to %1.").arg(QDir::toNativeSeparators(Settings::file())));
    return error.isEmpty();
}

void MainWindow::chooseCartridge()
{
    const QString folder = cartridgeFolder_.isEmpty() ? QString::fromStdString(tuxape::defaultRomDir().string())
                                                      : cartridgeFolder_;
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Cartridge"), folder,
                                                      tr("Cartridges (*.cpr);;All files (*)"));
    if (!path.isEmpty())
        insertCartridgeFile(path);
    screen_->setFocus();
}

// ---- the cassette deck ---------------------------------------------------------

bool MainWindow::insertTapeFile(const QString& path)
{
    const auto data = tuxape::readFile(path.toStdString());
    if (!insertTapeData(data ? std::span<const uint8_t>(*data) : std::span<const uint8_t>(), path))
        return false;
    tapeFolder_ = QFileInfo(path).absolutePath();
    return true;
}

bool MainWindow::insertTapeData(std::span<const uint8_t> data, const QString& name)
{
    auto tape = tuxape::Tape::parse(data);
    if (!tape) {
        report(tr("%1 is not a tape image.").arg(QDir::toNativeSeparators(name)));
        return false;
    }
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        cpc.tape().insert(std::move(*tape));
        cpc.tape().play(cpc.microseconds());
    });
    tapePath_ = name;
    if (tapeDialog_)
        tapeDialog_->setTape(tapePath_);
    updateTapeActions();
    return true;
}

void MainWindow::chooseTape()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Insert Tape Image"), tapeFolder_,
                                                      tr("Tape images (*.cdt *.tzx *.wav *.voc);;All files (*)"));
    if (!path.isEmpty())
        insertTapeFile(path);
}

void MainWindow::removeTape()
{
    emulator_->withMachine([](tuxape::Cpc& cpc) { cpc.tape().eject(); });
    tapePath_.clear();
    if (tapeDialog_)
        tapeDialog_->setTape(QString());
    updateTapeActions();
}

void MainWindow::showTapeControl()
{
    if (!tapeDialog_) {
        tapeDialog_ = new TapeDialog(emulator_, this);
        tapeDialog_->setTape(tapePath_);
        connect(tapeDialog_, &TapeDialog::openRequested, this, &MainWindow::chooseTape);
        connect(tapeDialog_, &TapeDialog::ejectRequested, this, &MainWindow::removeTape);
    }
    tapeDialog_->show();
    tapeDialog_->raise();
}

void MainWindow::updateTapeActions()
{
    const bool loaded = emulator_->withMachine([](tuxape::Cpc& cpc) { return cpc.tape().loaded(); });
    for (QAction* action : {rewindTapeAction_, removeTapeAction_, playTapeAction_})
        action->setEnabled(loaded);
}

// The Library window: the program chosen there goes into the machine, a
// disc in its drive, a tape in the deck, a snapshot as it is.
void MainWindow::showLibrary()
{
    LibraryDialog dialog(settings_.libraryFolders, this);
    dialog.setCategory(libraryCategory_);
    dialog.setSearch(librarySearch_);
    const int closed = dialog.exec();
    librarySearch_ = dialog.search();
    libraryCategory_ = dialog.category();
    if (dialog.folders() != settings_.libraryFolders) {
        settings_.libraryFolders = dialog.folders();
        if (!settings_.save())
            report(tr("Cannot save the settings to %1.").arg(QDir::toNativeSeparators(Settings::file())));
    }
    const LibraryEntry* chosen = dialog.chosen();
    if (closed != QDialog::Accepted || !chosen)
        return;
    if (chosen->member.isEmpty()) {
        if (chosen->kind == LibraryEntry::Snapshot)
            loadSnapshotFile(chosen->path);
        else if (chosen->kind == LibraryEntry::Tape)
            insertTapeFile(chosen->path);
        else if (chosen->kind == LibraryEntry::Cartridge)
            insertCartridgeFile(chosen->path);
        else if (saveBeforeLeaving(dialog.drive()))
            insertDiscFile(dialog.drive(), chosen->path);
        return;
    }
    // Out of an archive: the program is used as it is in there, and a disc
    // cannot be written back.
    const QString name = libraryDisplayPath(*chosen);
    const auto data = libraryData(*chosen);
    if (!data)
        report(tr("Cannot read %1.").arg(name));
    else if (chosen->kind == LibraryEntry::Snapshot)
        loadSnapshotData(*data, name);
    else if (chosen->kind == LibraryEntry::Tape)
        insertTapeData(*data, name);
    else if (chosen->kind == LibraryEntry::Cartridge) {
        // A cartridge is named in the settings, to be there at the next
        // start: it is taken out of its archive and kept beside them.
        const QString folder = QFileInfo(Settings::file()).absolutePath() + "/Cartridge";
        const QString file = folder + '/' + QFileInfo(chosen->member).fileName();
        QDir().mkpath(folder);
        if (tuxape::writeFile(file.toStdString(), *data))
            insertCartridgeFile(file);
        else
            report(tr("Cannot write %1.").arg(QDir::toNativeSeparators(file)));
    } else if (saveBeforeLeaving(dialog.drive()))
        report(discs_->insertImage(dialog.drive(), *data, name));
}

void MainWindow::autoType()
{
    AutoTypeDialog dialog(autoTypeText_, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    autoTypeText_ = dialog.text();
    emulator_->autoType(autoTypeText_);
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
    applyWindowOptions();
    screen_->setFocus();
}

void MainWindow::paste()
{
    emulator_->autoType(QApplication::clipboard()->text());
}

void MainWindow::updateStats(int speedPercent, int framesPerSecond)
{
    if (emulator_->isPaused())
        return;
    QString text = tr("%1%  FPS: %2").arg(speedPercent).arg(framesPerSecond);
    recordAviAction_->setChecked(emulator_->recordingAvi());
    // A session playing: where it is, and how long it is.
    if (emulator_->playingSession()) {
        const auto [played, total] = emulator_->playbackPosition();
        auto clock = [](uint32_t frames) {
            return QStringLiteral("%1:%2").arg(frames / 50 / 60).arg(frames / 50 % 60, 2, 10, QLatin1Char('0'));
        };
        text += tr("  Playback %1 / %2").arg(clock(played), clock(total));
    } else if (emulator_->recordingSession()) {
        text += tr("  Recording");
    }
    statusLabel_->setText(text);
}

// ---- disc drives -----------------------------------------------------------

void MainWindow::report(const QString& error)
{
    if (!error.isEmpty())
        QMessageBox::warning(this, windowTitle(), error);
}

bool MainWindow::insertDiscFile(int drive, const QString& path)
{
    const QString error = discs_->insert(drive, path);
    report(error);
    if (error.isEmpty())
        discFolder_ = QFileInfo(path).absolutePath();
    return error.isEmpty();
}

bool MainWindow::saveBeforeLeaving(int drive)
{
    const DiscManager::Info info = discs_->info(drive);
    if (!info.present || !info.modified || info.readOnly || info.path.isEmpty())
        return true;
    if (discs_->promptToSave()) {
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            tr("The disc in drive %1: has been changed.\n\nSave the changes to %2?")
                .arg(driveLetter(drive))
                .arg(QDir::toNativeSeparators(info.path)),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::No)
            return true;
    }
    const QString error = discs_->save(drive);
    report(error);
    return error.isEmpty();
}

void MainWindow::chooseDisc(int drive)
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Insert Disc Image in Drive %1:").arg(driveLetter(drive)), discFolder_, discFilter());
    if (!path.isEmpty() && saveBeforeLeaving(drive))
        insertDiscFile(drive, path);
    screen_->setFocus();
}

bool MainWindow::loadSnapshotFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(tr("Cannot read %1:\n%2").arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (!loadSnapshotData({reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size())},
                          QDir::toNativeSeparators(path)))
        return false;
    snapshotPath_ = path;
    snapshotFolder_ = QFileInfo(path).absolutePath();
    updateSnapshotAction_->setEnabled(true);
    return true;
}

bool MainWindow::loadSnapshotData(std::span<const uint8_t> data, const QString& name)
{
    std::string error;
    const bool ok = emulator_->withMachine([&](tuxape::Cpc& cpc) { return tuxape::loadSnapshot(cpc, data, &error); });
    if (!ok)
        report(tr("Cannot load %1:\n%2").arg(name, QString::fromStdString(error)));
    return ok;
}

bool MainWindow::saveSnapshotFile(const QString& path)
{
    using tuxape::CpcModel;
    using tuxape::SnapshotMachine;
    const CpcModel model = emulator_->model();
    const SnapshotMachine machine = model == CpcModel::Cpc464     ? SnapshotMachine::Cpc464
                                    : model == CpcModel::Cpc664   ? SnapshotMachine::Cpc664
                                    : model == CpcModel::Plus464  ? SnapshotMachine::Plus464
                                    : model == CpcModel::Plus6128 ? SnapshotMachine::Plus6128
                                                                  : SnapshotMachine::Cpc6128;
    const std::vector<uint8_t> bytes =
        emulator_->withMachine([&](tuxape::Cpc& cpc) { return tuxape::saveSnapshot(cpc, machine); });
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()))
               != static_cast<qint64>(bytes.size())) {
        report(tr("Cannot write %1:\n%2").arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    snapshotPath_ = path;
    snapshotFolder_ = QFileInfo(path).absolutePath();
    updateSnapshotAction_->setEnabled(true);
    return true;
}

void MainWindow::chooseSnapshot()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Snapshot"), snapshotFolder_, snapshotFilter());
    if (!path.isEmpty())
        loadSnapshotFile(path);
    screen_->setFocus();
}

void MainWindow::saveSnapshotAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Snapshot"), snapshotFolder_, snapshotFilter());
    if (!path.isEmpty()) {
        if (QFileInfo(path).suffix().isEmpty())
            path += QLatin1String(".sna");
        saveSnapshotFile(path);
    }
    screen_->setFocus();
}

void MainWindow::newBlankDisc(int drive)
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("New Blank Disc for Drive %1:").arg(driveLetter(drive)), discFolder_, discFilter());
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".dsk");
    FormatDialog dialog(this);
    dialog.setWindowTitle(tr("New Blank Disc"));
    if (dialog.exec() != QDialog::Accepted || !saveBeforeLeaving(drive))
        return;
    const QString error = discs_->createBlank(drive, path, dialog.format());
    report(error);
    if (error.isEmpty())
        discFolder_ = QFileInfo(path).absolutePath();
    screen_->setFocus();
}

void MainWindow::formatDisc(int drive)
{
    FormatDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto answer = QMessageBox::warning(
        this, windowTitle(),
        tr("Formatting will erase everything on the disc in drive %1:.").arg(driveLetter(drive)),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Ok)
        discs_->format(drive, dialog.format(), dialog.clearUnused());
    screen_->setFocus();
}

void MainWindow::removeDisc(int drive)
{
    if (saveBeforeLeaving(drive))
        discs_->remove(drive);
}

void MainWindow::flipDisc(int drive)
{
    discs_->flip(drive);
}

void MainWindow::swapDiscs()
{
    discs_->swap();
}

void MainWindow::driveSetup()
{
    DriveSetupDialog dialog(discs_, this);
    connect(&dialog, &DriveSetupDialog::openRequested, this, &MainWindow::chooseDisc);
    connect(&dialog, &DriveSetupDialog::removeRequested, this, &MainWindow::removeDisc);
    connect(&dialog, &DriveSetupDialog::flipRequested, this, &MainWindow::flipDisc);
    connect(&dialog, &DriveSetupDialog::swapRequested, this, &MainWindow::swapDiscs);
    dialog.exec();
    screen_->setFocus();
}

void MainWindow::updateDiscActions()
{
    for (int drive = 0; drive < DiscManager::kDrives; ++drive) {
        const DiscManager::Info info = discs_->info(drive);
        driveActions_[drive].edit->setEnabled(info.present);
        driveActions_[drive].format->setEnabled(info.present);
        driveActions_[drive].remove->setEnabled(info.present);
        driveLed_[drive]->setToolTip(info.present ? QDir::toNativeSeparators(info.path) : tr("No disc"));
    }
}

void MainWindow::updateDriveLights()
{
    const int lit = discs_->activity().activeDrive;
    if (lit == litDrive_)
        return;
    litDrive_ = lit;
    for (int drive = 0; drive < DiscManager::kDrives; ++drive)
        driveLed_[drive]->setStyleSheet(QLatin1String(drive == lit ? kLedOn : kLedOff));
}
