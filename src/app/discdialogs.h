#pragma once

#include <QDialog>

#include "core/disc.h"

class DiscManager;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTabBar;

// "Format Disc Image": choice of one of the known disc formats.
class FormatDialog : public QDialog {
    Q_OBJECT

public:
    explicit FormatDialog(QWidget* parent = nullptr);

    const tuxape::DiscFormat& format() const;
    bool clearUnused() const;

private:
    QComboBox* format_;
    QCheckBox* clear_;
};

// "Drive Setup": what is in each drive, and the options for disc images.
// The dialog asks its owner to do the actual work through signals, so that
// the same code paths serve the menus and the dialog.
class DriveSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit DriveSetupDialog(DiscManager* discs, QWidget* parent = nullptr);

    void accept() override;

signals:
    void openRequested(int drive);
    void removeRequested(int drive);
    void flipRequested(int drive);
    void swapRequested();

private:
    DiscManager* discs_;
    QTabBar* tabs_;
    QRadioButton* none_;
    QRadioButton* diskFile_;
    QLabel* description_;
    QCheckBox* singleSided_;
    QCheckBox* temporaryWrites_;
    QCheckBox* promptToSave_;
    QPushButton* flip_;
    bool singleSidedChoice_[2] = {};

    int drive() const;
    void refresh();
};
