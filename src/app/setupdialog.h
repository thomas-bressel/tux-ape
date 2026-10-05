#pragma once

#include <QDialog>

#include "settings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
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

private:
    Settings initial_;
    QTabWidget* tabs_ = nullptr;
    QComboBox* crtcType_ = nullptr;
    QCheckBox* fastDisc_ = nullptr;
    QSlider* speed_ = nullptr;
    QLabel* speedLabel_ = nullptr;
    QCheckBox* displayEvery_ = nullptr;
    QSpinBox* displayEveryFrames_ = nullptr;

    QWidget* createGeneralPage();
    void updateTiming();
};
