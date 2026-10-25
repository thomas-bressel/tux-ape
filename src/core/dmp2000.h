#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/printerport.h"

namespace tuxape {

// The DMP2000's three typefaces, as its ROMs hold them. Each has 128
// characters: 32 of the international sets (à è ù ò ì º £ ¡ ¿ Ñ ñ ¤ ₧ Å å
// ç § ß Æ æ Ø ø ¨ Ä Ö Ü ä ö ü É é ¥), then ASCII from &20 to &7F.
//
// Draft and its italics: an attribute byte, then 11 columns of 8 needles
// (bit 7 the top one) a half dot apart (1/120 inch in pica). The attribute
// is that of the characters a program defines (ESC "&"): bit 7 clear, the
// character is a needle lower, which is how g, j, p, q and y reach the
// ninth; bits 6-4 and 3-0 the first and last column, for proportional
// printing. The DMP2000 keeps them in its processor's own 4K ROM, which no
// one has read; they are taken from a DMP3160's, whose EPROM holds the
// whole of its program (from &00C0, italics from &06C0). The two printers
// share their NLQ characters, all but one, the DMP3160's being the
// DMP2000's firmware grown.
//
// NLQ: two passes of the same form, the second half a needle (1/144
// inch) lower, from the DMP2000's 16K EPROM (from &3400 and &3A00).
struct Dmp2000Font {
    struct Draft {
        uint8_t attribute = 0x8B;  // no descender, columns 0 to 11
        std::array<uint8_t, 11> columns{};
        bool descends() const { return !(attribute & 0x80); }
    };
    struct Nlq {
        uint8_t attribute = 0x8B;
        std::array<uint8_t, 11> upper{}, lower{};
        bool descends() const { return !(attribute & 0x80); }
    };
    std::array<Draft, 128> draft{}, italic{};
    std::array<Nlq, 128> nlq{};

    // From the two ROM images: the DMP2000's 16K EPROM and the DMP3160's
    // 32K one. Nothing if either is not what it should be.
    static std::optional<Dmp2000Font> fromRoms(std::span<const uint8_t> dmp2000, std::span<const uint8_t> dmp3160);
};

// The Amstrad DMP2000 (1985): a 9-needle impact dot-matrix printer, Epson
// FX-80 compatible, with NLQ. Its manual (chapter 7) gives what is
// followed here: 105 characters a second (52 in double width), a line
// feed of 1/6 inch in 200 ms, a matrix of 9 x 9, 10, 12 or 17 characters
// to the inch, a buffer of 2K, and BUSY high while it prints, feeds paper
// or takes in a control code. Its 72 control codes are those of CPCWiki's
// table of the DMP2000.
//
// What it does is told as events in time: each column of needles struck,
// each character printed (for a text to go with the picture), each motion
// of the carriage and of the paper (for their sound), each sheet. The
// mechanism's work is planned when a line is complete, its events dated
// in the time to come: a window shows them as the machine's clock reaches
// them, and the printer is busy until the last.
//
// Where the manual is silent, the figures are the FX-80's or a
// mechanism's: the carriage goes back at 20 inches a second, prints both
// ways (but graphics, which go left to right), emphasized printing and
// NLQ at half speed, NLQ in two passes; a character takes 10 microseconds
// to be taken in, a control code 20.
class Dmp2000 : public PrinterPort {
public:
    // The DIP switches (manual, chapter 7 page 5), as they leave the
    // factory but the alarm, which the manual has sounding at power on.
    struct Switches {
        int country = 0;               // DS1-1..3: USA, France, Germany, UK, Denmark, Sweden, Italy, Spain
        bool crLf = false;             // DS1-4: CR feeds a line too
        bool paperOutSensor = true;    // DS1-5 off
        bool page12Inch = false;       // DS1-6: page of 12 inches, not 11
        bool sevenBits = false;        // DS1-7: the eighth bit always 0
        bool nlq = false;              // DS1-8: NLQ to begin with
        bool slashedZero = false;      // DS2-1
        bool skipPerforation = false;  // DS2-2
        bool download = false;         // DS2-4: the buffer keeps characters defined by ESC "&"
        bool beeper = true;            // DS2-6
        bool condensed = false;        // DS2-7
        bool bold = false;             // DS2-8
        bool operator==(const Switches&) const = default;
        // From the two banks as they are set, bit n - 1 for switch n on
        // (DS1-1 to 3 the country, DS1-1 its lowest bit), and back.
        static Switches fromBanks(int ds1, int ds2);
        int ds1() const;
        int ds2() const;
    };

