#include "tapedialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include "emulator.h"

TapeDialog::TapeDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("Tape Control"));

    name_ = new QLineEdit;
    name_->setObjectName("edTape");
    name_->setReadOnly(true);
    auto* open = new QPushButton(tr("&Open"));
    open->setObjectName("bOpen");
    open->setAutoDefault(false);
    block_ = new QComboBox;
    block_->setObjectName("cbBlock");

    auto key = [this](const char* name, QStyle::StandardPixmap icon, const QString& hint) {
        auto* button = new QToolButton;
        button->setObjectName(name);
        button->setIcon(style()->standardIcon(icon));
        button->setToolTip(hint);
        button->setAutoRaise(true);
        return button;
    };
    rewind_ = key("bRewind", QStyle::SP_MediaSeekBackward, tr("Rewind"));
    play_ = key("bPlay", QStyle::SP_MediaPlay, tr("Play"));
    record_ = key("bRecord", QStyle::SP_DialogNoButton, tr("Record"));
    record_->setCheckable(true);
    stop_ = key("bStop", QStyle::SP_MediaStop, tr("Stop"));
    eject_ = key("bEject", QStyle::SP_ArrowUp, tr("Eject"));
    play_->setCheckable(true);
    led_ = new QFrame;
    led_->setObjectName("ledMotor");
    led_->setFixedSize(14, 14);
    led_->setToolTip(tr("Motor"));

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Tape:")), 0, 0);
    grid->addWidget(name_, 0, 1);
    grid->addWidget(open, 0, 2);
    grid->addWidget(new QLabel(tr("Block:")), 1, 0);
    grid->addWidget(block_, 1, 1, 1, 2);
    auto* keys = new QHBoxLayout;
    for (QToolButton* button : {rewind_, play_, record_, stop_, eject_})
        keys->addWidget(button);
    keys->addStretch(1);
    keys->addWidget(led_);
    // What is being recorded, and how much tape it has taken.
    recordingPanel_ = new QWidget;
    recordingPanel_->setObjectName("pRecording");
    recordingName_ = new QLineEdit;
    recordingName_->setObjectName("edRecord");
    recordingName_->setReadOnly(true);
    recordingTime_ = new QLabel;
    recordingTime_->setObjectName("lRecordTime");
    auto* recording = new QHBoxLayout(recordingPanel_);
    recording->setContentsMargins(0, 0, 0, 0);
    recording->addWidget(new QLabel(tr("Recording:")));
    recording->addWidget(recordingName_, 1);
    recording->addWidget(recordingTime_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(grid);
    layout->addLayout(keys);
    layout->addWidget(recordingPanel_);
    setMinimumWidth(300);

    auto deck = [this](auto&& act) {
        emulator_->withMachine([&](tuxape::Cpc& cpc) { act(cpc.tape(), cpc.microseconds()); });
        refresh();
    };
    connect(open, &QPushButton::clicked, this, &TapeDialog::openRequested);
    connect(eject_, &QToolButton::clicked, this, &TapeDialog::ejectRequested);
    connect(rewind_, &QToolButton::clicked, this,
            [deck] { deck([](tuxape::TapeDeck& tape, uint64_t now) { tape.rewind(now); }); });
    connect(play_, &QToolButton::clicked, this,
            [deck] { deck([](tuxape::TapeDeck& tape, uint64_t now) { tape.play(now); }); });
    connect(record_, &QToolButton::clicked, this, [this] {
        emit recordRequested();
        // The key stays up if no recording came of it.
        refresh();
    });
    connect(stop_, &QToolButton::clicked, this, [this, deck] {
        // Stop ends a recording, which the main window writes to its file.
        if (recording_)
            emit recordRequested();
        else
            deck([](tuxape::TapeDeck& tape, uint64_t now) { tape.stop(now); });
    });
    connect(block_, &QComboBox::activated, this, [deck](int index) {
        deck([index](tuxape::TapeDeck& tape, uint64_t now) { tape.seekBlock(static_cast<size_t>(index), now); });
    });

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &TapeDialog::refresh);
    timer_->start(100);
    setTape(QString());
    setRecording(QString());
}

void TapeDialog::setRecording(const QString& path)
{
    recording_ = !path.isEmpty();
    recordingName_->setText(QFileInfo(path).fileName());
    recordingName_->setCursorPosition(0);
    recordingName_->setToolTip(QDir::toNativeSeparators(path));
    recordingPanel_->setVisible(recording_);
    refresh();
}

void TapeDialog::setTape(const QString& path)
{
    name_->setText(QFileInfo(path).fileName());
    name_->setCursorPosition(0);
    name_->setToolTip(QDir::toNativeSeparators(path));
    QStringList blocks;
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        for (const tuxape::Tape::Block& block : cpc.tape().tape().blocks())
            blocks << QStringLiteral("%1 - %2").arg(blocks.size() + 1).arg(QString::fromLatin1(block.name.c_str()));
    });
    block_->clear();
    block_->addItems(blocks);
    refresh();
}

void TapeDialog::refresh()
{
    struct State {
        bool loaded, playing, motor, recording;
        int block;
        uint64_t recorded;
    };
    const State state = emulator_->withMachine([](tuxape::Cpc& cpc) {
        tuxape::TapeDeck& deck = cpc.tape();
        // Looking at the head moves the tape on to where it now is.
        deck.level(cpc.microseconds());
        return State{deck.loaded(),    deck.playing(),                 deck.motor(),
                     deck.recording(), static_cast<int>(deck.block()), deck.recordedTime(cpc.microseconds())};
    });
    block_->setEnabled(state.loaded);
    eject_->setEnabled(state.loaded);
    // Record holds Play down: Stop comes first.
    for (QToolButton* button : {rewind_, play_})
        button->setEnabled(state.loaded && !state.recording);
    stop_->setEnabled(state.loaded || state.recording);
    play_->setChecked(state.playing || state.recording);
    record_->setChecked(state.recording);
    if (state.recording) {
        const int seconds = static_cast<int>(state.recorded / 1000000);
        recordingTime_->setText(QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')));
    }
    // The list is left alone while it is open for a choice.
    if (state.loaded && !block_->view()->isVisible() && block_->currentIndex() != state.block)
        block_->setCurrentIndex(state.block);
    const bool lit = state.motor && (state.playing || state.recording);
    led_->setStyleSheet(QStringLiteral("background: %1; border: 1px solid palette(window-text); border-radius: 7px")
                            .arg(lit ? "#30e030" : "#205020"));
    led_->setProperty("lit", lit);
}
