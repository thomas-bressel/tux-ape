#include "mainwindow.h"

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
#include <QMimeData>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "discdialogs.h"
#include "discmanager.h"
#include "emulator.h"
#include "icons.h"
#include "screenwidget.h"

namespace {

constexpr int kPanelHeight = 30;
constexpr int kButtonSize = 26;

const char kLedOff[] = "background-color: #500000;";
const char kLedOn[] = "background-color: #ff2020;";

QString discFilter()
{
    return MainWindow::tr("Disc files (*.dsk);;All files (*)");
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
    // A dropped disc image goes into drive A:, a second one into B:.
    int drive = 0;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile() || drive >= DiscManager::kDrives)
            continue;
        if (saveBeforeLeaving(drive) && insertDiscFile(drive, url.toLocalFile()))
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
    const char* driveNames[2] = {QT_TR_NOOP("Drive &A:"), QT_TR_NOOP("Drive &B:")};
    for (int drive = 0; drive < 2; ++drive) {
        const Qt::Key key = drive == 0 ? Qt::Key_F1 : Qt::Key_F2;
        QMenu* menu = file->addMenu(tr(driveNames[drive]));
        addItem(menu, tr("&Insert Disc Image..."), CTRL | key, [this, drive] { chooseDisc(drive); });
        addItem(menu, tr("&New Blank Disc..."), {}, [this, drive] { newBlankDisc(drive); });
        driveActions_[drive].format =
            addItem(menu, tr("&Format Disc Image..."), {}, [this, drive] { formatDisc(drive); });
        addItem(menu, tr("&Edit Disc..."), SHIFT | CTRL | key);
        driveActions_[drive].flip = addItem(menu, tr("F&lip Disc"), SHIFT | key, [this, drive] { flipDisc(drive); });
        driveActions_[drive].remove = addItem(menu, tr("&Remove Disc"), {}, [this, drive] { removeDisc(drive); });
    }
    swapAction_ = addItem(file, tr("&Swap Discs A: and B:"), SHIFT | CTRL | Qt::Key_F3, [this] { swapDiscs(); });
    driveSetupAction_ = addItem(file, tr("&Drive Setup..."), Qt::Key_F2, [this] { driveSetup(); });
    addItem(file, tr("Load &Cartridge..."), CTRL | Qt::Key_F3);
    QMenu* tape = file->addMenu(tr("&Tape"));
    addItem(tape, tr("&Show Tape Control"));
    addItem(tape, tr("&Insert Tape Image..."), CTRL | Qt::Key_F4);
    addItem(tape, tr("Re&wind Tape"));
    addItem(tape, tr("R&emove Tape"));
    addItem(tape, tr("&Press Play"));
    addItem(tape, tr("Press &Record"));
    file->addSeparator();
    addItem(file, tr("&Load Snapshot..."), Qt::Key_F5);
    addItem(file, tr("Save S&napshot..."), Qt::Key_F6);
    addItem(file, tr("&Update Snapshot"), CTRL | Qt::Key_F6);
    file->addSeparator();
    addItem(file, tr("Playbac&k Session..."));
    addItem(file, tr("&Record Session..."));
    file->addSeparator();
    addItem(file, tr("Save Screens&hot..."), CTRL | Qt::Key_F7);
    addItem(file, tr("R&ecord AVI..."));
    addItem(file, tr("Record &WAV..."));
    addItem(file, tr("Record &YM..."));
    file->addSeparator();
    addItem(file, tr("Po&kes..."), CTRL | Qt::Key_F8);
    file->addSeparator();
    addItem(file, tr("Aut&o Type..."), CTRL | Qt::Key_F5);
    pasteAction_ = addItem(file, tr("&Paste"), CTRL | Qt::Key_F11, [this] { paste(); });
    pasteAction_->setEnabled(!QApplication::clipboard()->text().isEmpty());
    file->addSeparator();
    addItem(file, tr("E&xit"), {}, [this] { close(); });

    // ---- Settings ----
    QMenu* settings = menuBar()->addMenu(tr("&Settings"));
    addItem(settings, tr("&General"));
    addItem(settings, tr("&Display"));
    addItem(settings, tr("&Sound"));
    addItem(settings, tr("&Memory"));
    addItem(settings, tr("&Input"));
    addItem(settings, tr("&Other"));
    settings->addSeparator();
    addItem(settings, tr("&Normal Speed (100%)"), SHIFT | Qt::Key_F3, [this] { emulator_->setSpeedPercent(100); });
    addItem(settings, tr("&High Speed (1000%)"), SHIFT | Qt::Key_F4, [this] { emulator_->setSpeedPercent(1000); });
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
    addItem(debug, tr("&Registers"));
    addItem(debug, tr("&Breakpoints"));
    addItem(debug, tr("&Data Areas"));
    addItem(debug, tr("&Timers"));
    addItem(debug, tr("&Find Graphics"));

    // ---- Assembler ----
    QMenu* assembler = menuBar()->addMenu(tr("&Assembler"));
    addItem(assembler, tr("Show &Assembler"), Qt::Key_F3);

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
    addButton(IconId::SingleStep, tr("Single Step (F7)"), nullptr);
    addButton(IconId::StepOver, tr("Step Over (F8)"), nullptr);
    row->addWidget(separator(controlPanel_));
    addButton(IconId::Registers, tr("Registers"), nullptr);
    addButton(IconId::Assembler, tr("Assembler (F3)"), nullptr);
    row->addWidget(separator(controlPanel_));
    addButton(IconId::Disc, tr("Change Disc (F2)"), driveSetupAction_);
    addButton(IconId::Cartridge, tr("Change Cartridge (CTRL+F3)"), nullptr);
    addButton(IconId::Tape, tr("Tape Control"), nullptr);
    addButton(IconId::LoadSnapshot, tr("Load Snapshot (F5)"), nullptr);
    addButton(IconId::SaveSnapshot, tr("Save Snapshot (F6)"), nullptr);
    row->addWidget(separator(controlPanel_));
    addButton(IconId::Settings, tr("Settings (F12)"), nullptr);
    addButton(IconId::Pokes, tr("Pokes (CTRL+F8)"), nullptr);
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

void MainWindow::setPaused(bool paused)
{
    emulator_->setPaused(paused);
    runAction_->setEnabled(paused);
    pauseAction_->setEnabled(!paused);
    if (paused)
        statusLabel_->setText(tr("Paused"));
}

void MainWindow::toggleFullScreen()
{
    const bool enter = !isFullScreen();
    // WinAPE's defaults for full screen: nothing but the picture.
    menuBar()->setVisible(!enter);
    controlPanel_->setVisible(!enter);
    screen_->setCursor(enter ? Qt::BlankCursor : Qt::ArrowCursor);
    if (enter)
        showFullScreen();
    else
        showNormal();
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
    statusLabel_->setText(tr("%1%  FPS: %2").arg(speedPercent).arg(framesPerSecond));
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
