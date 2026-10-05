#pragma once

#include <QDialog>

#include "settings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTabWidget;

// WinAPE's Setup window: a profile selector above six pages of settings
// (General, Display, Sound, Memory, Input, Other). The pages and controls
// carry the object names of WinAPE's own form (cbCRTCType, ckFastDisc...).
// What TuxAPE cannot do yet is shown greyed out.
class SetupDialog : public QDialog {
    Q_OBJECT

public:
    enum Page { General, Display, Sound, Memory, Input, Other };

    explicit SetupDialog(const Settings& settings, QWidget* parent = nullptr);

    void showPage(Page page);
    // The settings as the window now shows them.
    Settings settings() const;

    // The ROM named in a row of the Memory page's list: row 0 is the
    // firmware ROM, row 1 upper ROM 0, and so on. Empty for none.
    QString rom(int row) const;
    void setRom(int row, const QString& name);
    // What the list shows for a row without a ROM.
    static QString emptyRomText(int row);

private:
    Settings initial_;
    QTabWidget* tabs_ = nullptr;
    QComboBox* crtcType_ = nullptr;
    QCheckBox* fastDisc_ = nullptr;
    QSlider* speed_ = nullptr;
    QLabel* speedLabel_ = nullptr;
    QCheckBox* displayEvery_ = nullptr;
    QSpinBox* displayEveryFrames_ = nullptr;

    QRadioButton* ram_[4] = {};
    QCheckBox* siliconDisc_ = nullptr;
    QLabel* totalRam_ = nullptr;
    QTableWidget* roms_ = nullptr;
    QCheckBox* rom32_ = nullptr;
    QCheckBox* disableRoms_ = nullptr;
    QCheckBox* onlyLower0And7_ = nullptr;
    // The names of all 33 ROMs, shown or not.
    QString romNames_[1 + tuxape::Memory::kRomSlots];

    QWidget* createGeneralPage();
    QWidget* createMemoryPage();
    void updateTiming();
    void updateTotalRam();
    void updateRomRows();
    tuxape::RamExpansion chosenRam() const;
};
