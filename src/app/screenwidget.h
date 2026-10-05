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
    bool numLock_ = true;

    void fetchFrame();
    void handleKey(QKeyEvent* event, bool pressed);
};
