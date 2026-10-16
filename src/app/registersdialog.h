#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

class Emulator;
class QCheckBox;
class QLineEdit;
class QToolButton;

// WinAPE's Registers window, which TuxAPE calls CRTC Registers: the Gate
// Array's palette, the sound chip's and the CRTC's registers, the counters
// inside the CRTC and the Gate Array. It follows the machine as it runs.
// The palette, the sound chip's and the CRTC's registers can be typed
// over. On a Plus, its ASIC button opens the ASIC's registers, which have
// a window of their own (AsicDialog).
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
    // The ASIC button was pressed.
    void asicRequested();

public:
    // A field by its name: "Palette0" to "Palette16" (the border), "PSG0"
    // to "PSG15", "CRTC0" to "CRTC15", "VCC", "R52", "HDC", "HCC", "VMA",
    // "Mode", "VLC", "VSC", "VTAC", "HSC", "VDUR", "ICSR".
    QString value(const QString& name) const;
    bool highlighted(const QString& name) const;
    // Gives a register of the palette, the sound chip or the CRTC a value
    // written in hexadecimal. False for another field or a bad value.
    bool setValue(const QString& name, const QString& hexText);

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
};
