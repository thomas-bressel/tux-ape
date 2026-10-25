// The DMP2000's window: the sheet filling as the time of each needle comes,
// the head going along the line, the window asking to be shown when
// printing starts (once), the dots where the needles struck, a cut sheet
// going out and the next one shown, the sheets gone back to; then its
// lamps and buttons, on the emulator's printer. Runs without a display
// (QT_QPA_PLATFORM=offscreen); needs the DMP2000's typefaces
// (roms/printer/), else it is skipped.
//
//   gui_printer [prefix]   also saves a picture of the window, a page of
//                          the printer's typefaces on it, as <prefix>printer.png,
//                          the page as <prefix>printer_page.png, and its top
//                          at 600 dots to the inch as <prefix>printer_600.png

#include <cstdio>
#include <string>

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>

#include "check.h"
#include "emulator.h"
#include "printersound.h"
#include "printerwindow.h"
#include "settings.h"

namespace {

using tuxape::Dmp2000;
using Event = Dmp2000::Event;

uint64_t send(Dmp2000& printer, const std::string& text, uint64_t t)
{
    for (unsigned char c : text) {
        while (printer.busy(t))
            t += 1000;
        printer.receive(c, t);
        t += 50;
    }
    return t;
}

size_t strikes(const std::vector<Event>& events)
{
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [](const Event& e) { return e.kind == Event::Kind::Strike; }));
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const auto font = Emulator::dmp2000Fonts();
    if (!font) {
        std::printf("the DMP2000's typefaces were not found; skipping\n");
        return 77;
    }

    // ---- The window by itself, given a printer's events ----
    {
        Dmp2000 printer(*font);
        PrinterWindow window(nullptr);
        QSignalSpy started(&window, &PrinterWindow::printingStarted);
        send(printer, "HELLO\r\n", 0);
        std::vector<Event> events = printer.takeEvents();
        const size_t all = strikes(events);
        const uint64_t end = printer.idleFrom();
        uint64_t first = UINT64_MAX, last = 0;
        for (const Event& e : events)
            if (e.kind == Event::Kind::Strike) {
                first = std::min(first, e.microsecond);
                last = std::max(last, e.microsecond);
            }

        // Before the first needle: the sheet, nothing on it.
        window.feed(events, first - 1);
        CHECK(window.sheets().size() == 1 && window.sheets()[0].strikes.empty());
        CHECK_EQ(started.count(), 0);
        // Half way along the line: some of it, and the window asks to be
        // shown.
        window.feed({}, (first + last) / 2);
        const size_t half = window.sheets()[0].strikes.size();
        CHECK(half > 0 && half < all);
        CHECK_EQ(started.count(), 1);
        // All of it, the window asking no more.
        window.feed({}, end);
        CHECK_EQ(window.sheets()[0].strikes.size(), all);
        CHECK_EQ(window.sheets()[0].text.size(), 5u);
        CHECK_EQ(started.count(), 1);
        // A dot in the picture where the first needle of "H" struck.
        const Dmp2000::Strike s = window.sheets()[0].strikes.front();
        int pin = 0;
        while (pin < 9 && !(s.pins & (0x100 >> pin)))
            ++pin;
        const QImage& picture = window.view()->picture();
        const QPoint at(static_cast<int>(s.x * SheetView::kDotsPerInch), static_cast<int>((s.y + pin / 72.0) * SheetView::kDotsPerInch));
        CHECK(qAlpha(picture.pixel(at)) > 128);
        CHECK(qAlpha(picture.pixel(at + QPoint(0, 40))) == 0);  // a fifth of an inch lower, the paper

        // A form feed sends the sheet out; the next one put in is shown.
        send(printer, "\x0C", printer.idleFrom());
        printer.loadPaper(printer.idleFrom());
        send(printer, "BYE\r\n", printer.idleFrom());
        window.feed(printer.takeEvents(), printer.idleFrom());
        CHECK(window.sheets().size() == 2 && window.sheets()[0].done);
        CHECK_EQ(window.shownSheet(), 1);
        auto* label = window.findChild<QLabel*>("lSheet");
        CHECK(label && label->text() == "Sheet 2 of 2");
        auto* previous = window.findChild<QToolButton*>("tbPreviousSheet");
        CHECK(previous && previous->isEnabled());
        if (previous)
            previous->click();
        CHECK(window.shownSheet() == 0 && label && label->text() == "Sheet 1 of 2");
        // The first sheet as it was.
        CHECK(qAlpha(window.view()->picture().pixel(at)) > 128);

        // Finished: a PDF of the two sheets, then none left but the one
        // still in the printer, blank.
        auto* finish = window.findChild<QPushButton*>("bFinish");
        CHECK(finish && finish->isEnabled());
        const QString pdf = folder.filePath("print.pdf");
        CHECK(window.writePdf(pdf, 150));
        QFile written(pdf);
        CHECK(written.open(QIODevice::ReadOnly) && written.read(5) == "%PDF-");
        CHECK(written.size() > 1000);
        const int inPrinter = window.sheets().back().number;
        window.takeAway();
        CHECK(window.sheets().size() == 1 && window.sheets()[0].strikes.empty() && window.sheets()[0].number == inPrinter);
        CHECK(finish && !finish->isEnabled());
        CHECK(!window.writePdf(pdf, 150));
        // The next line goes on it.
        send(printer, "AGAIN\r\n", printer.idleFrom());
        window.feed(printer.takeEvents(), printer.idleFrom());
        CHECK(window.sheets().size() == 1 && !window.sheets()[0].strikes.empty() && finish && finish->isEnabled());
    }

    // ---- The needles' marks: the same each time, lighter struck softer ----
    {
        const Dmp2000::Strike column{0, 0, 0.1, 0.1, 0x1FF};
        QImage once(80, 80, QImage::Format_ARGB32_Premultiplied), again(80, 80, QImage::Format_ARGB32_Premultiplied),
            softer(80, 80, QImage::Format_ARGB32_Premultiplied);
        for (QImage* picture : {&once, &again, &softer})
            picture->fill(Qt::transparent);
        SheetView::paint(once, 200, column, 1.0);
        SheetView::paint(again, 200, column, 1.0);
        SheetView::paint(softer, 200, column, 0.0);
        CHECK(once == again);
        const auto ink = [](const QImage& picture) {
            long sum = 0;
            for (int y = 0; y < picture.height(); ++y)
                for (int x = 0; x < picture.width(); ++x)
                    sum += qAlpha(picture.pixel(x, y));
            return sum;
        };
        CHECK(ink(softer) > 0 && ink(softer) < ink(once) * 3 / 4);
        // Struck right to left, the dots land further left.
        Dmp2000::Strike back = column;
        back.backward = true;
        again.fill(Qt::transparent);
        SheetView::paint(again, 200, back, 1.0);
        CHECK(again != once);
    }

    // ---- What is heard: nothing before, the needles and the motors while
    // it prints, nothing once done; the same each time ----
    {
        const auto hear = [&](std::vector<int16_t>& all) {
            Dmp2000 printer(*font);
            send(printer, "HELLO, WORLD\r\n", 100000);
            printer.formFeedButton(0);  // on line: nothing
            const std::vector<Event> events = printer.takeEvents();
            PrinterSound sound;
            constexpr int kRate = 44100;
            const uint64_t until = printer.idleFrom() + 500000;
            for (uint64_t t = 20000; t <= until; t += 20000) {
                if (t == 20000)
                    sound.add(events, t);
                std::vector<int16_t> chunk(2 * kRate / 50, 0);
                sound.mix(chunk, kRate, t);
                all.insert(all.end(), chunk.begin(), chunk.end());
            }
            return printer.idleFrom();
        };
        std::vector<int16_t> heard, again;
        const uint64_t end = hear(heard);
        hear(again);
        CHECK(heard == again);
        // 20 ms a chunk, from 20 ms: chunk n ends at (n + 1) * 20 ms.
        const auto loudest = [&](uint64_t from, uint64_t to) {
            int most = 0;
            for (size_t i = from / 20000 * 1764; i < std::min(heard.size(), to / 20000 * 1764); ++i)
                most = std::max(most, std::abs(static_cast<int>(heard[i])));
            return most;
        };
        CHECK_EQ(loudest(0, 100000), 0);
        CHECK(loudest(120000, end) > 2000 && loudest(120000, end) < 32000);
        CHECK(loudest(end + 300000, end + 500000) < 50);
        if (argc > 1) {
            // To listen to.
            QFile wav(QString::fromLocal8Bit(argv[1]) + "printer.wav");
            if (wav.open(QIODevice::WriteOnly)) {
                const auto u32 = [&](uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
                const auto u16 = [&](uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
                const uint32_t bytes = static_cast<uint32_t>(heard.size() * 2);
                wav.write("RIFF");
                u32(36 + bytes);
                wav.write("WAVEfmt ");
                u32(16);
                u16(1);
                u16(2);
                u32(44100);
                u32(44100 * 4);
                u16(4);
                u16(16);
                wav.write("data");
                u32(bytes);
                wav.write(reinterpret_cast<const char*>(heard.data()), bytes);
            }
        }
    }

    // ---- A page of what it prints, for whoever looks ----
    if (argc > 1) {
        Dmp2000 printer(*font);
        PrinterWindow window(nullptr);
        // Strings that keep their zeros: ESC "-" 0, ESC "x" 0, ESC "R" 0.
        using namespace std::string_literals;
        std::string page = "10 REM The Amstrad DMP2000, as TuxAPE prints it\r\n"
                           "20 PRINT #8,\"Hello, World!\"\r\n\r\n"s;
        page += "\x1B" "EEmphasized\x1B" "F, \x1B" "Gdouble strike\x1B" "H, \x1B" "4italics\x1B" "5, \x1B" "-\x01underlined\x1B" "-\x00.\r\n"s;
        page += "\x0E" "Double width\r\n"s;
        page += "\x1B" "x\x01Near Letter Quality: the quick brown fox jumps over the lazy dog.\r\n\x1B" "x\x00"s;
        page += "\x0F" "Condensed: 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz\x12\r\n"s;
        page += "\x1B" "R\x01" "En fran\x5C" "ais : d\x7Bj\x40 vu, l\x7Dve, o\x7C ?\x1B" "R\x00\r\n"s;
        std::string bars = "\x1B" "L"s;
        bars += static_cast<char>(240);
        bars += '\0';
        for (int i = 0; i < 240; ++i)
            bars += static_cast<char>((i / 8) % 2 ? 0x7F : (0x7F >> (i % 8)));
        page += bars + "\r\n";
        send(printer, page, 0);
        window.feed(printer.takeEvents(), printer.idleFrom() + 1);
        window.resize(780, 520);
        window.show();
        QApplication::processEvents();
        window.grab().save(QString::fromLocal8Bit(argv[1]) + "printer.png");
        window.view()->picture().copy(0, 0, window.view()->picture().width(), 1100).save(QString::fromLocal8Bit(argv[1]) + "printer_page.png");
        // Its top at 600 dots to the inch, for the needles' marks.
        QImage fine(3 * 600, static_cast<int>(1.6 * 600), QImage::Format_ARGB32_Premultiplied);
        fine.fill(QColor(0xFB, 0xFA, 0xF4));
        QImage ink(fine.size(), QImage::Format_ARGB32_Premultiplied);
        ink.fill(Qt::transparent);
        for (const Dmp2000::Strike& s : window.sheets()[0].strikes)
            if (s.x < 3.6)
                SheetView::paint(ink, 600, Dmp2000::Strike{s.microsecond, s.sheet, s.x - 0.55, s.y - 0.25, s.pins, s.backward, s.ink, s.slip}, 1.0);
        QPainter(&fine).drawImage(0, 0, ink);
        fine.save(QString::fromLocal8Bit(argv[1]) + "printer_600.png");
        CHECK(window.writePdf(QString::fromLocal8Bit(argv[1]) + "printer.pdf"));
        // How long a full page takes to draw again, when a sheet is shown.
        Dmp2000 full(*font);
        std::string lines;
        for (int i = 0; i < 60; ++i)
            lines += std::string(80, static_cast<char>('A' + i % 26)) + "\r\n";
        send(full, lines, 0);
        PrintedSheet whole;
        for (const Event& e : full.takeEvents())
            if (e.kind == Event::Kind::Strike && e.sheet == 0)
                whole.strikes.push_back(e.strike);
        QElapsedTimer clock;
        clock.start();
        window.view()->showSheet(whole);
        std::printf("a full page drawn in %lld ms\n", static_cast<long long>(clock.elapsed()));
    }

    // ---- On the emulator's printer: lamps and buttons ----
    {
        Emulator emulator;
        CHECK(emulator.setPrinter(Settings::PrinterHost, QString()));
        CHECK(emulator.hasDmp2000());
        PrinterWindow window(&emulator);
        auto* online = window.findChild<QPushButton*>("bOnLine");
        auto* lineFeed = window.findChild<QPushButton*>("bLineFeed");
        auto* onlineLamp = window.findChild<QLabel*>("lOnLine");
        CHECK(online && lineFeed && onlineLamp);
        if (!online || !lineFeed || !onlineLamp)
            return checkSummary("gui_printer");
        CHECK(emulator.dmp2000Lamps().online && !lineFeed->isEnabled());
        CHECK(onlineLamp->text().contains(QChar(0x25CF)));  // lit: a full disc
        online->click();
        CHECK(!emulator.dmp2000Lamps().online && lineFeed->isEnabled());
        CHECK(onlineLamp->text().contains(QChar(0x25CB)));  // out: a ring
        lineFeed->click();
        uint64_t now = 0;
        const std::vector<Event> fed = emulator.takeDmp2000Events(&now);
        CHECK(std::any_of(fed.begin(), fed.end(), [](const Event& e) {
            return e.kind == Event::Kind::Motion && e.motion.kind == Dmp2000::Motion::Kind::Paper;
        }));
        // Another printer mode: no DMP2000.
        CHECK(emulator.setPrinter(Settings::PrinterDisabled, QString()));
        CHECK(!emulator.hasDmp2000());
    }
    return checkSummary("gui_printer");
}
