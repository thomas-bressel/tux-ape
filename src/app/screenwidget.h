#pragma once

#include <QImage>
#include <QWidget>

#include "crtlook.h"

class QPainter;

class CrtView;
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
    // The picture as the user sees it: through the shader when that is
    // on, no wider than asked.
    QImage shownPicture(int widest = 960) const;

    // WinAPE's "Half Size Display": the picture asks for 384 x 270.
    void setHalfSize(bool half);
    bool halfSize() const { return halfSize_; }
    // WinAPE's "Render both lines". Without it, of the two screen lines a
    // CPC line takes, the second is left black.
    void setRenderBothLines(bool both);
    bool renderBothLines() const { return renderBoth_; }
    // WinAPE's "PAL Emulation": each pixel runs a little into its
    // neighbours along the line, and of the two screen lines a CPC line
    // takes, the second is at half brightness.
    void setPalEmulation(bool pal);
    bool palEmulation() const { return pal_; }
    // TuxAPE's own: the picture as a CTM644 monitor shows it (see CrtView),
    // drawn by the graphics card. Where there is none to do it the picture
    // stays as it was. A colour tube has a mask, a green or grey one none.
    void setCrtShader(bool on, bool colourTube = true, const CrtLook& look = {});
    bool crtShader() const { return crt_ != nullptr; }
    CrtView* crtView() const { return crt_; }
    // In step with a 50 Hz screen: where the screen the window is on shows
    // fifty pictures a second (or a hundred), and the graphics card draws
    // the picture, the machine runs one frame to each picture shown, and
    // what scrolls does so as smoothly as on a monitor. Any other screen
    // is left alone. How many of the screen's pictures go to a frame; 0
    // when not in step.
    void setDisplaySync(bool wanted);
    bool displaySync() const { return syncWanted_; }
    int displaySyncEvery() const { return swapsPerFrame_; }
    // What it would be on a screen of that refresh rate.
    static int syncEveryFor(double hertz);
    // WinAPE's "AMX Mouse": the pointer's moves over the picture, and the
    // buttons pressed there, are the CPC's mouse's.
    void setAmxMouse(bool on);
    // WinAPE's "On-Screen Drive LED" and "Show Drive Cylinders": a light
    // in the picture's top right corner while a drive is at work, with the
    // drive's letter and, if asked, the cylinder its head is on.
    void setDriveLight(bool shown, bool withCylinder);
    // What the light shows: as Emulator::driveLight() gives it.
    void showDriveLight(int light);
    // Where the light is drawn, in the widget; empty when it is off.
    QRect driveLightRect() const;
    // WinAPE's "Row Highlight": the line of the picture and the column in
    // it (0 to 767) the beam is at, shown as a line across and a line
    // down; -1 for none.
    void setBeamMarker(int line, int column);
    int beamMarkerLine() const { return beamLine_; }
    int beamMarkerColumn() const { return beamColumn_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

private:
    Emulator* emulator_;
    QImage image_;
    QImage striped_;  // image_ with black lines between its own, when asked for
    bool halfSize_ = false;
    bool renderBoth_ = true;
    bool pal_ = false;
    bool amxMouse_ = false;
    QPoint amxLast_;      // where the pointer was when last looked at
    bool amxSeen_ = false;
    void amxButtons(QMouseEvent* event);
    bool driveLightShown_ = false;
    bool driveCylinderShown_ = false;
    int driveLight_ = -1;
    QRect pictureRect() const;
    void paintDriveLight(QPainter& painter, const QRect& light) const;
    void paintBeamMarker(QPainter& painter, const QRect& picture) const;
    int beamLine_ = -1;
    int beamColumn_ = -1;
    CrtView* crt_ = nullptr;
    bool syncWanted_ = true;
    int swapsPerFrame_ = 0;
    int swaps_ = 0;
    void updateDisplayClock();
    bool numLock_ = true;

    void fetchFrame();
    void handleKey(QKeyEvent* event, bool pressed);
};
