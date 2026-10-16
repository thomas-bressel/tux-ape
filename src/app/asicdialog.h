#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QString>

#include "core/asic.h"

class Emulator;
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QScrollBar;
class QTimer;
class SpriteBoard;
class SpriteMap;
struct SpriteState;

// What the Plus's ASIC holds, in a window of its own: the part WinAPE's
// Registers window opens with its ASIC button. The palette and the
// sprites' palette, the lock and RMR2, the raster interrupt and the split
// screen, a sprite's registers, the three sound lists. TuxAPE's own: the
// sixteen hardware sprites as they are drawn, and the picture with each
// where it stands, as the machine runs. A sprite's position and
// magnification can be typed over; the ASIC can be unlocked or locked,
// and its page of registers shown in memory or not.
class AsicDialog : public QDialog {
    Q_OBJECT

public:
    explicit AsicDialog(Emulator* emulator, QWidget* parent = nullptr);
    ~AsicDialog() override;

    void refresh();

    // A field by its name: "LRB", "CartBank", "DCSR", "X", "Y", "MagX",
    // "MagY", "PRI", "SSS", "SSC", "IVR", "SSSA", "Addr0" to "Addr2",
    // "Pause0" to "Pause2".
    QString value(const QString& name) const;
    // Gives the position ("X", "Y") or the magnification ("MagX", "MagY":
    // 0, 1, 2 or 4) of the sprite shown a value written in hexadecimal,
    // written where a program writes it. False for another field, a bad
    // value, or a machine that is not a Plus.
    bool setValue(const QString& name, const QString& hexText);
    // The ASIC unlocked or locked, as a program does it: the sequence of
    // bytes written to the CRTC's register select port.
    void setUnlocked(bool on);
    // The ASIC's page of registers at &4000, or the lower ROM back where
    // it was before. The ASIC must be unlocked.
    bool setRamEnabled(bool on);
    // The sprite whose registers are shown, 0 to 15.
    int sprite() const;
    void setSprite(int number);
    // That sprite as the screen shows it: its magnification widens and
    // heightens its pixels, a line being twice as tall as a mode 2 pixel
    // is wide.
    QImage spriteAsShown() const;
    QString dmaInstruction(int channel) const;
    // Whether the machine was a Plus when the window last looked.
    bool plus() const { return plus_; }

    // What the sprites' part shows, for those who look: a sprite's place
    // on the board of sixteen, and on the picture of the screen, in the
    // widgets' own pixels; an empty rectangle for a sprite not on it.
    QRect boardCell(int sprite) const;
    QRect mapPlace(int sprite) const;

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLineEdit* field(const QString& name, int digits, bool editable = false);
    void put(const QString& name, unsigned value);

    Emulator* emulator_;
    // What the board and the map draw from.
    std::unique_ptr<SpriteState> state_;
    QHash<QString, QLineEdit*> fields_;
    QLabel* notPlus_;
    QWidget* body_;
    QLabel* colours_[32] = {};
    QCheckBox* unlocked_;
    QCheckBox* ramEnabled_;
    QGroupBox* spriteBox_;
    QLabel* spriteView_;
    QLabel* spriteShown_;
    QScrollBar* spriteBar_;
    QGroupBox* dmaBox_[3] = {};
    QCheckBox* dmaEnabled_[3] = {};
    QLabel* dmaNext_[3] = {};
    SpriteBoard* board_;
    SpriteMap* map_;
    QTimer* timer_;
    QImage shownImage_;
    bool plus_ = false;
    int lowerRomPosition_ = 0;  // RMR2's bits 4-3 before the page of registers was shown
};