    // The paper: a cut sheet (fed by friction) of so many inches, or fan
    // fold (by the tractors), sheets joined at their perforations.
    struct Paper {
        double width = 8.27;    // A4
        double length = 11.69;
        bool fanFold = false;
        bool operator==(const Paper&) const = default;
    };
    static Paper a4() { return {}; }
    static Paper fanFold(bool twelveInches) { return {9.5, twelveInches ? 12.0 : 11.0, true}; }

    // A column of needles struck: `pins` bit 8 the top needle, bit 0 the
    // ninth. `x` is from the paper's left edge, `y` the top needle's from
    // the sheet's top, in inches.
    struct Strike {
        uint64_t microsecond;
        int sheet;
        double x, y;
        uint16_t pins;
        // What the mechanism adds to it: the head going right to left (its
        // dots land a little off those struck the other way), the ink of
        // the ribbon where it is under the head, and how far the paper's
        // last move missed its mark (inches, down). See printerwindow.cpp.
        bool backward = false;
        float ink = 1.0f;
        float slip = 0.0f;
    };
    // A character printed, for a text that goes with the picture.
    struct Printed {
        uint64_t microsecond;
        int sheet;
        double x, y, width, height;  // the cell it takes, in inches
        char32_t character;
        bool bold, italic, underline;
    };
    // The carriage or the paper moving, for the sound and for the head
    // seen moving. `from` and `to` are the head's x, or the paper's
    // place under it (the sheet's y), in inches.
    struct Motion {
        enum class Kind : uint8_t { Carriage, Paper };
        Kind kind;
        uint64_t start, end;  // microseconds
        double from, to;
        bool printing;  // the carriage at printing speed, needles striking
    };
    struct Event {
        enum class Kind : uint8_t {
            Strike,
            Printed,
            Motion,
            SheetStarted,  // `sheet` is under the head, from its top
            SheetDone,     // `sheet` has left the printer, or been torn off
            PaperOut,
            Beep,
        };
        Kind kind;
        uint64_t microsecond;
        int sheet = 0;
        Strike strike{};
        Printed printed{};
        Motion motion{};
    };

    Dmp2000(const Dmp2000Font& font, const Switches& switches, const Paper& paper);
    // As it leaves the factory, an A4 sheet in.
    explicit Dmp2000(const Dmp2000Font& font);

    // The port: eight bits, as the cable has them (see printerport.h).
    void receive(uint8_t data, uint64_t microsecond) override;
    bool busy(uint64_t microsecond) override;

    // The buttons. LF and FF move the paper only off line.
    void setOnline(bool online, uint64_t microsecond);
    bool online() const { return online_; }
    void lineFeedButton(uint64_t microsecond);
    void formFeedButton(uint64_t microsecond);
    // A cut sheet put in (fed in by friction to the first line), or fan
    // fold loaded: the paper-out lamp goes off.
    void loadPaper(uint64_t microsecond);
    bool paperOut() const { return paperOut_; }
    // Power off and on: ESC "@", the buffer emptied, the paper where it is.
    void reset(uint64_t microsecond);

