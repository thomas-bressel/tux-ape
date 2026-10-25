#pragma once

#include <deque>
#include <vector>

#include <QImage>
#include <QWidget>

#include "core/dmp2000.h"

class Emulator;
class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QToolButton;

// The paper of one sheet as the DMP2000 left it: what was struck on it,
// and the characters, for a text that goes with the picture.
struct PrintedSheet {
    int number = 0;
    std::vector<tuxape::Dmp2000::Strike> strikes;
    std::vector<tuxape::Dmp2000::Printed> text;
    bool done = false;  // it has left the printer
};

// A sheet drawn: the paper (a cut sheet, or fan fold with its tractor
// holes and its perforations), the dots of its needles, and the head over
// the line it is at.
class SheetView : public QWidget {
    Q_OBJECT
public:
    // Dots to the inch of the picture kept of a sheet.
    static constexpr int kDotsPerInch = 200;

    explicit SheetView(QWidget* parent = nullptr);
    void setPaper(const tuxape::Dmp2000::Paper& paper);
    // The sheet shown, drawn again from what was struck on it.
    void showSheet(const PrintedSheet& sheet);
    // A column of needles struck on the sheet shown.
    void strike(const tuxape::Dmp2000::Strike& strike);
    // How hard the needles strike, 0 to 1: the paper thickness lever,
    // 1 set right for the paper. For the next strikes, and the sheets
    // shown again.
    void setImpression(double impression);
    // A column of needles struck on a picture of `dotsPerInch` (ink on
    // transparent, premultiplied ARGB; or in grey on white, Grayscale8):
    // the dots as the paper takes them, the same on the screen and in the
    // PDF.
    static void paint(QImage& picture, double dotsPerInch, const tuxape::Dmp2000::Strike& strike, double impression);
    double impression() const { return impression_; }
    // Where the head is, in inches on the sheet; hidden if `shown` is false.
    void setHead(double x, double y, bool shown);
    // Where the print line is, in the widget, for the window to scroll to.
    int lineInView() const;
    const QImage& picture() const { return picture_; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    tuxape::Dmp2000::Paper paper_;
    QImage picture_;  // the dots, grey on white, kDotsPerInch to the inch
    double headX_ = 0.0, headY_ = 0.0;
    bool headShown_ = false;
    double impression_ = 1.0;

    void blankPicture();
    double scale() const;  // widget pixels to the picture's
    QRectF headRect() const;
};

// The DMP2000's window: the sheet under its head, filling as the
// machine's clock reaches what the printer does (see core/dmp2000.h), the
// sheets printed before it, and the printer's lamps and buttons.
//
// It takes the printer's events from the emulator a few times a second
// and keeps those still to come until their time; it shows itself, without
// taking the keyboard from the machine, when needles start striking.
class PrinterWindow : public QWidget {
    Q_OBJECT
public:
    explicit PrinterWindow(Emulator* emulator, QWidget* parent = nullptr);

    // Events of the printer and the machine's time now: those whose time
    // has come are shown, the others kept. The window's timer gives it
    // the emulator's; a test gives its own.
    void feed(std::vector<tuxape::Dmp2000::Event> events, uint64_t now);
    void setPaper(const tuxape::Dmp2000::Paper& paper);

    const std::vector<PrintedSheet>& sheets() const { return sheets_; }
    int shownSheet() const { return shown_; }
    void showSheet(int index);
    SheetView* view() const { return view_; }

    // The sheets with something printed on them, a page each of a PDF: the
    // dots as the window has them (at `dotsPerInch`), and under them the
    // characters, unseen, to be found and copied.
    bool writePdf(const QString& path, int dotsPerInch = 600) const;
    // The print done ("Finish..."): the PDF written where the user says,
    // then the sheets taken away.
    void finish();
    void takeAway();

signals:
    // Needles have started striking after a while of none.
    void printingStarted();

private:
    Emulator* emulator_;
    tuxape::Dmp2000::Paper paper_;
    std::vector<PrintedSheet> sheets_;
    std::deque<tuxape::Dmp2000::Event> waiting_;  // their time not yet come
    int shown_ = -1;
    bool following_ = true;  // showing the sheet being printed
    bool striking_ = false;
    uint64_t lastStrike_ = 0;
    tuxape::Dmp2000::Motion carriage_{}, paperMotion_{};
    bool carriageKnown_ = false, paperKnown_ = false;

    SheetView* view_;
    QScrollArea* scroll_;
    QLabel* power_;
    QLabel* onlineLamp_;
    QLabel* paperOutLamp_;
    QPushButton* online_;
    QPushButton* formFeed_;
    QPushButton* lineFeed_;
    QPushButton* loadPaper_;
    QPushButton* finish_;
    QString pdfFolder_;  // where the last PDF went
    QToolButton* previous_;
    QToolButton* next_;
    QLabel* sheetLabel_;
    QTimer* timer_;

    void poll();
    void apply(const tuxape::Dmp2000::Event& event);
    PrintedSheet& sheet(int number);
    void updateHead(uint64_t now);
    void updateLamps();
    void updateSheetControls();
};
