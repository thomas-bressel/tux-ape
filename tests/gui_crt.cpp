// The CTM644 shader: what the graphics card makes of a picture. It needs a
// graphics card to run at all: without one (a build machine, most often)
// the test is skipped. QT_QPA_PLATFORM=offscreen draws without a window
// where the display's server lets it.
//
//   gui_crt [prefix [frame.png [width]]]   also saves the picture drawn, of
//                                          the test's own card or of the
//                                          frame given, as <prefix>crt.png
//   gui_crt --time                         says how long the graphics card
//                                          takes to draw a picture

#include <cmath>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "check.h"
#include "core/setup.h"
#include "crtview.h"
#include "emulator.h"
#include "mainwindow.h"
#include "screenwidget.h"
#include "settings.h"
#include "setupdialog.h"

namespace {

double light(QRgb pixel)
{
    const auto lin = [](int c) { return std::pow(c / 255.0, 2.2); };
    return 0.2126 * lin(qRed(pixel)) + 0.7152 * lin(qGreen(pixel)) + 0.0722 * lin(qBlue(pixel));
}

// The mean light of a patch.
double meanLight(const QImage& image, const QRect& patch)
{
    double sum = 0;
    for (int y = patch.top(); y <= patch.bottom(); ++y)
        for (int x = patch.left(); x <= patch.right(); ++x)
            sum += light(image.pixel(x, y));
    return sum / (patch.width() * patch.height());
}

// A frame of the CPC's: 768 texels by 270 lines, black, with a white
// square in its middle, a grey band above and thin lines beside.
QImage card()
{
    QImage frame(768, 270, QImage::Format_RGB32);
    frame.fill(Qt::black);
    QPainter painter(&frame);
    painter.fillRect(288, 87, 192, 96, Qt::white);
    painter.fillRect(96, 20, 576, 30, QColor(128, 128, 128));
    for (int x = 520; x < 640; x += 16)
        painter.fillRect(x, 87, 2, 96, Qt::white);
    return frame;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (prefix == "--time") {
        for (const QSize size : {QSize(1440, 1080), QSize(2880, 2160)}) {
            QString card;
            const double each = CrtRenderer::measure(size, &card);
            if (each < 0)
                return std::printf("no graphics card to draw with\n"), 77;
            std::printf("%d x %d: %.2f ms a picture, %.1f %% of the 20 ms a CPC frame lasts (%s)\n", size.width(),
                        size.height(), each, each * 5.0, qPrintable(card));
        }
        return 0;
    }

    // The mask's pitch follows the size of the picture: none when it is
    // too small, three pixels at least, 0.85 of a mode 1 pixel beyond.
    CHECK_EQ(CrtRenderer::triadPixels(384), 0);
    CHECK_EQ(CrtRenderer::triadPixels(768), 3);
    CHECK_EQ(CrtRenderer::triadPixels(1440), 3);
    CHECK_EQ(CrtRenderer::triadPixels(2880), 6);


    QImage frame = card();
    if (argc > 2) {
        // A picture of the emulator's: its lines may be doubled.
        QImage given(QString::fromLocal8Bit(argv[2]));
        if (!given.isNull())
            frame = given.scaled(768, 270, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    }
    // Drawn without a window. Where that cannot be done there is nothing
    // to test.
    // The frame given may be drawn at another width than the test's.
    const int wide = argc > 3 ? std::max(384, QString::fromLocal8Bit(argv[3]).toInt()) : 1440;
    const QImage drawn = CrtRenderer::available() ? CrtRenderer::render(frame, QSize(wide, wide * 3 / 4)) : QImage();
    if (drawn.isNull()) {
        std::printf("no graphics card to draw with: the shader was not tested\n");
        const int result = checkSummary("gui_crt");
        return result != 0 ? result : 77;
    }
    CHECK(drawn.size() == QSize(wide, wide * 3 / 4));
    if (!prefix.isEmpty())
        drawn.save(prefix + "crt.png");
    if (argc > 2)
        return checkSummary("gui_crt");

    // A frame's texel is 1.875 pixels wide here, a line 4 pixels high.
    const auto at = [](int texel, int line, int wide, int high) {
        return QRect(static_cast<int>(texel * 1.875), line * 4, static_cast<int>(wide * 1.875), high * 4);
    };
    const double white = meanLight(drawn, at(340, 110, 80, 50));
    const double besideSquare = meanLight(drawn, at(488, 110, 6, 50));  // just right of the square
    const double farAway = meanLight(drawn, at(100, 215, 60, 25));      // down in a corner of the black
    const double corner = meanLight(drawn, QRect(0, 0, 12, 12));
    // The square is bright, its surroundings are lit by it, and less and
    // less so further away; the far black is not far from black.
    CHECK(white > 0.35);
    CHECK(besideSquare > 0.02 * white && besideSquare < 0.25 * white);
    CHECK(farAway < besideSquare * 0.5 && farAway < 0.02 * white);
    // Outside the rounded corners of the glass: nothing.
    CHECK(corner < 0.001);
    // Scan lines: down a column of the grey band, light and dark follow one
    // another every four pixels.
    {
        double peak = 0, trough = 1;
        for (int y = 30 * 4; y < 30 * 4 + 16; ++y) {
            const double row = meanLight(drawn, QRect(600, y, 90, 1));
            peak = std::max(peak, row);
            trough = std::min(trough, row);
        }
        CHECK(trough < peak * 0.75);
        CHECK(trough > peak * 0.05);
    }
    // The mask: across the white square, one pixel in three is reddish, the
    // next greenish, the next bluish.
    {
        int red = 0, green = 0, blue = 0;
        const int y = 135 * 4 + 2;
        for (int x = 660; x < 660 + 90; ++x) {
            const QRgb pixel = drawn.pixel(x, y);
            red += qRed(pixel) > qGreen(pixel) && qRed(pixel) > qBlue(pixel);
            green += qGreen(pixel) > qRed(pixel) && qGreen(pixel) > qBlue(pixel);
            blue += qBlue(pixel) > qRed(pixel) && qBlue(pixel) > qGreen(pixel);
        }
        CHECK(red >= 20 && green >= 20 && blue >= 20);
    }
    // A green or a grey tube has no mask: the white square is white.
    {
        const QImage plain = CrtRenderer::render(frame, QSize(1440, 1080), false);
        int tinted = 0;
        const int y = 135 * 4 + 2;
        for (int x = 660; x < 660 + 90; ++x) {
            const QRgb pixel = plain.pixel(x, y);
            tinted += std::abs(qRed(pixel) - qGreen(pixel)) > 12;
        }
        CHECK_EQ(tinted, 0);
    }

    // The settings by hand, each a percentage of TuxAPE's own.
    {
        const QSize size(1440, 1080);
        CrtLook look;
        // No glow: the black beside the square is black.
        look.glow = 0;
        const QImage dark = CrtRenderer::render(frame, size, true, look);
        CHECK(meanLight(dark, at(488, 110, 6, 50)) < besideSquare * 0.3);
        look = CrtLook();
        look.glow = 200;
        CHECK(meanLight(CrtRenderer::render(frame, size, true, look), at(488, 110, 6, 50)) > besideSquare * 1.5);
        // No scan lines: the grey band is even from top to bottom.
        look = CrtLook();
        look.scanLines = 0;
        look.mask = 0;
        const QImage even = CrtRenderer::render(frame, size, true, look);
        double peak = 0, trough = 1;
        for (int y = 30 * 4; y < 30 * 4 + 16; ++y) {
            const double row = meanLight(even, QRect(600, y, 90, 1));
            peak = std::max(peak, row);
            trough = std::min(trough, row);
        }
        CHECK(trough > peak * 0.93);
        // No mask: white is white.
        int tinted = 0;
        for (int x = 660; x < 660 + 90; ++x) {
            const QRgb pixel = even.pixel(x, 135 * 4 + 2);
            tinted += std::abs(qRed(pixel) - qBlue(pixel)) > 30;
        }
        CHECK_EQ(tinted, 0);
        // The curve of the glass draws the picture in towards its corners:
        // flat, the grey band's left end is where the frame has it, at
        // pixel 180; curved, some ten pixels further in.
        look = CrtLook();
        look.curvature = 0;
        const QImage flat = CrtRenderer::render(frame, size, true, look);
        const QRect end(181, 26 * 4, 6, 60);
        CHECK(meanLight(flat, end) > 4 * meanLight(drawn, end));
        // Blur: at none a thin line is thinner, and brighter in its middle.
        look = CrtLook();
        look.blur = 0;
        look.fringe = 0;
        const QImage crisp = CrtRenderer::render(frame, size, true, look);
        const QRect beside(static_cast<int>(521 * 1.875) + 4, 120 * 4, 2, 80);  // a pixel and a half off a line's middle
        CHECK(meanLight(crisp, beside) < meanLight(drawn, beside));
    }

    // In the application the shader is on unless turned off, and set as
    // TuxAPE has it. A display that cannot draw widgets with the graphics
    // card, such as the one tests run on, keeps the plain picture.
    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();
    Settings settings = window.settings();
    CHECK(settings.crtShader && settings.crtLook == CrtLook());
    CHECK(window.screen()->crtShader() == CrtView::supported());
    if (CrtView* crt = window.screen()->crtView()) {
        CHECK(crt->mask() && crt->look() == CrtLook());
        CHECK(crt->geometry().width() * 540 == crt->geometry().height() * 768);
        // The keyboard and the mouse are still the screen's.
        CHECK(crt->testAttribute(Qt::WA_TransparentForMouseEvents) && crt->focusPolicy() == Qt::NoFocus);
        settings.monitorType = 1;  // green: no mask
        settings.crtLook.glow = 150;
        window.applySettings(settings);
        CHECK(window.screen()->crtView() == crt && !crt->mask() && crt->look().glow == 150);
        settings.monitorType = 0;
    }
    settings.crtShader = false;
    window.applySettings(settings);
    CHECK(!window.screen()->crtShader());

    // The Display page: a box, six sliders and a button that puts them back.
    settings.crtShader = true;
    settings.crtLook = CrtLook();
    settings.crtLook.mask = 40;
    settings.crtLook.fringe = 180;
    {
        SetupDialog dialog(settings);
        auto* box = dialog.findChild<QCheckBox*>("ckCrtShader");
        auto* defaults = dialog.findChild<QPushButton*>("bCrtDefaults");
        const char* const names[6] = {"slCrtCurvature", "slCrtScanLines", "slCrtMask", "slCrtGlow", "slCrtBlur", "slCrtFringe"};
        QSlider* sliders[6];
        bool all = box && defaults;
        for (int i = 0; i < 6; ++i) {
            sliders[i] = dialog.findChild<QSlider*>(names[i]);
            all = all && sliders[i];
        }
        CHECK(all);
        if (all) {
            CHECK(box->isChecked());
            CHECK(sliders[0]->value() == 100 && sliders[2]->value() == 40 && sliders[5]->value() == 180);
            CHECK(sliders[0]->minimum() == 0 && sliders[0]->maximum() == 200);
            CHECK(dialog.findChild<QLabel*>("lCrtMask")->text() == "40 %");
            // Each move is told at once, for the picture to follow.
            int told = 0;
            bool lastOn = false;
            CrtLook lastLook;
            QObject::connect(&dialog, &SetupDialog::shaderChanged, [&](bool on, const CrtLook& look) {
                ++told;
                lastOn = on;
                lastLook = look;
            });
            sliders[3]->setValue(65);
            CHECK(told == 1 && lastOn && lastLook.glow == 65 && lastLook.mask == 40);
            CHECK(dialog.settings().crtLook.glow == 65);
            box->setChecked(false);
            CHECK(told == 2 && !lastOn);
            CHECK(!dialog.settings().crtShader);
            defaults->click();
            CHECK(lastLook == CrtLook() && dialog.settings().crtLook == CrtLook());
            for (QSlider* slider : sliders)
                CHECK_EQ(slider->value(), 100);
            CHECK(!dialog.settings().crtShader);  // the box is not the button's
        }
    }
    // Kept in the settings file.
    {
        Settings kept;
        kept.crtShader = false;
        kept.crtLook.curvature = 20;
        kept.crtLook.blur = 170;
        CHECK(kept.save());
        Settings read;
        read.load();
        CHECK(!read.crtShader && read.crtLook.curvature == 20 && read.crtLook.blur == 170 && read.crtLook.mask == 100);
    }

    // The machine stands still while the Settings window is open, and goes
    // on when it closes; one that was paused stays paused.
    for (const bool pausedBefore : {false, true}) {
        emulator.setPaused(pausedBefore);
        bool seen = false, pausedMeanwhile = false;
        QTimer watch;
        QObject::connect(&watch, &QTimer::timeout, [&] {
            if (auto* dialog = qobject_cast<SetupDialog*>(QApplication::activeModalWidget())) {
                seen = true;
                pausedMeanwhile = emulator.isPaused();
                dialog->reject();
            }
        });
        watch.start(10);
        // Settings > Display.
        for (QAction* action : window.findChildren<QAction*>())
            if (action->text() == "&Display")
                action->trigger();
        watch.stop();
        CHECK(seen && pausedMeanwhile);
        CHECK(emulator.isPaused() == pausedBefore);
    }
    emulator.setPaused(false);
    return checkSummary("gui_crt");
}
