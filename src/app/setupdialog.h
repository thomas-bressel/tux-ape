#pragma once

#include <map>
#include <vector>

#include <QByteArray>
#include <QDialog>
#include <QImage>

#include <QList>

#include "core/keymap.h"
#include "settings.h"

class QAbstractButton;
class QCheckBox;
class QGroupBox;
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
    enum Page { General, Display, Sound, Memory, Input, Other, Expansions };

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
    // Asks for the file of a row's ROM ("Select File..."). No file, no
    // change.
    void chooseRom(int row);
    // What the list shows for a row without a ROM.
    static QString emptyRomText(int row);

    // The Expansions page, TuxAPE's own: the cards on the expansion port,
    // in the four places of a MotherX4 board, and what each card has to
    // set. A card is in one place at the most: put in another, it leaves
    // the one it was in.
    tuxape::Card card(int place) const;
    void setCard(int place, tuxape::Card card);
    // The X-MEM's switches, and the file its flash memory is kept in.
    tuxape::Xmem::Switches xmemSwitches() const;
    void setXmemSwitches(const tuxape::Xmem::Switches& switches);
    QString xmemFlash() const { return xmemFlash_; }
    void setXmemFlash(const QString& file);
    // What that flash memory holds, as its file has it: a line for the
    // lower ROM and one for each of ROMs 0 to 31 but 7, "5: UNIDOS  1.40
    // (background)" or "5: (empty)". The button "ROMs..." shows them.
    QStringList xmemRoms() const;
    void showXmemRoms();
    // A ROM put in a place of that flash memory from here, as a programmer
    // would, or a place erased: 0 to 31, 7 being the lower ROM's. It shows
    // at once in the lists; it is in the card, and in its file, once the
    // window is accepted (see xmemEdits). False for a file that cannot be
    // read.
    bool loadXmemRom(int place, const QString& file);
    void eraseXmemRom(int place);
    // What has been asked for so far: the 16K of each place changed, none
    // for one erased.
    std::map<int, QByteArray> xmemEdits() const { return xmemEdits_; }
    // Whether a row of the Memory page's list of ROMs is the X-MEM's for
    // the time being: with the card in a place and READ ROM on YES, ROMs 1
    // to 31 but 7 are, and with BOOT on ROM the lower ROM and ROM 0 too.
    // Such a row is greyed, shows what the card holds there ("X-MEM:
    // UNIDOS  1.40") and cannot be changed; the name it had is kept for
    // when the card is out.
    bool xmemTakesRow(int row) const;
    QString xmemRowText(int row) const;
    // The file that is the X-MASS's disc.
    QString xmassDisc() const { return xmassDisc_; }
    void setXmassDisc(const QString& file);

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
    QCheckBox* displaySync_ = nullptr;
    QCheckBox* welcome_ = nullptr;
    QCheckBox* throughX11_ = nullptr;
    CrtLook chosenLook() const;
    void setLook(const CrtLook& look);
    QCheckBox* turbo_ = nullptr;
    QCheckBox* plusPpi_ = nullptr;
    QCheckBox* tapeSounds_ = nullptr;
    QCheckBox* discSounds_ = nullptr;
    QCheckBox* flyback_ = nullptr;
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
    QCheckBox* enableMultiface_ = nullptr;
    QLabel* multifaceFile_ = nullptr;
    QString multifaceRom_;
    void setMultifaceRom(const QString& rom);
    void chooseMultifaceRom();
    QLabel* totalRam_ = nullptr;
    QTableWidget* roms_ = nullptr;
    QCheckBox* rom32_ = nullptr;
    QCheckBox* disableRoms_ = nullptr;
    QCheckBox* onlyLower0And7_ = nullptr;
    // The names of all 33 ROMs, shown or not.
    QString romNames_[1 + tuxape::Memory::kRomSlots];
    QString romFolder_;  // where a ROM's file was last taken

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

    QWidget* createExpansionsPage();
    void updateCards();
    QString chooseCardFile(const QString& title, const QString& now, const QString& kinds);
    void chooseXmemFlash();
    QComboBox* cards_[tuxape::kCardPlaces] = {};
    QGroupBox* xmemBox_ = nullptr;
    // Each switch is two buttons; the second is the one kept here.
    QRadioButton* xmem6128_ = nullptr;
    QRadioButton* xmemBootRom_ = nullptr;
    QRadioButton* xmemFree_ = nullptr;
    QRadioButton* xmemReadYes_ = nullptr;
    QLabel* xmemFile_ = nullptr;
    QString xmemFlash_;
    QStringList xmemPlaces_;  // what each of the flash's 32 places holds, in short
    std::map<int, QByteArray> xmemEdits_;
    std::vector<uint8_t> xmemImage() const;
    void readXmemPlaces();
    QLabel* romsNote_ = nullptr;
    void updateRomOwners();
    void chooseXmassDisc();
    QGroupBox* xmassBox_ = nullptr;
    QLabel* xmassFile_ = nullptr;
    QString xmassDisc_;
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
