// The Registers window: what the Gate Array, the sound chip, the CRTC and,
// on a Plus, the ASIC hold, shown as the machine has them and, for the
// palette and the two chips' registers, typed over. Runs without a display
// (QT_QPA_PLATFORM=offscreen); no ROM is needed.
//
//   gui_registers [prefix]   also saves a picture of the window as
//                            <prefix>registers.png

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPoint>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "check.h"
#include "core/cartridge.h"
#include "core/cpc.h"
#include "core/setup.h"
#include "emulator.h"
#include "mainwindow.h"
#include "registersdialog.h"
#include "screenwidget.h"
#include "settings.h"

namespace {

QString hex(unsigned value, int digits)
{
    return QString("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    tuxape::Cartridge cartridge;  // outlives the machine it goes into
    cartridge.data.assign(0x4000 * 8, 0);
    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();

    QAction* show = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text() == "&Registers")
            show = action;
    QToolButton* button = nullptr;
    for (QToolButton* candidate : window.findChildren<QToolButton*>())
        if (candidate->toolTip() == "Registers")
            button = candidate;
    CHECK(show && show->isEnabled() && button && button->isEnabled());
    CHECK(window.registers() == nullptr);
    if (!show)
        return checkSummary("gui_registers");
    show->trigger();
    RegistersDialog* registers = window.registers();
    CHECK(registers && registers->isVisible());
    if (!registers)
        return checkSummary("gui_registers");

    // What a program puts in the chips shows, with the register it last
    // chose lit.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.out(0x7F00, 0x03);
        cpc.out(0x7F00, 0x4A);
        cpc.out(0x7F00, 0x10);
        cpc.out(0x7F00, 0x55);
        cpc.out(0xBC00, 12);
        cpc.out(0xBD00, 0x30);
        cpc.out(0xBC00, 7);
        cpc.out(0xBD00, 0x1E);
        cpc.psg().setRegister(7, 0x3F);
    });
    registers->refresh();
    CHECK(registers->value("Palette3") == "0A");
    CHECK(registers->value("Palette16") == "15");
    CHECK(registers->highlighted("Palette16") && !registers->highlighted("Palette3"));
    CHECK(registers->value("CRTC12") == "30" && registers->value("CRTC7") == "1E");
    CHECK(registers->highlighted("CRTC7") && !registers->highlighted("CRTC12"));
    CHECK(registers->value("PSG7") == "3F");
    struct Counters {
        unsigned vcc, vlc, hcc, r52, mode;
    };
    const Counters counters = emulator.withMachine([](tuxape::Cpc& cpc) {
        return Counters{cpc.crtc().vcc(), cpc.crtc().vlc(), cpc.crtc().hcc(), cpc.gateArray().interruptCounter(),
                        cpc.gateArray().mode()};
    });
    CHECK(registers->value("VCC") == hex(counters.vcc, 2) && registers->value("VLC") == hex(counters.vlc, 2));
    CHECK(registers->value("HCC") == hex(counters.hcc, 2) && registers->value("R52") == hex(counters.r52, 2));
    CHECK(registers->value("Mode") == hex(counters.mode, 1));
    CHECK(registers->value("VMA").size() == 4 && registers->value("VDUR").size() == 4);
    for (const char* name : {"VSC", "HDC", "VTAC", "HSC", "ICSR"})
        CHECK(registers->value(name).size() == 2);

    // A value typed in goes to the chip, and the chip's choice of register
    // stays what it was.
    CHECK(registers->setValue("Palette2", "1C"));
    CHECK(registers->setValue("CRTC1", "20"));
    CHECK(registers->setValue("PSG8", "0f"));
    CHECK(!registers->setValue("VCC", "01"));
    CHECK(!registers->setValue("Palette2", "zz"));
    CHECK(!registers->setValue("CRTC16", "00"));
    CHECK(!registers->setValue("PSG3", "100"));
    auto* pen5 = registers->findChild<QLineEdit*>("edPalette5");
    CHECK(pen5 && !pen5->isReadOnly());
    if (pen5) {
        pen5->setFocus();
        pen5->selectAll();
        QTest::keyClicks(pen5, "0b");
        QTest::keyClick(pen5, Qt::Key_Return);
    }
    struct Chips {
        unsigned ink2, ink5, pen, crtc1, crtcSelected, psg8;
    };
    const Chips chips = emulator.withMachine([](tuxape::Cpc& cpc) {
        return Chips{cpc.gateArray().ink(2), cpc.gateArray().ink(5), cpc.gateArray().selectedPen(), cpc.crtc().reg(1),
                     cpc.crtc().selected(), cpc.psg().reg(8)};
    });
    CHECK_EQ(chips.ink2, 0x1C);
    CHECK_EQ(chips.ink5, 0x0B);
    CHECK_EQ(chips.pen, 16);
    CHECK_EQ(chips.crtc1, 0x20);
    CHECK_EQ(chips.crtcSelected, 7);
    CHECK_EQ(chips.psg8, 0x0F);
    CHECK(registers->value("Palette2") == "1C" && registers->value("Palette5") == "0B");
    CHECK(registers->value("CRTC1") == "20" && registers->value("PSG8") == "0F");
    auto* vcc = registers->findChild<QLineEdit*>("edVCC");
    CHECK(vcc && vcc->isReadOnly());

    // The ASIC's part is for a Plus.
    auto* asicButton = registers->findChild<QToolButton*>("bAsic");
    CHECK(asicButton && !asicButton->isEnabled() && !registers->asicShown());
    struct Asic {
        unsigned sssa, ivr, colour1, x, y;
        bool plus, unlocked;
    };
    const Asic asic = emulator.withMachine([&](tuxape::Cpc& cpc) {
        cpc.setCartridge(&cartridge);
        // The key that unlocks the ASIC, then its registers in memory.
        for (const int value : {1, 0, 0xFF, 0x77, 0xB3, 0x51, 0xA8, 0xD4, 0x62, 0x39, 0x9C, 0x46, 0x2B, 0x15, 0x8A, 0xCD, 0xEE})
            cpc.out(0xBC00, static_cast<uint8_t>(value));
        cpc.out(0x7F00, 0xA0 | 0x18 | 2);
        cpc.write(0x6402, 0x84);  // pen 1: red 8, blue 4, green 15
        cpc.write(0x6403, 0x0F);
        cpc.write(0x6424, 0x0F);  // sprite colour 2: blue
        cpc.write(0x6425, 0x00);
        cpc.write(0x4200, 2);     // sprite 2, its first pixel
        cpc.write(0x6010, 0x23);
        cpc.write(0x6011, 0x01);
        cpc.write(0x6012, 0x45);
        cpc.write(0x6013, 0x00);
        cpc.write(0x6014, 0x0D);  // four times as wide, as tall as it is
        cpc.write(0x6800, 0x40);
        cpc.write(0x6801, 0x30);
        cpc.write(0x6802, 0x30);
        cpc.write(0x6803, 0xF0);
        cpc.write(0x6804, 0x25);
        cpc.write(0x6805, 0x51);
        cpc.write(0x6C00, 0x00);
        cpc.write(0x6C01, 0x90);
        cpc.write(0x6C02, 0x55);
        cpc.write(0x6C0F, 0x01);
        cpc.memory().write(0x9000, 0x0A);  // LD R9,0A
        cpc.memory().write(0x9001, 0x09);
        const tuxape::Asic& a = cpc.asic();
        return Asic{a.splitAddress(), a.interruptVector(), a.colour(1), static_cast<uint16_t>(a.sprite(2).x),
                    static_cast<uint16_t>(a.sprite(2).y), cpc.plus(), a.unlocked()};
    });
    CHECK(asic.plus && asic.unlocked);
    CHECK_EQ(asic.colour1, 0xF84);
    registers->refresh();
    CHECK(asicButton && asicButton->isEnabled());
    registers->setAsicShown(true);
    registers->setSprite(2);
    CHECK(registers->asicShown() && registers->sprite() == 2);
    auto* unlocked = registers->findChild<QCheckBox*>("ckUnlocked");
    auto* ramEnabled = registers->findChild<QCheckBox*>("ckRamEnabled");
    CHECK(unlocked && unlocked->isVisible() && unlocked->isChecked());
    CHECK(ramEnabled && ramEnabled->isChecked());
    CHECK(registers->value("LRB") == "0000" && registers->value("CartBank") == "2");
    auto* colour1 = registers->findChild<QLabel*>("colour1");
    CHECK(colour1 && colour1->palette().color(QPalette::Window) == QColor(0x88, 0xFF, 0x44));
    CHECK(colour1 && colour1->toolTip() == "Red 8, Green 15, Blue 4");
    auto* spriteBox = registers->findChild<QGroupBox*>("gbSprite");
    CHECK(spriteBox && spriteBox->title() == "Sprite 2");
    auto* spriteView = registers->findChild<QLabel*>("SpriteView");
    CHECK(spriteView && !spriteView->pixmap().isNull()
          && spriteView->pixmap().toImage().pixelColor(1, 1) == QColor(0, 0, 0xFF));
    CHECK(registers->value("X") == hex(asic.x, 4) && asic.x == 0x123);
    CHECK(registers->value("Y") == hex(asic.y, 4) && asic.y == 0x45);
    CHECK(registers->value("MagX") == "4" && registers->value("MagY") == "1");
    // As the screen shows it: four mode 2 pixels to a pixel across, one
    // line down, a line being two mode 2 pixels high.
    CHECK(registers->spriteAsShown().size() == QSize(64, 32));
    CHECK(registers->spriteAsShown().pixelColor(1, 1) == QColor(0, 0, 0xFF));
    // Its position and its magnification typed over: written where a
    // program writes them.
    const auto spriteNow = [&] {
        return emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.asic().sprite(2); });
    };
    CHECK(registers->setValue("MagY", "2") && spriteNow().magY == 2 && spriteNow().magX == 4);
    CHECK(registers->value("MagY") == "2" && registers->spriteAsShown().size() == QSize(64, 64));
    CHECK(!registers->setValue("MagX", "3") && spriteNow().magX == 4);
    CHECK(registers->setValue("MagY", "0") && spriteNow().magY == 0 && spriteNow().magX == 4);
    CHECK(registers->setValue("MagY", "1") && spriteNow().magY == 1);
    CHECK(registers->setValue("X", "0050") && spriteNow().x == 0x50 && registers->value("X") == "0050");
    CHECK(registers->setValue("Y", "FFF0") && spriteNow().y == -16);
    CHECK(registers->setValue("X", "0123") && registers->setValue("Y", "0045"));
    {
        QLineEdit* magX = registers->findChild<QLineEdit*>("edMagX");
        CHECK(magX && !magX->isReadOnly());
        CHECK(registers->findChild<QLineEdit*>("edPRI")->isReadOnly());
    }
    // The page of registers out of memory and back, by its box.
    CHECK(ramEnabled && ramEnabled->isEnabled());
    ramEnabled->click();
    CHECK(!ramEnabled->isChecked());
    CHECK(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.asic().rmr2(); }) == 0x02);
    ramEnabled->click();
    CHECK(ramEnabled->isChecked() && emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.asic().rmr2(); }) == 0x1A);
    // The ASIC locked, then unlocked, by its box: the CRTC keeps the
    // register that was chosen.
    const auto crtcSelected = [&] { return emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.crtc().selected(); }); };
    const uint8_t chosen = crtcSelected();
    unlocked->click();
    CHECK(!unlocked->isChecked() && !emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.asic().unlocked(); }));
    CHECK(!ramEnabled->isEnabled() && !registers->setRamEnabled(false));
    unlocked->click();
    CHECK(unlocked->isChecked() && emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.asic().unlocked(); }));
    CHECK(ramEnabled->isEnabled() && crtcSelected() == chosen);
    CHECK(registers->value("PRI") == "40" && registers->value("SSS") == "30" && registers->value("SSC") == "25");
    CHECK(registers->value("IVR") == hex(asic.ivr, 2) && asic.ivr != 0);
    CHECK(registers->value("SSSA") == hex(asic.sssa, 4) && asic.sssa != 0);
    CHECK(registers->value("DCSR") == "01");
    auto* dma0 = registers->findChild<QCheckBox*>("ckDma0");
    auto* dma1 = registers->findChild<QCheckBox*>("ckDma1");
    CHECK(dma0 && dma0->isChecked() && dma1 && !dma1->isChecked());
    CHECK(registers->value("Addr0") == "9000" && registers->value("Pause0") == "55");
    CHECK(registers->dmaInstruction(0) == "LD R9,0A");
    // The lower ROM moved, the registers out of memory again.
    emulator.withMachine([](tuxape::Cpc& cpc) { cpc.out(0x7F00, 0xA0 | 0x08 | 5); });
    registers->refresh();
    CHECK(ramEnabled && !ramEnabled->isChecked());
    CHECK(registers->value("LRB") == "4000" && registers->value("CartBank") == "5");

    if (!prefix.isEmpty())
        registers->grab().save(prefix + "registers.png");
    // "Row Highlight": while its box is ticked and the machine stands
    // still, the screen shows where the beam is: the line of the picture
    // and the place along it. Not while the machine runs, nor once the
    // box is cleared or the window gone.
    {
        auto* row = registers->findChild<QCheckBox*>("ckRowHighlight");
        ScreenWidget* screen = window.screen();
        CHECK(row && !row->isChecked() && !registers->rowHighlight());
        CHECK(screen->beamMarkerLine() == -1 && screen->beamMarkerColumn() == -1);
        emulator.setPaused(true);
        // A quarter of a frame on: the beam is somewhere down the picture.
        emulator.withMachine([](tuxape::Cpc& cpc) { cpc.run(5000); });
        const int line = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.monitor().rasterLine(); });
        const int column = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.monitor().beamColumn(); });
        if (row)
            row->setChecked(true);
        CHECK(registers->rowHighlight());
        CHECK_EQ(screen->beamMarkerLine(), line);
        CHECK_EQ(screen->beamMarkerColumn(), column);
        CHECK(registers->value("VDUR").toInt(nullptr, 16) == (line & 0xFFFF) || registers->value("VDUR").toInt() == line);
        // It follows the machine from one stop to the next.
        emulator.withMachine([](tuxape::Cpc& cpc) { cpc.run(640); });  // ten lines
        registers->refresh();
        CHECK_EQ(screen->beamMarkerLine(),
                 emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.monitor().rasterLine(); }));
        CHECK(screen->beamMarkerLine() != line);
        // On the picture: a line across, at the beam's line. The line is
        // yellow on a picture that has none of it.
        if (screen->beamMarkerLine() >= 0 && screen->beamMarkerLine() < 270) {
            const QImage shown = screen->grab().toImage();
            const int y = (screen->beamMarkerLine() * 2 + 1) * screen->height() / 540;
            int yellow = 0;
            for (int x = 0; x < shown.width(); x += 8) {
                const QColor colour = shown.pixelColor(x, std::min(y, shown.height() - 1));
                yellow += colour.red() > 180 && colour.green() > 180 && colour.blue() < 120;
            }
            CHECK(yellow > shown.width() / 8 / 2);
        }
        emulator.setPaused(false);
        registers->refresh();
        CHECK_EQ(screen->beamMarkerLine(), -1);
        emulator.setPaused(true);
        registers->refresh();
        CHECK(screen->beamMarkerLine() != -1);
        if (row)
            row->setChecked(false);
        CHECK(screen->beamMarkerLine() == -1 && screen->beamMarkerColumn() == -1);
        registers->setRowHighlight(true);
        CHECK(screen->beamMarkerLine() != -1);
        registers->hide();
        CHECK_EQ(screen->beamMarkerLine(), -1);
        CHECK(!screen->interruptMarksShown());
        registers->setRowHighlight(false);
        emulator.setPaused(false);

        // And, the machine running, where the Z80 took its interrupts in
        // the picture: those of the Gate Array, every 52 lines, six to a
        // frame. A program that does nothing but take them: EI and a loop,
        // with EI, RET at &0038 in RAM.
        emulator.setPaused(true);
        emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
        emulator.setPaused(true);
        emulator.withMachine([](tuxape::Cpc& cpc) {
            cpc.out(0x7F00, 0x8D);  // both ROMs off, mode 1
            // The CRTC as the firmware sets it, which it has not done yet.
            const int crtc[10] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7};
            for (int n = 0; n < 10; ++n) {
                cpc.out(0xBC00, static_cast<uint8_t>(n));
                cpc.out(0xBD00, static_cast<uint8_t>(crtc[n]));
            }
            for (const auto& [address, byte] : {std::pair{0x0038, 0xFB}, {0x0039, 0xC9}, {0x9100, 0xFB}, {0x9101, 0x18}, {0x9102, 0xFE}})
                cpc.memory().write(static_cast<uint16_t>(address), static_cast<uint8_t>(byte));
            cpc.cpu().pc = 0x9100;
            cpc.cpu().im = 1;
        });
        registers->show();
        registers->setRowHighlight(true);
        CHECK(screen->interruptMarksShown());
        emulator.setPaused(false);
        emulator.start();
        QTest::qWait(300);
        QList<QPoint> marks = screen->interruptMarks();
        CHECK_EQ(marks.size(), 6);
        std::sort(marks.begin(), marks.end(), [](const QPoint& a, const QPoint& b) { return a.y() < b.y(); });
        for (int n = 1; n < marks.size(); ++n)
            CHECK_EQ(marks[n].y() - marks[n - 1].y(), 52);
        // On the picture: a dashed light magenta line, at a mark's line.
        for (const QPoint& mark : marks) {
            if (mark.y() < 10 || mark.y() >= 260)
                continue;
            const QImage shown = screen->grab().toImage();
            const int y = (mark.y() * 2 + 1) * screen->height() / 540;
            int magenta = 0;
            for (int x = 0; x < shown.width(); ++x) {
                const QColor colour = shown.pixelColor(x, std::min(y, shown.height() - 1));
                magenta += colour.red() > 200 && colour.green() < 160 && colour.blue() > 200;
            }
            CHECK(magenta > shown.width() / 4 && magenta < shown.width());
            break;
        }
        if (!prefix.isEmpty())
            screen->grab().save(prefix + "interrupts.png");
        registers->setRowHighlight(false);
        CHECK(!screen->interruptMarksShown());
        registers->setRowHighlight(true);
        registers->hide();
        CHECK(!screen->interruptMarksShown());
        registers->setRowHighlight(false);
        emulator.stop();
    }
    return checkSummary("gui_registers");
}
