#pragma once

#include <QDialog>

class Emulator;
class QComboBox;
class QFrame;
class QLineEdit;
class QTimer;
class QToolButton;

// WinAPE's "Tape Control" window: the tape in the deck, the block under the
// head (any block can be gone to), the deck's keys, and a light for the
// motor. It stays open beside the main window.
class TapeDialog : public QDialog {
    Q_OBJECT

public:
    explicit TapeDialog(Emulator* emulator, QWidget* parent = nullptr);

    // The file the tape came from, for the Tape box; the blocks are read
    // from the deck. To be called when a tape goes in or comes out.
    void setTape(const QString& path);
    // Brings the window up to date with the deck. It does so by itself ten
    // times a second.
    void refresh();

signals:
    // The Open and eject buttons: the main window does the rest.
    void openRequested();
    void ejectRequested();

private:
    Emulator* emulator_;
    QLineEdit* name_;
    QComboBox* block_;
    QToolButton* rewind_;
    QToolButton* play_;
    QToolButton* record_;
    QToolButton* stop_;
    QToolButton* eject_;
    QFrame* led_;
    QTimer* timer_;
};
