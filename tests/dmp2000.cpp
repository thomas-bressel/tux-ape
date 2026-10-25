// The Amstrad DMP2000: its typefaces read from the ROMs, and what it does
// with what a program sends, in time: the needles of a line where they
// fall and when, 105 characters a second, BUSY while it prints and feeds,
// both ways but graphics, the international characters, NLQ in two
// passes, graphics, a cut sheet running out and the next put in, fan fold
// going on past its perforations.
//
// Needs the ROM images of a DMP2000 and a DMP3160 (roms/printer/DMP2000.ROM
// and DMP3160.ROM, or $TUXAPE_PRINTER_ROMS); exits with code 77, which CTest
// reports as "skipped", when they are not there.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/dmp2000.h"
#include "core/files.h"
#include "core/screen_text.h"
#include "core/setup.h"

namespace {

using namespace tuxape;
using Event = Dmp2000::Event;

std::optional<Dmp2000Font> loadFont()
{
    std::filesystem::path folder = userRomDir() / "printer";
    if (const char* dir = std::getenv("TUXAPE_PRINTER_ROMS"); dir && *dir)
        folder = dir;
    const auto a = readFile(folder / "DMP2000.ROM");
    const auto b = readFile(folder / "DMP3160.ROM");
    if (!a || !b)
        return std::nullopt;
    return Dmp2000Font::fromRoms(*a, *b);
}

// Sends a string as the CPC does: a character, then BUSY waited for.
uint64_t send(Dmp2000& printer, const std::string& text, uint64_t t)
{
    for (unsigned char c : text) {
        while (printer.busy(t))
            t += 1000;  // the firmware's loop, a millisecond at a time
        printer.receive(c, t);
        t += 50;
    }
    return t;
}

std::vector<Event> only(const std::vector<Event>& events, Event::Kind kind)
{
    std::vector<Event> out;
    std::copy_if(events.begin(), events.end(), std::back_inserter(out), [&](const Event& e) { return e.kind == kind; });
    return out;
}

// What was printed, in reading order: a line printed right to left gives
// its characters in that order, their places say the line's.
std::u32string text(const std::vector<Event>& events)
{
    std::vector<Event> printed = only(events, Event::Kind::Printed);
    std::stable_sort(printed.begin(), printed.end(), [](const Event& a, const Event& b) {
        const auto& p = a.printed;
        const auto& q = b.printed;
        return p.sheet != q.sheet ? p.sheet < q.sheet : std::fabs(p.y - q.y) > 1e-6 ? p.y < q.y : p.x < q.x;
    });
    std::u32string s;
    for (const Event& e : printed)
        s += e.printed.character;
    return s;
}

bool near(double a, double b, double within = 1e-6)
{
    return std::fabs(a - b) <= within;
}

void testFont(const Dmp2000Font& font)
{
    // "A" in draft: columns of needles, nothing in a space; the NLQ "A"
    // as its ROM has it, its upper pass from &3400 + 65 x 12, after its
    // attribute; NLQ's "g" descends too.
    const auto& a = font.draft[32 + ('A' - 0x20)];
    CHECK(std::any_of(a.columns.begin(), a.columns.end(), [](uint8_t b) { return b; }));
    CHECK(!a.descends());
    CHECK(font.draft[32 + ('g' - 0x20)].descends());
    const std::array<uint8_t, 11> upperA{0, 0x06, 0x18, 0x60, 0x80, 0x60, 0x18, 0x06, 0, 0, 0};
    CHECK(font.nlq[32 + ('A' - 0x20)].upper == upperA);
    CHECK(font.nlq[32 + ('g' - 0x20)].descends() && !font.nlq[32 + ('A' - 0x20)].descends());
    // The wrong images: nothing.
    CHECK(!Dmp2000Font::fromRoms(std::vector<uint8_t>(0x4000), std::vector<uint8_t>(0x8000)));
    CHECK(!Dmp2000Font::fromRoms(std::vector<uint8_t>(100), std::vector<uint8_t>(0x8000)));
}

void testLine(const Dmp2000Font& font)
{
    Dmp2000 printer(font);
    const std::vector<Event> start = printer.takeEvents();
    CHECK(start.size() == 1 && start[0].kind == Event::Kind::SheetStarted);
    CHECK(!printer.busy(0));

    // Nothing is printed until the line ends.
    uint64_t t = send(printer, "HELLO", 1000);
    CHECK(only(printer.takeEvents(), Event::Kind::Strike).empty());
    CHECK(!printer.busy(t));
    t = send(printer, "\r\n", t);
    const uint64_t sent = t;
    std::vector<Event> events = printer.takeEvents();
    CHECK(text(events) == U"HELLO");
    const auto strikes = only(events, Event::Kind::Strike);
    CHECK(strikes.size() > 20);
    // On the first line of the A4 sheet, the 8 inches in its middle.
    const double left = (8.27 - 8.0) / 2.0;
    bool inLine = true, inCells = true;
    for (const Event& e : strikes) {
        inLine = inLine && near(e.strike.y, 1.0 / 3.0) && e.strike.sheet == 0;
        inCells = inCells && e.strike.x >= left - 1e-9 && e.strike.x < left + 0.5;
    }
    CHECK(inLine && inCells);
    const auto printed = only(events, Event::Kind::Printed);
    CHECK(printed.size() == 5 && near(printed[1].printed.x - printed[0].printed.x, 0.1));
    // 105 characters a second: the head over half an inch at 10.5 inches a
    // second, then the paper 1/6 inch in 200 ms.
    const auto motions = only(events, Event::Kind::Motion);
    CHECK(motions.size() == 2);
    if (motions.size() == 2) {
        CHECK(motions[0].motion.kind == Dmp2000::Motion::Kind::Carriage && motions[0].motion.printing);
        const double seconds = (motions[0].motion.end - motions[0].motion.start) / 1e6;
        CHECK(near(seconds, (motions[0].motion.to - motions[0].motion.from) / 10.5, 1e-4));
        CHECK(motions[1].motion.kind == Dmp2000::Motion::Kind::Paper);
        CHECK(near((motions[1].motion.end - motions[1].motion.start) / 1e6, 0.2, 1e-4));
        CHECK(near(motions[1].motion.to - motions[1].motion.from, 1.0 / 6.0));
    }
    // BUSY all that time: a CPC waits for it.
    CHECK(printer.busy(sent) && printer.busy(printer.idleFrom() - 1) && !printer.busy(printer.idleFrom()));
    // The strikes come in time, the head going left to right.
    bool rising = true;
    for (size_t i = 1; i < strikes.size(); ++i)
        rising = rising && strikes[i].microsecond >= strikes[i - 1].microsecond;
    CHECK(rising && strikes.front().strike.x < strikes.back().strike.x);

    // The next line from the right, where the head is: both ways.
    t = send(printer, "AB\r\n", printer.idleFrom());
    events = printer.takeEvents();
    const auto back = only(events, Event::Kind::Strike);
    CHECK(!back.empty() && back.front().strike.x > back.back().strike.x);
    CHECK(near(back.front().strike.y, 1.0 / 3.0 + 1.0 / 6.0));

    // A full line of pica: 80 characters in 8 inches, 80 / 105 of a second.
    t = send(printer, std::string(80, 'H') + "\r\n", printer.idleFrom());
    events = printer.takeEvents();
    CHECK_EQ(text(events).size(), 80u);
    const auto line = only(events, Event::Kind::Motion);
    CHECK(!line.empty() && line[0].motion.printing);
    if (!line.empty())
        CHECK(std::fabs((line[0].motion.end - line[0].motion.start) / 1e6 - 80.0 / 105.0) < 0.02);
    // One more is a new line of its own.
    send(printer, std::string(81, 'I') + "\r\n", printer.idleFrom());
    events = printer.takeEvents();
    const auto lines = only(events, Event::Kind::Printed);
    CHECK(lines.size() == 81 && lines.back().printed.y > lines.front().printed.y);
}

void testCodes(const Dmp2000Font& font)
{
    // French characters, by DIP switch and by ESC "R".
    Dmp2000::Switches france;
    france.country = 1;
    Dmp2000 printer(font, france, Dmp2000::a4());
    printer.takeEvents();
    send(printer, "@{|}\r\n", 0);
    CHECK(text(printer.takeEvents()) == U"àéùè");
    send(printer, std::string("\x1BR\x02[\\]\r\n"), printer.idleFrom());
    CHECK(text(printer.takeEvents()) == U"ÄÖÜ");

    std::vector<Event> events0;
    // CAN takes back the line, DEL its last character.
    send(printer, std::string("ABC\x18XY\x7FZ\r\n"), printer.idleFrom());
    CHECK(text(printer.takeEvents()) == U"XZ");

    // The eighth bit, which only a Plus sends: &C1 is an italic "A".
    printer.receive(0xC1, printer.idleFrom());
    send(printer, "\r", printer.idleFrom() + 100);
    events0 = printer.takeEvents();
    const auto italic = only(events0, Event::Kind::Printed);
    CHECK(italic.size() == 1 && italic[0].printed.character == U'A' && italic[0].printed.italic);
    // DIP switch DS1-7: seven bits, whatever the machine.
    Dmp2000::Switches seven;
    seven.sevenBits = true;
    Dmp2000 apple(font, seven, Dmp2000::a4());
    apple.takeEvents();
    apple.receive(0xC1, 0);
    send(apple, "\r", 100);
    const auto upright = only(apple.takeEvents(), Event::Kind::Printed);
    CHECK(upright.size() == 1 && !upright[0].printed.italic);

    // NLQ: two passes, the second half a needle lower.
    send(printer, std::string("\x1Bx\x01M\r\n"), printer.idleFrom());
    std::vector<Event> events = printer.takeEvents();
    double top = 1e9, bottom = -1e9;
    for (const Event& e : only(events, Event::Kind::Strike)) {
        top = std::min(top, e.strike.y);
        bottom = std::max(bottom, e.strike.y);
    }
    CHECK(near(bottom - top, 1.0 / 144.0));
    CHECK(only(events, Event::Kind::Motion).size() >= 3);  // two passes and the line feed

    // Graphics: ESC "K" with three columns, 60 to the inch, bit 7 the top
    // needle (which a CPC's seven bits never reach).
    send(printer, std::string("\x1Bx\x00\x1BK\x03\x00\x01\x02\x40\r\n", 12), printer.idleFrom());
    events = printer.takeEvents();
    const auto dots = only(events, Event::Kind::Strike);
    CHECK_EQ(dots.size(), 3u);
    if (dots.size() == 3) {
        CHECK(dots[0].strike.pins == 0x002 && dots[1].strike.pins == 0x004 && dots[2].strike.pins == 0x080);
        CHECK(near(dots[1].strike.x - dots[0].strike.x, 1.0 / 60.0));
    }

    // ESC "@" sets it back: pica, draft, USA.
    send(printer, std::string("\x1B@#\r\n"), printer.idleFrom());
    CHECK(text(printer.takeEvents()) == U"#");
}

void testPaper(const Dmp2000Font& font)
{
    // A cut sheet: a form feed sends it out, the lamp lights, and BUSY
    // stays until another is put in.
    Dmp2000 printer(font);
    printer.takeEvents();
    send(printer, "X\r\x0C", 0);
    std::vector<Event> events = printer.takeEvents();
    CHECK(only(events, Event::Kind::SheetDone).size() == 1 && only(events, Event::Kind::PaperOut).size() == 1);
    CHECK(printer.paperOut() && printer.busy(printer.idleFrom() + 1000000));
    printer.loadPaper(printer.idleFrom());
    events = printer.takeEvents();
    const auto started = only(events, Event::Kind::SheetStarted);
    CHECK(started.size() == 1 && started[0].sheet == 1);
    CHECK(!printer.paperOut() && !printer.busy(printer.idleFrom()));
    // Off line, LF and FF move the paper, and nothing is taken in.
    printer.setOnline(false, printer.idleFrom());
    CHECK(printer.busy(printer.idleFrom()));
    printer.lineFeedButton(printer.idleFrom());
    CHECK(only(printer.takeEvents(), Event::Kind::Motion).size() == 1);
    printer.setOnline(true, printer.idleFrom());

    // Fan fold of 11 inches: 66 lines of 1/6 inch, then the next sheet.
    Dmp2000 fan(font, {}, Dmp2000::fanFold(false));
    fan.takeEvents();
    uint64_t t = 0;
    for (int line = 0; line < 66; ++line)
        t = send(fan, "\n", t);
    events = fan.takeEvents();
    CHECK(only(events, Event::Kind::SheetDone).size() == 1 && only(events, Event::Kind::SheetStarted).size() == 1);
    CHECK(!fan.paperOut());
    // A form feed goes to the next page's top, whatever the line.
    send(fan, "\n\n\x0C", t);
    events = fan.takeEvents();
    CHECK(only(events, Event::Kind::SheetStarted).size() == 1);
}

// A 6128's BASIC printing two lines: its firmware sends each character
// when BUSY is low, and waits while the first line prints.
void testCpc(const Dmp2000Font& font)
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        std::printf("the CPC's ROM images were not found: its part is skipped\n");
        return;
    }
    Dmp2000 printer(font);
    printer.takeEvents();
    cpc.setPrinter(&printer);
    for (int frame = 0; frame < 150; ++frame)
        cpc.runFrame();
    AutoType autoType(cpc.keyboard());
    autoType.type("PRINT #8,\"HELLO\":PRINT #8,\"WORLD\":PRINT \"DO\";\"NE\"\n");
    while (autoType.active()) {
        autoType.frame();
        cpc.runFrame();
    }
    int frames = 0;
    while (readScreenText(cpc).find("DONE") == std::string::npos && frames < 500) {
        cpc.runFrame();
        ++frames;
    }
    const uint64_t done = cpc.microseconds();
    const std::vector<Event> events = printer.takeEvents();
    CHECK(text(events) == U"HELLOWORLD");
    // WORLD's first needles after HELLO's line feed; BASIC on only then.
    const auto feeds = only(events, Event::Kind::Motion);
    uint64_t firstFeed = 0;
    for (const Event& e : feeds)
        if (e.motion.kind == Dmp2000::Motion::Kind::Paper && !firstFeed)
            firstFeed = e.motion.end;
    uint64_t world = UINT64_MAX;
    for (const Event& e : only(events, Event::Kind::Printed))
        if (e.printed.character == U'W')
            world = e.printed.microsecond;
    CHECK(firstFeed > 0 && world >= firstFeed);
    CHECK(done >= firstFeed);
    const auto printed = only(events, Event::Kind::Printed);
    CHECK(!printed.empty() && near(printed.back().printed.y, 1.0 / 3.0 + 1.0 / 6.0));
    cpc.setPrinter(nullptr);
}

}  // namespace

int main()
{
    const auto font = loadFont();
    if (!font) {
        std::printf("the DMP2000's and DMP3160's ROM images were not found; skipping\n");
        return 77;
    }
    testFont(*font);
    testLine(*font);
    testCodes(*font);
    testPaper(*font);
    testCpc(*font);
    return checkSummary("dmp2000");
}
