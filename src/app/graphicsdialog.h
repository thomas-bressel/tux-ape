#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <QDialog>
#include <QImage>
#include <QRgb>

class Emulator;
class QComboBox;
class QLabel;
class QScrollBar;
class QSpinBox;
class QToolButton;

// WinAPE's Graphics Finder: a stretch of the machine's memory shown as
// tiles of a chosen width and height, to find where a program keeps its
// pictures, and a brush and a bucket to draw in them.
class GraphicsDialog : public QDialog {
    Q_OBJECT

public:
    // How a byte's bits make pixels, and how a tile's lines follow one
    // another in memory.
    enum Encoding {
        Cpc,      // bits as the Gate Array reads them, lines one after the other
        Screen,   // the same bits, lines as screen memory has them (&800 apart)
        Linear,   // a pixel's bits side by side, the leftmost pixel in the high bits
        Reverse,  // the same, the leftmost pixel in the low bits
    };
    // The screen mode pixels are read in: -1 for the one the machine is
    // in, 0 to 2, or 3 for a Plus's sprites, a byte to a pixel.
    static constexpr int kCurrentMode = -1;
    static constexpr int kSpriteMode = 3;

    explicit GraphicsDialog(Emulator* emulator, QWidget* parent = nullptr);

    void setView(int mode, int zoom, unsigned address, int width, int height, Encoding encoding);
    int mode() const;  // 0 to 3: what kCurrentMode stands for, if that is chosen
    int zoom() const;
    unsigned address() const;
    int tileWidth() const;   // bytes
    int tileHeight() const;  // lines
    Encoding encoding() const;

    // Looks at the machine again.
    void refresh();
    // The tiles in view, and one of them: where it starts in memory, its
    // size in pixels and the pen of one of its pixels.
    int tileCount() const { return columns_ * rows_; }
    int columns() const { return columns_; }
    unsigned tileAddress(int tile) const;
    int pixelsWide() const;
    int pen(int tile, int x, int y) const;
    QRgb colour(int pen) const;
    // The whole view as a picture, as shown.
    QImage picture() const;

    // The brush and the bucket: a pixel, or a patch of one colour, takes
    // the pen chosen. They write to the machine's memory.
    int currentPen() const { return pen_; }
    void setCurrentPen(int pen);
    void paintPixel(int tile, int x, int y);
    void fillFrom(int tile, int x, int y);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    unsigned byteAddress(int tile, int column, int line) const;
    int pensInMode() const;
    void setPixel(int tile, int x, int y, int pen);
    void layoutTiles();
    void drawView();
    bool locate(const QPoint& at, int* tile, int* x, int* y) const;
    void updateSwatches();

    Emulator* emulator_;
    QComboBox* modeBox_;
    QSpinBox* zoomBox_;
    QSpinBox* addressBox_;
    QSpinBox* widthBox_;
    QSpinBox* heightBox_;
    QComboBox* encodingBox_;
    QLabel* view_;
    QScrollBar* bar_;
    QLabel* addressLabel_;
    QToolButton* selectTool_;
    QToolButton* brushTool_;
    QToolButton* fillTool_;
    QLabel* penSwatch_;
    QWidget* swatches_;

    std::array<uint8_t, 0x10000> memory_ = {};
    std::array<QRgb, 16> colours_ = {};
    int machineMode_ = 1;
    int columns_ = 1;
    int rows_ = 1;
    int pen_ = 1;
};
