#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

class Emulator;
class QCheckBox;
class QImage;
class QGroupBox;
class QLabel;
class QLineEdit;
class QScrollBar;
class QToolButton;

// WinAPE's Registers window: the Gate Array's palette, the sound chip's
// and the CRTC's registers, the counters inside the CRTC and the Gate
// Array, and, on a Plus, what the ASIC holds. It follows the machine as it
// runs. The palette, the sound chip's and the CRTC's registers, and a
// sprite's position and magnification, can be typed over; the ASIC can be
// unlocked or locked, and its page of registers shown in memory or not.
class RegistersDialog : public QDialog {
    Q_OBJECT

public:
    explicit RegistersDialog(Emulator* emulator, QWidget* parent = nullptr);

    void refresh();
    // WinAPE's "Row Highlight": while the box is ticked and the machine
    // stands still, the screen shows where the beam is; while it is ticked,
    // where the interrupts were taken in the picture.
    bool rowHighlight() const;
    void setRowHighlight(bool on);

signals:
    // The line of the picture and the column in it the beam is at, for
    // the screen to show; -1 and -1 when it is not to show any.
    void beamChanged(int line, int column);
    // Whether the screen shows where the Z80 took its interrupts: while
    // the window is up with "Row Highlight" ticked, the machine running or
    // not.
    void interruptMarksShown(bool on);

public:
    // A field by its name: "Palette0" to "Palette16" (the border), "PSG0"
    // to "PSG15", "CRTC0" to "CRTC15", "VCC", "R52", "HDC", "HCC", "VMA",
    // "Mode", "VLC", "VSC", "VTAC", "HSC", "VDUR", "ICSR", and on a Plus
    // "LRB", "CartBank", "DCSR", "X", "Y", "MagX", "MagY", "PRI", "SSS",
    // "SSC", "IVR", "SSSA", "Addr0" to "Addr2", "Pause0" to "Pause2".
    QString value(const QString& name) const;
    bool highlighted(const QString& name) const;
    // Gives a register of the palette, the sound chip or the CRTC, or the
    // position ("X", "Y") or the magnification ("MagX", "MagY": 0, 1, 2 or
    // 4) of the sprite shown, a value written in hexadecimal. False for
    // another field or a bad value.
    bool setValue(const QString& name, const QString& hexText);
    // The ASIC unlocked or locked, as a program does it: the sequence of
    // bytes written to the CRTC's register select port.
    void setUnlocked(bool on);
    // The ASIC's page of registers at &4000, or the lower ROM back where
    // it was before. The ASIC must be unlocked.
    bool setRamEnabled(bool on);
    // The sprite as the screen shows it: its magnification widens and
    // heightens its pixels, a line being twice as tall as a mode 2 pixel
    // is wide. Null if the machine is not a Plus.
    QImage spriteAsShown() const;
    // The ASIC's part, which the button at the bottom right opens.
    bool asicShown() const;
    void setAsicShown(bool shown);
    int sprite() const;
    void setSprite(int number);
    QString dmaInstruction(int channel) const;

protected:
    void showEvent(QShowEvent* event) override;

protected:
    void hideEvent(QHideEvent* event) override;

private:
    QLineEdit* field(const QString& name, int digits, bool editable = false);
    void put(const QString& name, unsigned value, bool highlight = false);

    Emulator* emulator_;
    QHash<QString, QLineEdit*> fields_;
    QToolButton* asicButton_;
    QCheckBox* rowHighlight_;
    QWidget* asicPane_;
    QLabel* colours_[32] = {};
    QCheckBox* unlocked_;
    QCheckBox* ramEnabled_;
    QGroupBox* spriteBox_;
    QLabel* spriteView_;
    QLabel* spriteShown_;
    QImage shownImage_;
    int lowerRomPosition_ = 0;  // RMR2's bits 4-3 before the page of registers was shown
    QScrollBar* spriteBar_;
    QGroupBox* dmaBox_[3] = {};
    QCheckBox* dmaEnabled_[3] = {};
    QLabel* dmaNext_[3] = {};
};
