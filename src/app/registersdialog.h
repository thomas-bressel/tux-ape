#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

class Emulator;
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QScrollBar;
class QToolButton;

// WinAPE's Registers window: the Gate Array's palette, the sound chip's
// and the CRTC's registers, the counters inside the CRTC and the Gate
// Array, and, on a Plus, what the ASIC holds. It follows the machine as it
// runs. The palette, the sound chip's and the CRTC's registers can be
// typed over.
class RegistersDialog : public QDialog {
    Q_OBJECT

public:
    explicit RegistersDialog(Emulator* emulator, QWidget* parent = nullptr);

    void refresh();
    // A field by its name: "Palette0" to "Palette16" (the border), "PSG0"
    // to "PSG15", "CRTC0" to "CRTC15", "VCC", "R52", "HDC", "HCC", "VMA",
    // "Mode", "VLC", "VSC", "VTAC", "HSC", "VDUR", "ICSR", and on a Plus
    // "LRB", "CartBank", "DCSR", "X", "Y", "MagX", "MagY", "PRI", "SSS",
    // "SSC", "IVR", "SSSA", "Addr0" to "Addr2", "Pause0" to "Pause2".
    QString value(const QString& name) const;
    bool highlighted(const QString& name) const;
    // Gives a register of the palette, the sound chip or the CRTC a value
    // written in hexadecimal. False for another field or a bad value.
    bool setValue(const QString& name, const QString& hexText);
    // The ASIC's part, which the button at the bottom right opens.
    bool asicShown() const;
    void setAsicShown(bool shown);
    int sprite() const;
    void setSprite(int number);
    QString dmaInstruction(int channel) const;

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLineEdit* field(const QString& name, int digits, bool editable = false);
    void put(const QString& name, unsigned value, bool highlight = false);

    Emulator* emulator_;
    QHash<QString, QLineEdit*> fields_;
    QToolButton* asicButton_;
    QWidget* asicPane_;
    QLabel* colours_[32] = {};
    QCheckBox* unlocked_;
    QCheckBox* ramEnabled_;
    QGroupBox* spriteBox_;
    QLabel* spriteView_;
    QScrollBar* spriteBar_;
    QGroupBox* dmaBox_[3] = {};
    QCheckBox* dmaEnabled_[3] = {};
    QLabel* dmaNext_[3] = {};
};
