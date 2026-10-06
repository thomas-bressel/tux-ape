#pragma once

#include <QImage>
#include <QWidget>

class Emulator;

// The emulated monitor. Shows the pictures the emulator produces and feeds
// it the keys pressed while it has the focus.
class ScreenWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScreenWidget(Emulator* emulator, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // The picture currently on screen, at the size WinAPE saves it (768x540).
    QImage screenshot() const;

    // WinAPE's "Half Size Display": the picture asks for 384 x 270.
    void setHalfSize(bool half);
    bool halfSize() const { return halfSize_; }
    // WinAPE's "Render both lines". Without it, of the two screen lines a
    // CPC line takes, the second is left black.
    void setRenderBothLines(bool both);
    bool renderBothLines() const { return renderBoth_; }
    // WinAPE's "On-Screen Drive LED" and "Show Drive Cylinders": a light
    // in the picture's top right corner while a drive is at work, with the
    // drive's letter and, if asked, the cylinder its head is on.
    void setDriveLight(bool shown, bool withCylinder);
    // What the light shows: as Emulator::driveLight() gives it.
    void showDriveLight(int light);
    // Where the light is drawn, in the widget; empty when it is off.
    QRect driveLightRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    Emulator* emulator_;
    QImage image_;
    QImage striped_;  // image_ with black lines between its own, when asked for
    bool halfSize_ = false;
    bool renderBoth_ = true;
    bool driveLightShown_ = false;
    bool driveCylinderShown_ = false;
    int driveLight_ = -1;
    QRect pictureRect() const;
    bool numLock_ = true;

    void fetchFrame();
    void handleKey(QKeyEvent* event, bool pressed);
};
