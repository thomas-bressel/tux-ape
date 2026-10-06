#pragma once

#include <QDialog>
#include <QImage>

#include <QList>

#include "core/keymap.h"
#include "settings.h"

class QAbstractButton;
class QCheckBox;
class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTreeWidget;

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
    void setSettings(const Settings& settings);

    // Profiles. Loading one changes, on the pages, the settings it holds;
    // nothing reaches the machine before OK. False if the file cannot be
    // read or written.
    bool loadProfile(const QString& path);
    bool saveProfile(const QString& path, unsigned parts) const;

    // The picture the Display page shows a piece of, in the colours of the
    // monitor chosen there. It must have been made with the settings the
    // window was opened with.
    void setPreview(const QImage& frame);

signals:
    // The shader's box or one of its sliders has moved: for the picture to
    // show it at once, before OK.
    void shaderChanged(bool on, const CrtLook& look);

public:

    // The keyboard layout the Input page shows and changes.
    void setKeyMap(const tuxape::KeyMap& map);
    const tuxape::KeyMap& keyMap() const { return keyMap_; }
    // Clicks a key of the Input page's CPC keyboard, for its PC keys to be
    // shown; -1 for none.
    void selectCpcKey(int key);
    int selectedCpcKey() const { return selectedKey_; }
    // Reads or writes the layout as a .kbd file. False if it cannot be done.
    bool loadKeyboard(const QString& path);
    bool saveKeyboard(const QString& path);

    // The ROM named in a row of the Memory page's list: row 0 is the
    // firmware ROM, row 1 upper ROM 0, and so on. Empty for none.
    QString rom(int row) const;
    void setRom(int row, const QString& name);
    // What the list shows for a row without a ROM.
    static QString emptyRomText(int row);

private:
    QComboBox* profile_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QComboBox* crtcType_ = nullptr;
    QCheckBox* fastDisc_ = nullptr;
    QCheckBox* enablePlus_ = nullptr;
    QSlider* speed_ = nullptr;
    QLabel* speedLabel_ = nullptr;
    QCheckBox* displayEvery_ = nullptr;
    QSpinBox* displayEveryFrames_ = nullptr;

    Settings opened_;  // what the window was opened with
    QImage previewFrame_;
    QLabel* preview_ = nullptr;
    QRadioButton* monitor_[3] = {};
    QSlider* verticalHold_ = nullptr;
    QLabel* verticalHoldLabel_ = nullptr;
    QSlider* brightness_ = nullptr;
    QLabel* brightnessLabel_ = nullptr;
    QCheckBox* linearPalette_ = nullptr;
    QCheckBox* pal_ = nullptr;
    QCheckBox* crtShader_ = nullptr;
    QSlider* crtSliders_[6] = {};
    CrtLook chosenLook() const;
    void setLook(const CrtLook& look);
    QCheckBox* turbo_ = nullptr;
    QCheckBox* plusPpi_ = nullptr;
    QCheckBox* tapeSounds_ = nullptr;
    QCheckBox* amDrum_ = nullptr;
    QCheckBox* fourDrives_ = nullptr;
    QCheckBox* amxMouse_ = nullptr;
    // The Other page: what is on the printer's port, by Settings::PrinterMode.
    QRadioButton* printer_[5] = {};
    QLineEdit* printerFile_ = nullptr;
    QCheckBox* driveLed_ = nullptr;
    QCheckBox* showTrack_ = nullptr;
    // Half size, both lines, hide mouse, hide panel, hide menus, no
    // right-click menu: for the window, then for full screen.
    QCheckBox* windowOptions_[2][6] = {};

    QRadioButton* soundOutput_[3] = {};  // none, PC speaker, sound card
    QRadioButton* soundRate_[2] = {};    // 22 kHz, 44 kHz
    QRadioButton* soundBits_[2] = {};    // 8, 16
    QRadioButton* soundChannels_[2] = {};  // mono, stereo
    QSlider* soundVolume_ = nullptr;
    QLabel* soundVolumeLabel_ = nullptr;
    QSlider* soundBufferSync_ = nullptr;
    QLabel* soundBufferSyncLabel_ = nullptr;

    tuxape::KeyMap keyMap_;
    QString keyboardFile_;
    int selectedKey_ = -1;
    QList<QAbstractButton*> keyButtons_;
    QComboBox* keyCombos_[2][3] = {};  // Num Lock off then on; first, second, third key
    QCheckBox* joystick_ = nullptr;
    QPushButton* loadKeys_ = nullptr;
    QPushButton* saveKeys_ = nullptr;

    QRadioButton* ram_[4] = {};
    QCheckBox* siliconDisc_ = nullptr;
    QCheckBox* enableCartridge_ = nullptr;
    QLabel* cartridgeFile_ = nullptr;
    QString cartridge_;  // a file of the ROM folder by its name, or a path
    void setCartridge(const QString& cartridge);
    void chooseCartridge();
    QLabel* totalRam_ = nullptr;
    QTableWidget* roms_ = nullptr;
    QCheckBox* rom32_ = nullptr;
    QCheckBox* disableRoms_ = nullptr;
    QCheckBox* onlyLower0And7_ = nullptr;
    // The names of all 33 ROMs, shown or not.
    QString romNames_[1 + tuxape::Memory::kRomSlots];

    QWidget* createGeneralPage();
    QWidget* createDisplayPage();
    QWidget* createSoundPage();
    QWidget* createInputPage();
    QWidget* createOtherPage();
    void showKeyBindings();
    void updateSoundOptions();
    QWidget* createMemoryPage();
    void updatePreview();
    int chosenMonitor() const;
    void fillProfiles();
    void profileChosen(int index);
    void saveProfileAs();
    void updateTiming();
    void updateTotalRam();
    void updateRomRows();
    tuxape::RamExpansion chosenRam() const;
};

// Which settings go into a profile: WinAPE's "Select Profile Settings"
// window, a tree of the settings with a box to tick for each.
class ProfilePartsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProfilePartsDialog(QWidget* parent = nullptr);

    // The settings ticked, as Settings::Part bits.
    unsigned parts() const;

private:
    QTreeWidget* tree_ = nullptr;
};
