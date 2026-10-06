// The Graphics Finder: memory shown as tiles in each screen mode and
// encoding, and the brush and the bucket that draw in them. Runs without a
// display (QT_QPA_PLATFORM=offscreen); no ROM is needed.
//
//   gui_graphics [prefix]   also saves a picture of the window as
//                           <prefix>graphics.png

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
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

    if (!prefix.isEmpty()) {
        for (int n = 0; n < 0x400; ++n)
            poke(0xA000 + static_cast<unsigned>(n), {(n * 37 ^ n >> 3) & 0xFF});
        graphics->setView(1, 3, 0xA000, 2, 8, G::Cpc);
        QTest::qWait(50);
        graphics->refresh();
        graphics->grab().save(prefix + "graphics.png");
    }
    return checkSummary("gui_graphics");
}