    const Switches& switches() const { return switches_; }
    const Paper& paper() const { return paper_; }
    // What has happened since the last call, in the order of the times.
    std::vector<Event> takeEvents();
    // When the mechanism will have done all it has been given.
    uint64_t idleFrom() const { return busyUntil_; }

private:
    struct Style {
        bool elite = false, condensed = false, bold = false, doubleStrike = false;
        bool doubleWidth = false, lineDoubleWidth = false, italic = false, underline = false;
        bool proportional = false, nlq = false;
        int script = -1;  // -1 none, 0 superscript, 1 subscript
    };
    // What a line is made of until it is printed: columns of needles at
    // places in the line, in passes (0 the first; NLQ and double strike
    // have two), and the characters for the text.
    struct Column {
        double x;       // from the left margin's first column
        double dy;      // the pass's offset down, in inches
        uint16_t pins;  // 9 needles
        int pass;
    };

    void take(uint8_t byte, uint64_t microsecond);
    void control(uint8_t code, uint64_t microsecond);
    void escape(uint64_t microsecond);
    bool wantsMore() const;
    void printable(uint8_t code, uint64_t microsecond);
    void graphics(const std::vector<uint8_t>& sequence, size_t data, int density, bool nine);
    void homeHead(uint64_t microsecond);
    void printLine(uint64_t microsecond);
    void feed(double inches, uint64_t microsecond);
    void formFeed(uint64_t microsecond);
    void carriageReturn();
    void beep(uint64_t microsecond);
    void initialize();
    double characterWidth() const;
    double columnSpacing() const;
    double lineSpacing() const { return lineSpacing_; }
    double printableWidth() const { return 8.0; }
    double leftEdge() const;
    void startSheet(uint64_t microsecond);
    void record(Event event);
    uint64_t later(uint64_t microsecond) const;
    char32_t unicode(int glyph) const;
    int glyphIndex(uint8_t code) const;

    Dmp2000Font font_;
    std::array<Dmp2000Font::Draft, 256> download_{};
    std::array<bool, 256> downloaded_{};
    Switches switches_;
    Paper paper_;

    // The interpreter.
    Style style_;
    std::vector<uint8_t> sequence_;  // an ESC sequence or graphics being taken in
    bool online_ = true;
    bool paperOut_ = false;
    bool msbForced_ = false, msbValue_ = false;
    bool printControls_ = false;   // ESC "I" 1
    bool upperControls_ = true;    // &80-&9F are control codes, unless ESC "6"
    bool useDownload_ = false;     // ESC "%" 1
    bool immediate_ = false;       // ESC "i" 1
    bool unidirectional_ = false;  // ESC "U" 1
    bool oneLineUnidirectional_ = false;  // ESC "<"
    bool halfSpeed_ = false;       // ESC "s" 1
    bool paperOutSensor_ = true;
    int country_ = 0;
    double lineSpacing_ = 1.0 / 6.0;
    int pageLines_ = 0;            // ESC "C" n: the page in lines, or 0
    double pageLength_ = 11.0;     // inches
    int skipLines_ = 0;            // ESC "N"
    int leftMargin_ = 0, rightMargin_ = 80;  // in characters of the pitch they were set in
    double leftMarginInches_ = 0.0, rightMarginInches_ = 8.0;
    std::vector<int> tabs_;        // in characters
    std::array<std::vector<int>, 8> verticalTabs_{};
    int verticalChannel_ = 0;
    std::array<int, 4> graphicsDensity_{0, 1, 2, 3};  // what K, L, Y, Z print

    // The ribbon: how far its loop has gone by the head, in inches; and
    // the paper's moves, for the error of each.
    double ribbon_ = 0.0;
    uint32_t paperMoves_ = 0;
    float slip_ = 0.0f;

    // The line being made.
    std::vector<Column> line_;
    std::vector<Printed> lineText_;
    double x_ = 0.0;      // where the next character goes, from the left margin's place
    bool lineGraphics_ = false;
    bool lineQuad_ = false;  // graphics of 240 to the inch: half speed

    // The mechanism.
    double headX_ = 0.0;  // inches from the print position's first column
    double paperY_ = 0.0; // the top needle's place on the sheet
    double sinceTopOfForm_ = 0.0;
    int sheet_ = 0;
    bool sheetLoaded_ = false;
    uint64_t busyUntil_ = 0;
    uint64_t takenUntil_ = 0;  // a character being taken in
    std::vector<Event> events_;
};

}  // namespace tuxape
