// The Graphics Finder: memory shown as tiles in each screen mode and
// encoding, and the brush and the bucket that draw in them. Runs without a
// display (QT_QPA_PLATFORM=offscreen); no ROM is needed.
//
//   gui_graphics [prefix]   also saves a picture of the window as
//                           <prefix>graphics.png

#include <vector>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
#include "debuggerdialog.h"
#include "emulator.h"
#include "graphicsdialog.h"
#include "mainwindow.h"
#include "settings.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();
    const auto poke = [&](unsigned address, std::initializer_list<int> bytes) {
        emulator.withMachine([&](tuxape::Cpc& cpc) {
            for (const int byte : bytes)
                cpc.memory().write(static_cast<uint16_t>(address++), static_cast<uint8_t>(byte));
        });
    };
    const auto peek = [&](unsigned address) {
        return emulator.withMachine([&](tuxape::Cpc& cpc) { return cpc.memory().readRam(static_cast<uint16_t>(address)); });
    };
    // Four inks, and two lines of two bytes at &9000.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        const int inks[4] = {0x14, 0x0A, 0x13, 0x0C};
        for (int pen = 0; pen < 4; ++pen) {
            cpc.out(0x7F00, static_cast<uint8_t>(pen));
            cpc.out(0x7F00, static_cast<uint8_t>(0x40 | inks[pen]));
        }
    });
    poke(0x9000, {0xF0, 0x0F, 0xFF, 0x00, 0xAA, 0xA0});

    QAction* show = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text() == "&Find Graphics")
            show = action;
    CHECK(show && show->isEnabled());
    CHECK(window.graphics() == nullptr);
    if (!show)
        return checkSummary("gui_graphics");
    show->trigger();
    GraphicsDialog* graphics = window.graphics();
    CHECK(graphics && graphics->isVisible());
    if (!graphics)
        return checkSummary("gui_graphics");
    using G = GraphicsDialog;
    auto* modes = graphics->findChild<QComboBox*>("cbMode");
    auto* encodings = graphics->findChild<QComboBox*>("cbEncoding");
    CHECK(modes && modes->count() == 5 && modes->itemText(0) == "Current" && modes->itemText(4) == "Sprite");
    CHECK(encodings && encodings->count() == 4 && encodings->itemText(1) == "Screen");

    // Mode 1, as the Gate Array reads a byte: two bits of each half.
    graphics->setView(1, 3, 0x9000, 2, 2, G::Cpc);
    CHECK(graphics->mode() == 1 && graphics->zoom() == 3 && graphics->address() == 0x9000);
    CHECK(graphics->tileWidth() == 2 && graphics->tileHeight() == 2 && graphics->encoding() == G::Cpc);
    CHECK_EQ(graphics->pixelsWide(), 8);
    CHECK(graphics->tileCount() >= 2);
    CHECK_EQ(graphics->tileAddress(0), 0x9000);
    CHECK_EQ(graphics->tileAddress(1), 0x9004);
    CHECK(graphics->pen(0, 0, 0) == 1 && graphics->pen(0, 3, 0) == 1 && graphics->pen(0, 4, 0) == 2);
    CHECK(graphics->pen(0, 0, 1) == 3 && graphics->pen(0, 7, 1) == 0);
    const QRgb yellow = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.gateArray().colour(0x0A) | 0xFF000000; });
    CHECK(graphics->colour(1) == yellow && graphics->colour(0) != yellow);
    // The picture: tiles side by side, a white line between them.
    const QImage picture = graphics->picture();
    CHECK_EQ(picture.width(), graphics->columns() * (8 * 3 + 1));
    CHECK(picture.pixel(0, 0) == yellow && picture.pixel(11, 2) == yellow);
    CHECK(picture.pixel(12, 0) == graphics->colour(2) && picture.pixel(0, 3) == graphics->colour(3));
    CHECK(picture.pixel(24, 0) == qRgb(255, 255, 255));
    // A pixel's bits side by side, from the left or from the right.
    graphics->setView(1, 3, 0x9000, 2, 2, G::Linear);
    CHECK(graphics->pen(0, 0, 0) == 3 && graphics->pen(0, 1, 0) == 3 && graphics->pen(0, 2, 0) == 0);
    graphics->setView(1, 3, 0x9000, 2, 2, G::Reverse);
    CHECK(graphics->pen(0, 0, 0) == 0 && graphics->pen(0, 2, 0) == 3 && graphics->pen(0, 3, 0) == 3);
    // Mode 0: two pixels of sixteen colours; mode 2: eight of two.
    graphics->setView(0, 3, 0x9004, 1, 1, G::Cpc);
    CHECK(graphics->pixelsWide() == 2 && graphics->pen(0, 0, 0) == 15 && graphics->pen(0, 1, 0) == 0);
    graphics->setView(0, 3, 0x9004, 1, 1, G::Linear);
    CHECK(graphics->pen(0, 0, 0) == 10 && graphics->pen(0, 1, 0) == 10);
    graphics->setView(2, 3, 0x9005, 1, 1, G::Cpc);
    CHECK(graphics->pixelsWide() == 8 && graphics->pen(0, 0, 0) == 1 && graphics->pen(0, 1, 0) == 0 && graphics->pen(0, 2, 0) == 1);
    // A Plus's sprites: a byte to a pixel.
    graphics->setView(G::kSpriteMode, 3, 0x9000, 16, 16, G::Cpc);
    CHECK(graphics->mode() == G::kSpriteMode && graphics->pixelsWide() == 16);
    CHECK(graphics->pen(0, 0, 0) == 0 && graphics->pen(0, 1, 0) == 15 && graphics->pen(0, 4, 0) == 10);
    // The machine's own mode.
    graphics->setView(G::kCurrentMode, 3, 0x9000, 2, 2, G::Cpc);
    const int machineMode = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.gateArray().mode() & 3; });
    CHECK_EQ(graphics->mode(), machineMode == 3 ? 0 : machineMode);

    // As on the screen: lines &800 apart, the next row of characters &50 on.
    graphics->setView(1, 2, 0xC000, 2, 8, G::Screen);
    CHECK_EQ(graphics->tileAddress(0), 0xC000);
    CHECK_EQ(graphics->tileAddress(1), 0xC002);
    poke(0xC800, {0x0F});
    poke(0xF800, {0xFF});
    graphics->refresh();
    CHECK(graphics->pen(0, 0, 0) == 0 && graphics->pen(0, 0, 1) == 2 && graphics->pen(0, 0, 7) == 3);
    if (graphics->tileCount() > graphics->columns())
        CHECK_EQ(graphics->tileAddress(graphics->columns()), 0xC050);

    // The brush gives a pixel the pen chosen; the bucket, a patch.
    graphics->setView(1, 3, 0x9000, 2, 2, G::Cpc);
    graphics->setCurrentPen(2);
    CHECK_EQ(graphics->currentPen(), 2);
    graphics->paintPixel(0, 0, 0);
    CHECK_EQ(peek(0x9000), 0x78);
    CHECK(graphics->pen(0, 0, 0) == 2 && graphics->pen(0, 1, 0) == 1);
    graphics->setCurrentPen(1);
    graphics->fillFrom(0, 2, 1);
    CHECK_EQ(peek(0x9002), 0xF0);
    CHECK_EQ(peek(0x9003), 0x00);
    // The patch runs round corners: pen 1 of the top line joins the one
    // just made below it.
    graphics->setCurrentPen(3);
    graphics->fillFrom(0, 3, 0);
    CHECK_EQ(peek(0x9000), 0x7F);
    CHECK_EQ(peek(0x9002), 0xFF);
    CHECK_EQ(peek(0x9001), 0x0F);
    graphics->setCurrentPen(9);  // more than the mode has
    CHECK_EQ(graphics->currentPen(), 3);
    graphics->paintPixel(99999, 0, 0);
    graphics->fillFrom(0, 99, 0);
    // In mode 0 and in the linear encodings too.
    graphics->setView(0, 3, 0x9010, 1, 1, G::Cpc);
    graphics->setCurrentPen(15);
    graphics->paintPixel(0, 1, 0);
    CHECK_EQ(peek(0x9010), 0x55);
    graphics->setView(1, 3, 0x9011, 1, 1, G::Linear);
    graphics->setCurrentPen(2);
    graphics->paintPixel(0, 1, 0);
    CHECK_EQ(peek(0x9011), 0x20);
    graphics->setView(2, 3, 0x9012, 1, 1, G::Reverse);
    graphics->setCurrentPen(1);
    graphics->paintPixel(0, 1, 0);
    CHECK_EQ(peek(0x9012), 0x02);

    // The address box moves the view, as the bar under it does.
    auto* address = graphics->findChild<QSpinBox*>("seAddress");
    CHECK(address && address->displayIntegerBase() == 16);
    if (address)
        address->setValue(0x72AD);
    CHECK_EQ(graphics->tileAddress(0), 0x72AD);

    // ---- The tiles selected, and what the right button's menu does ----
    // Tiles of four pixels by four, one byte to a line. The first has pen 1
    // at (1,0) and pen 2 at (3,2).
    poke(0x9100, {0x40, 0x00, 0x01, 0x00, 0xC0, 0xC0, 0x03, 0x03, 0xFF, 0xFF, 0xFF, 0xFF});
    graphics->setView(1, 3, 0x9100, 1, 4, G::Cpc);
    const auto bytes = [&](unsigned at) {
        return std::vector<int>{peek(at), peek(at + 1), peek(at + 2), peek(at + 3)};
    };
    const QList<QAction*> entries = graphics->tileActions();
    QStringList texts;
    for (const QAction* entry : entries)
        texts << entry->text();
    CHECK(texts == (QStringList{"&Copy", "Copy &Merged", "Mark as &data", "Flip &Horizontal", "Flip &Vertical",
                                "&Rotate Clockwise", "Rotate &Anti-Clockwise"}));
    const auto entry = [&](const char* name) { return graphics->findChild<QAction*>(name); };
    CHECK(entry("Copy1") && entry("Copy1")->shortcut() == QKeySequence(QKeySequence::Copy));
    // Nothing selected: nothing to do.
    CHECK(graphics->selectionStart() == -1 && graphics->selectionCount() == 0);
    for (const QAction* each : entries)
        CHECK(!each->isEnabled());
    graphics->turnSelection(G::FlipHorizontal);
    CHECK(bytes(0x9100) == (std::vector<int>{0x40, 0x00, 0x01, 0x00}));

    // A click selects a tile; Shift and a click, as far as another.
    auto* view = graphics->findChild<QLabel*>("GraphicsView");
    CHECK(view != nullptr);
    if (!view)
        return checkSummary("gui_graphics");
    const auto place = [&](int tile) {
        return QPoint(view->frameWidth() + (tile % graphics->columns()) * 13 + 5,
                      view->frameWidth() + (tile / graphics->columns()) * 13 + 5);
    };
    QTest::mouseClick(view, Qt::LeftButton, {}, place(1));
    CHECK(graphics->selectionStart() == 1 && graphics->selectionCount() == 1);
    for (const QAction* each : entries)
        CHECK(each->isEnabled());
    QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, place(2));
    CHECK(graphics->selectionStart() == 1 && graphics->selectionCount() == 2);
    QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, place(0));
    CHECK(graphics->selectionStart() == 0 && graphics->selectionCount() == 2);
    // The right button keeps a selection it falls in, and makes one of
    // the tile under it otherwise.
    QTest::mouseClick(view, Qt::RightButton, {}, place(1));
    CHECK(graphics->selectionStart() == 0 && graphics->selectionCount() == 2);
    QTest::mouseClick(view, Qt::RightButton, {}, place(2));
    CHECK(graphics->selectionStart() == 2 && graphics->selectionCount() == 1);
    QTest::mouseClick(view, Qt::LeftButton, {}, place(0));
    CHECK(graphics->selectionStart() == 0 && graphics->selectionCount() == 1);

    // Flipped and turned, in the machine's memory; and back.
    entry("FlipHorizontal1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x20, 0x00, 0x08, 0x00}));
    CHECK(graphics->pen(0, 2, 0) == 1 && graphics->pen(0, 0, 2) == 2);
    entry("FlipHorizontal1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x40, 0x00, 0x01, 0x00}));
    entry("FlipVertical1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x00, 0x01, 0x00, 0x40}));
    entry("FlipVertical1")->trigger();
    entry("Rotate1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x00, 0x10, 0x00, 0x04}));
    entry("RotateAntiClockwise1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x40, 0x00, 0x01, 0x00}));
    entry("RotateAntiClockwise1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x02, 0x00, 0x80, 0x00}));
    entry("Rotate1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x40, 0x00, 0x01, 0x00}));
    CHECK(bytes(0x9104) == (std::vector<int>{0xC0, 0xC0, 0x03, 0x03}));  // the others are left alone
    // Several tiles: each by itself.
    graphics->select(0, 2);
    entry("FlipHorizontal1")->trigger();
    CHECK(bytes(0x9100) == (std::vector<int>{0x20, 0x00, 0x08, 0x00}));
    CHECK(bytes(0x9104) == (std::vector<int>{0x30, 0x30, 0x0C, 0x0C}));
    entry("FlipHorizontal1")->trigger();

    // Copy: the tiles as a picture, a pixel to a pixel, a line between
    // them; and their bytes as text for the assembler. Merged: one picture.
    entry("Copy1")->trigger();
    const QRgb white = qRgb(255, 255, 255);
    QImage copied = QGuiApplication::clipboard()->image();
    CHECK(copied.size() == QSize(9, 4));
    CHECK(copied.pixel(1, 0) == graphics->colour(1) && copied.pixel(0, 0) == graphics->colour(0));
    CHECK(copied.pixel(3, 2) == graphics->colour(2) && copied.pixel(4, 0) == white);
    CHECK(copied.pixel(5, 0) == graphics->colour(1) && copied.pixel(8, 3) == graphics->colour(2));
    CHECK(QGuiApplication::clipboard()->text() ==
          "; #9100\ndb #40\ndb #00\ndb #01\ndb #00\n; #9104\ndb #C0\ndb #C0\ndb #03\ndb #03\n");
    entry("CopyMerged1")->trigger();
    copied = QGuiApplication::clipboard()->image();
    CHECK(copied.size() == QSize(8, 4));
    CHECK(copied.pixel(1, 0) == graphics->colour(1) && copied.pixel(4, 0) == graphics->colour(1));
    CHECK(graphics->selectionPicture(false).size() == QSize(9, 4) && graphics->selectionPicture(true).size() == QSize(8, 4));

    // Mark as data: the debugger shows those bytes as data, without
    // having to be on the screen.
    graphics->select(1, 2);
    entry("MarkAsData")->trigger();
    CHECK(window.debugger() != nullptr);
    if (window.debugger()) {
        CHECK(!window.debugger()->isVisible());
        const auto areas = window.debugger()->dataAreas();
        CHECK_EQ(areas.size(), 1);
        CHECK(!areas.empty() && areas[0].start == 0x9104 && areas[0].size == 8 && !areas[0].words);
        // Marked again, further on: one area in place of the other.
        graphics->select(2, 2);
        entry("MarkAsData")->trigger();
        const auto again = window.debugger()->dataAreas();
        CHECK(again.size() == 1 && again[0].start == 0x9108 && again[0].size == 8);
    }

    // The arrow keys move a selection of one, and the view with it at its
    // edges.
    graphics->select(0);
    QTest::keyClick(view, Qt::Key_Right);
    CHECK(graphics->selectionStart() == 1 && graphics->selectionCount() == 1);
    QTest::keyClick(view, Qt::Key_Left);
    CHECK_EQ(graphics->selectionStart(), 0);
    const int columns = graphics->columns();
    QTest::keyClick(view, Qt::Key_Left);
    CHECK_EQ(graphics->address(), 0x9100u - static_cast<unsigned>(columns) * 4);
    CHECK(graphics->selectionStart() == columns - 1 && graphics->selectionCount() == 1);
    CHECK_EQ(graphics->tileAddress(graphics->selectionStart()), 0x90FC);
    QTest::keyClick(view, Qt::Key_Right);
    CHECK_EQ(graphics->tileAddress(graphics->selectionStart()), 0x9100);
    // Up: the tile above, in view; up again: the view goes up a row.
    QTest::keyClick(view, Qt::Key_Up);
    CHECK_EQ(graphics->address(), 0x9100u - static_cast<unsigned>(columns) * 4);
    CHECK_EQ(graphics->selectionStart(), 0);
    QTest::keyClick(view, Qt::Key_Up);
    CHECK_EQ(graphics->address(), 0x9100u - static_cast<unsigned>(columns) * 8);
    CHECK_EQ(graphics->selectionStart(), 0);
    QTest::keyClick(view, Qt::Key_Down);
    CHECK_EQ(graphics->tileAddress(graphics->selectionStart()), 0x9100u - static_cast<unsigned>(columns) * 4);
    // A change of the view selects none.
    graphics->setView(1, 3, 0x9100, 2, 4, G::Cpc);
    CHECK(graphics->selectionStart() == -1 && !entry("Copy1")->isEnabled());
    // Tiles that are not as wide as they are high are flipped, not turned.
    graphics->select(0);
    CHECK(!graphics->canRotate() && entry("FlipVertical1")->isEnabled());
    CHECK(!entry("Rotate1")->isEnabled() && !entry("RotateAntiClockwise1")->isEnabled());
    graphics->turnSelection(G::RotateClockwise);
    CHECK(bytes(0x9100) == (std::vector<int>{0x40, 0x00, 0x01, 0x00}));
    graphics->turnSelection(G::FlipHorizontal);  // eight pixels wide now: two bytes to a line
    CHECK(bytes(0x9100) == (std::vector<int>{0x00, 0x20, 0x00, 0x08}));
    graphics->turnSelection(G::FlipHorizontal);
    graphics->clearSelection();
    CHECK(graphics->selectionCount() == 0 && graphics->selectionPicture(true).isNull());

    // The pen and the paper: the left button chooses and draws with the
    // one, the right button with the other.
    graphics->setView(1, 3, 0x9100, 1, 4, G::Cpc);
    auto* penSwatch = graphics->findChild<QLabel*>("PenSwatch");
    auto* paperSwatch = graphics->findChild<QLabel*>("PaperSwatch");
    auto* brush = graphics->findChild<QToolButton*>("bBrush");
    auto* bucket = graphics->findChild<QToolButton*>("bFill");
    auto* arrow = graphics->findChild<QToolButton*>("bSelect");
    CHECK(penSwatch && paperSwatch && brush && bucket && arrow);
    if (penSwatch && paperSwatch && brush && bucket && arrow) {
        QTest::mouseClick(graphics->findChild<QLabel*>("pen3"), Qt::LeftButton);
        QTest::mouseClick(graphics->findChild<QLabel*>("pen2"), Qt::RightButton);
        CHECK(graphics->currentPen() == 3 && graphics->currentPaper() == 2);
        CHECK(penSwatch->palette().color(QPalette::Window).rgb() == graphics->colour(3));
        CHECK(paperSwatch->palette().color(QPalette::Window).rgb() == graphics->colour(2));
        brush->click();
        // Pixel (0,1) of the first tile: pen 3 is both of its bits, pen 2
        // the lower one.
        const QPoint pixel = QPoint(view->frameWidth() + 1, view->frameWidth() + 4);
        QTest::mouseClick(view, Qt::LeftButton, {}, pixel);
        CHECK_EQ(peek(0x9101), 0x88);
        QTest::mouseClick(view, Qt::RightButton, {}, pixel);
        CHECK_EQ(peek(0x9101), 0x08);
        CHECK(graphics->selectionCount() == 0);  // the brush selects nothing
        // The bucket too: the rest of the line, then the whole of it.
        bucket->click();
        graphics->setCurrentPaper(0);
        QTest::mouseClick(view, Qt::RightButton, {}, pixel);
        CHECK_EQ(peek(0x9101), 0x00);
        graphics->paintPixel(0, 0, 1, true);
        graphics->fillFrom(0, 0, 3, true);
        CHECK_EQ(peek(0x9101), 0x00);
        arrow->click();
    }

    if (!prefix.isEmpty()) {
        for (int n = 0; n < 0x400; ++n)
            poke(0xA000 + static_cast<unsigned>(n), {(n * 37 ^ n >> 3) & 0xFF});
        graphics->setView(1, 3, 0xA000, 2, 8, G::Cpc);
        QTest::qWait(50);
        graphics->refresh();
        graphics->select(12, 2);
        graphics->grab().save(prefix + "graphics.png");
    }
    return checkSummary("gui_graphics");
}
