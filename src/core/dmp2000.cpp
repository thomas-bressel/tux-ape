#include "core/dmp2000.h"

#include <algorithm>
#include <cmath>

namespace tuxape {

namespace {

// The mechanism. 105 characters a second of 12 half-dot columns in pica:
// the carriage at 10.5 inches a second while it prints; paper at 1/6 inch
// in 200 ms (manual, chapter 7). The rest is chosen (see dmp2000.h).
constexpr double kPrintSpeed = 10.5;         // inches a second
constexpr double kColumnRate = 1260.0;       // half-dot columns a second, at most
constexpr double kReturnSpeed = 20.0;        // the carriage moving without printing
constexpr double kPaperSpeed = (1.0 / 6.0) / 0.2;
constexpr double kLoadSpeed = 2.0;           // a cut sheet fed in by friction
constexpr double kTopOfSheet = 1.0 / 3.0;    // the first line, under the head once a sheet is in
constexpr uint64_t kTakeCharacter = 10;      // microseconds
constexpr uint64_t kTakeControl = 20;
constexpr double kNeedle = 1.0 / 72.0;       // between two needles
constexpr double kHalfNeedle = 1.0 / 144.0;  // NLQ's second pass
constexpr double kDoubleStrike = 1.0 / 216.0;
constexpr int kLowestGlyph = 32;             // the ASCII ones come after the 32 international ones
// The ribbon, a loop of inked cloth that the carriage's moves draw past the
// needles: a third of their way, and three yards round, inked a little
// unevenly along it. The paper's moves: each within a thousandth and a half
// of an inch of where it should end (a stepper's teeth, the platen's grip).
constexpr double kRibbonDrive = 1.0 / 3.0;
constexpr double kRibbonLoop = 108.0;
constexpr double kPaperError = 0.0015;

uint64_t us(double seconds)
{
    return static_cast<uint64_t>(std::llround(seconds * 1e6));
}

// The ink the ribbon gives where it is: some ten per cent either way of
// the mean, in waves of a few inches to a yard, the same each time round.
float ribbonInk(double at)
{
    constexpr double kTurn = 2.0 * 3.14159265358979323846 / kRibbonLoop;
    const double p = std::fmod(at, kRibbonLoop) * kTurn;
    return static_cast<float>(1.0 + 0.05 * std::sin(3.0 * p) + 0.035 * std::sin(11.0 * p + 1.3) + 0.02 * std::sin(38.0 * p + 2.1));
}

// The error of the paper's nth move, -1 to 1: the same for the same move.
float paperError(uint32_t move)
{
    uint64_t z = move + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(static_cast<double>(z >> 11) / 4503599627370496.0 - 1.0);
}

// The international characters, by country: which glyph (0-31) takes the
// place of each of the twelve codes that change (Epson's ESC "R" sets).
constexpr uint8_t kChanged[12] = {0x23, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E, 0x60, 0x7B, 0x7C, 0x7D, 0x7E};
constexpr int8_t kCountries[9][12] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},  // USA
    {-1, -1, 0, 5, 15, 16, -1, -1, 30, 2, 1, 22},      // France
    {-1, -1, 16, 23, 24, 25, -1, -1, 26, 27, 28, 17},  // Germany
    {6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},   // United Kingdom
    {-1, -1, -1, 18, 20, 13, -1, -1, 19, 21, 14, -1},  // Denmark
    {-1, 11, 29, 23, 24, 13, 25, 30, 26, 27, 14, 28},  // Sweden
    {-1, -1, -1, 5, -1, 30, -1, 2, 0, 3, 1, 4},        // Italy
    {12, -1, -1, 7, 9, 8, -1, -1, 22, 10, -1, -1},     // Spain
    {-1, -1, -1, -1, 31, -1, -1, -1, -1, -1, -1, -1},  // Japan
};
constexpr char32_t kInternational[32] = {U'à', U'è', U'ù', U'ò', U'ì', U'º', U'£', U'¡', U'¿', U'Ñ', U'ñ',
                                         U'¤', U'₧', U'Å', U'å', U'ç', U'§', U'ß', U'Æ', U'æ', U'Ø', U'ø',
                                         U'¨', U'Ä', U'Ö', U'Ü', U'ä', U'ö', U'ü', U'É', U'é', U'¥'};

// The nine needles of a column of 8 (bit 7 the top one): needles 1 to 8,
// or 2 to 9 for a character that descends.
uint16_t pins(uint8_t column, bool lower)
{
    return lower ? column : static_cast<uint16_t>(column << 1);
}

}  // namespace

std::optional<Dmp2000Font> Dmp2000Font::fromRoms(std::span<const uint8_t> dmp2000, std::span<const uint8_t> dmp3160)
{
    if (dmp2000.size() != 0x4000 || dmp3160.size() != 0x8000)
        return std::nullopt;
    Dmp2000Font font;
    for (size_t c = 0; c < 128; ++c) {
        for (auto [table, at] : {std::pair{&font.draft, size_t{0x00C0}}, std::pair{&font.italic, size_t{0x06C0}}}) {
            const uint8_t* g = dmp3160.data() + at + c * 12;
            (*table)[c].attribute = g[0];
            std::copy(g + 1, g + 12, (*table)[c].columns.begin());
        }
        font.nlq[c].attribute = dmp2000[0x3400 + c * 12];
        std::copy_n(dmp2000.data() + 0x3400 + c * 12 + 1, 11, font.nlq[c].upper.begin());
        std::copy_n(dmp2000.data() + 0x3A00 + c * 12 + 1, 11, font.nlq[c].lower.begin());
    }
    // A space is blank in all three, an "A" is not: what is read is a font.
    const size_t space = kLowestGlyph, a = kLowestGlyph + ('A' - 0x20);
    const auto blank = [](const auto& columns) { return std::all_of(columns.begin(), columns.end(), [](uint8_t b) { return b == 0; }); };
    if (!blank(font.draft[space].columns) || blank(font.draft[a].columns) || !blank(font.nlq[space].upper)
        || blank(font.nlq[a].upper))
        return std::nullopt;
    return font;
}

Dmp2000::Dmp2000(const Dmp2000Font& font, const Switches& switches, const Paper& paper)
    : font_(font), switches_(switches), paper_(paper)
{
    initialize();
    sheetLoaded_ = true;
    paperY_ = paper_.fanFold ? 0.0 : kTopOfSheet;
    events_.push_back({Event::Kind::SheetStarted, 0, sheet_});
}

Dmp2000::Dmp2000(const Dmp2000Font& font)
    : Dmp2000(font, Switches(), Paper())
{
}

void Dmp2000::initialize()
{
    style_ = Style{};
    style_.condensed = switches_.condensed;
    style_.bold = switches_.bold;
    style_.nlq = switches_.nlq;
    sequence_.clear();
    msbForced_ = false;
    printControls_ = false;
    upperControls_ = true;
    useDownload_ = false;
    immediate_ = false;
    unidirectional_ = false;
    oneLineUnidirectional_ = false;
    halfSpeed_ = false;
    paperOutSensor_ = switches_.paperOutSensor;
    country_ = switches_.country;
    lineSpacing_ = 1.0 / 6.0;
    pageLines_ = 0;
    pageLength_ = paper_.fanFold ? paper_.length : (switches_.page12Inch ? 12.0 : 11.0);
    skipLines_ = switches_.skipPerforation ? 6 : 0;
    leftMarginInches_ = 0.0;
    rightMarginInches_ = printableWidth();
    // Every eight characters, as the FX-80 sets them.
    tabs_.clear();
    for (int t = 8; t < 137; t += 8)
        tabs_.push_back(t);
    for (auto& channel : verticalTabs_)
        channel.clear();
    verticalChannel_ = 0;
    graphicsDensity_ = {0, 1, 2, 3};
    line_.clear();
    lineText_.clear();
    x_ = 0.0;
    lineGraphics_ = false;
}

void Dmp2000::reset(uint64_t microsecond)
{
    initialize();
    downloaded_.fill(false);
    busyUntil_ = std::max(busyUntil_, microsecond);
}

// ---- the port ----------------------------------------------------------------------

bool Dmp2000::busy(uint64_t microsecond)
{
    if (!online_ || (paperOut_ && paperOutSensor_))
        return true;
    return microsecond < takenUntil_ || microsecond < busyUntil_;
}

void Dmp2000::receive(uint8_t data, uint64_t microsecond)
{
    if (!online_)
        return;  // off line, DATA is not taken (manual, pin 13)
    if (switches_.sevenBits)
        data &= 0x7F;
    if (msbForced_)
        data = static_cast<uint8_t>((data & 0x7F) | (msbValue_ ? 0x80 : 0));
    take(data, microsecond);
}

uint64_t Dmp2000::later(uint64_t microsecond) const
{
    return std::max(microsecond, busyUntil_);
}

void Dmp2000::record(Event event)
{
    events_.push_back(event);
}

std::vector<Dmp2000::Event> Dmp2000::takeEvents()
{
    std::vector<Event> taken;
    taken.swap(events_);
    std::stable_sort(taken.begin(), taken.end(), [](const Event& a, const Event& b) { return a.microsecond < b.microsecond; });
    return taken;
}

// ---- the interpreter -------------------------------------------------------------

void Dmp2000::take(uint8_t byte, uint64_t microsecond)
{
    if (!sequence_.empty()) {
        sequence_.push_back(byte);
        takenUntil_ = microsecond + kTakeCharacter;
        if (!wantsMore())
            escape(microsecond);
        return;
    }
    const uint8_t low = byte & 0x7F;
    // &80-&9F and &FF are the controls again, unless ESC "6". With ESC "I"
    // 1 the codes below &20 that do nothing print a character, the others
    // still doing what they do.
    bool control = (low < 0x20 || low == 0x7F) && (byte < 0x80 || upperControls_);
    if (control && printControls_ && low < 0x20) {
        static constexpr uint8_t kStillControls[] = {0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
                                                     0x0F, 0x11, 0x12, 0x13, 0x14, 0x18, 0x1B};
        control = std::find(std::begin(kStillControls), std::end(kStillControls), low) != std::end(kStillControls);
    }
    if (control) {
        takenUntil_ = microsecond + kTakeControl;
        this->control(low, microsecond);
        return;
    }
    takenUntil_ = microsecond + kTakeCharacter;
    printable(byte, microsecond);
}

void Dmp2000::control(uint8_t code, uint64_t microsecond)
{
    switch (code) {
    case 0x07:
        beep(microsecond);
        break;
    case 0x08:
        x_ = std::max(0.0, x_ - characterWidth());
        break;
    case 0x09: {
        // The next tab, in characters of the pitch now in use.
        const double width = characterWidth();
        const auto next = std::find_if(tabs_.begin(), tabs_.end(), [&](int t) { return t * width > x_ + 1e-9; });
        if (next != tabs_.end() && *next * width <= rightMarginInches_ - leftMarginInches_ + 1e-9)
            x_ = *next * width;
        break;
    }
    case 0x0A:
        printLine(microsecond);
        feed(lineSpacing(), microsecond);
        break;
    case 0x0B: {
        printLine(microsecond);
        const auto& tabs = verticalTabs_[static_cast<size_t>(verticalChannel_)];
        const int lineNow = static_cast<int>(std::floor(sinceTopOfForm_ / lineSpacing() + 1e-6));
        const auto next = std::find_if(tabs.begin(), tabs.end(), [&](int t) { return t > lineNow; });
        if (tabs.empty())
            feed(lineSpacing(), microsecond);
        else if (next == tabs.end())
            formFeed(microsecond);
        else
            feed((*next - lineNow) * lineSpacing(), microsecond);
        break;
    }
    case 0x0C:
        printLine(microsecond);
        formFeed(microsecond);
        break;
    case 0x0D:
        printLine(microsecond);
        carriageReturn();
        if (switches_.crLf)
            feed(lineSpacing(), microsecond);
        break;
    case 0x0E:
        style_.lineDoubleWidth = true;
        break;
    case 0x0F:
        if (!style_.bold)
            style_.condensed = true;
        break;
    case 0x12:
        style_.condensed = false;
        break;
    case 0x14:
        style_.lineDoubleWidth = false;
        break;
    case 0x18:
        // What is in the line and not yet printed.
        line_.clear();
        lineText_.clear();
        x_ = 0.0;
        lineGraphics_ = false;
        break;
    case 0x1B:
        sequence_.assign(1, 0x1B);
        break;
    case 0x7F:
        // The last character, if it is still in the line.
        if (!lineText_.empty()) {
            const Printed last = lineText_.back();
            lineText_.pop_back();
            std::erase_if(line_, [&](const Column& c) { return c.x >= last.x - 1e-9; });
            x_ = last.x;
        }
        break;
    default:
        break;  // DC1, DC3 and the rest: nothing
    }
}

// How many bytes the sequence being taken in has yet to come.
bool Dmp2000::wantsMore() const
{
    const size_t n = sequence_.size();
    if (n < 2)
        return true;
    const uint8_t c = sequence_[1];
    const auto word = [&](size_t at) { return n > at + 1 ? sequence_[at] | (sequence_[at + 1] << 8) : -1; };
    switch (c) {
    case '!': case '-': case '/': case '3': case 'A': case 'I': case 'J': case 'N': case 'Q': case 'R': case 'S':
    case 'U': case 'W': case 'i': case 'j': case 'l': case 'p': case 's': case 'x':
        return n < 3;
    case 'C':
        return n < 3 || (sequence_[2] == 0 && n < 4);
    case '%': case '?':
        return n < 4;
    case ':':
        return n < 5;
    case 'D': case 'B':
        return n < 3 || sequence_.back() != 0;
    case 'b':
        return n < 4 || sequence_.back() != 0;
    case '&': {
        if (n < 5)
            return true;
        const int first = sequence_[3], last = sequence_[4];
        return n < 5 + static_cast<size_t>(std::max(0, last - first + 1)) * 12;
    }
    case 'K': case 'L': case 'Y': case 'Z': {
        const int count = word(2);
        return count < 0 || n < 4 + static_cast<size_t>(count);
    }
    case '*': case '^': {
        if (n < 5)
            return true;
        const int count = word(3);
        return n < 5 + static_cast<size_t>(count) * (c == '^' ? 2 : 1);
    }
    default:
        return false;  // ESC and one byte: E, F, @, 4, 5...
    }
}

void Dmp2000::escape(uint64_t microsecond)
{
    std::vector<uint8_t> s;
    s.swap(sequence_);
    const uint8_t c = s[1];
    const int n = s.size() > 2 ? s[2] : 0;
    const bool on = n == 1 || n == '1';  // a flag, in binary or as a digit
    switch (c) {
    case 0x0E: style_.lineDoubleWidth = true; break;
    case 0x0F: if (!style_.bold) style_.condensed = true; break;
    case '!':
        style_.elite = n & 0x01;
        style_.condensed = (n & 0x04) && !(n & 0x08);
        style_.bold = n & 0x08;
        style_.doubleStrike = n & 0x10;
        style_.doubleWidth = n & 0x20;
        break;
    case '#': msbForced_ = false; break;
    case '=': msbForced_ = true; msbValue_ = false; break;
    case '>': msbForced_ = true; msbValue_ = true; break;
    case '%': useDownload_ = on; break;
    case '&': {
        if (!switches_.download)
            break;
        const int first = s[3], last = s[4];
        for (int ch = first; ch <= last; ++ch) {
            const size_t at = 5 + static_cast<size_t>(ch - first) * 12;
            Dmp2000Font::Draft g;
            g.attribute = s[at];
            std::copy_n(s.begin() + static_cast<std::ptrdiff_t>(at + 1), 11, g.columns.begin());
            download_[static_cast<size_t>(ch)] = g;
            download_[static_cast<size_t>(ch | 0x80)] = g;
            downloaded_[static_cast<size_t>(ch)] = downloaded_[static_cast<size_t>(ch | 0x80)] = true;
        }
        break;
    }
    case ':':
        // The ROM's characters into the download set.
        for (int ch = 0x20; ch < 0x80; ++ch) {
            download_[static_cast<size_t>(ch)] = font_.draft[static_cast<size_t>(kLowestGlyph + ch - 0x20)];
            downloaded_[static_cast<size_t>(ch)] = true;
        }
        break;
    case '*':
        graphics(s, 5, std::clamp(n, 0, 6), false);
        break;
    case '^':
        graphics(s, 5, std::clamp(n, 0, 6), true);
        break;
    case 'K': case 'L': case 'Y': case 'Z': {
        // ESC K lo hi ... is ESC * with the density K is given (ESC "?").
        const int which = c == 'K' ? 0 : c == 'L' ? 1 : c == 'Y' ? 2 : 3;
        graphics(s, 4, graphicsDensity_[static_cast<size_t>(which)], false);
        break;
    }
    case '?': {
        const int which = s[2] == 'K' ? 0 : s[2] == 'L' ? 1 : s[2] == 'Y' ? 2 : s[2] == 'Z' ? 3 : -1;
        if (which >= 0)
            graphicsDensity_[static_cast<size_t>(which)] = std::clamp(static_cast<int>(s[3]), 0, 6);
        break;
    }
    case '-': style_.underline = on; break;
    case '/': verticalChannel_ = std::clamp(n & 7, 0, 7); break;
    case '0': lineSpacing_ = 9.0 / 72.0; break;
    case '1': lineSpacing_ = 7.0 / 72.0; break;
    case '2': lineSpacing_ = 12.0 / 72.0; break;
    case '3': lineSpacing_ = n / 216.0; break;
    case 'A': lineSpacing_ = std::min(n, 85) / 72.0; break;
    case '4': style_.italic = true; break;
    case '5': style_.italic = false; break;
    case '6': upperControls_ = false; break;
    case '7': upperControls_ = true; break;
    case '8': paperOutSensor_ = false; break;
    case '9': paperOutSensor_ = true; break;
    case '<': oneLineUnidirectional_ = true; homeHead(microsecond); break;
    case '@': initialize(); break;
    case 'B': {
        auto& tabs = verticalTabs_[0];
        tabs.clear();
        for (size_t i = 2; i + 1 < s.size() && tabs.size() < 16; ++i)
            if (tabs.empty() || s[i] > tabs.back())
                tabs.push_back(s[i]);
        break;
    }
    case 'b': {
        auto& tabs = verticalTabs_[static_cast<size_t>(s[2] & 7)];
        tabs.clear();
        for (size_t i = 3; i + 1 < s.size() && tabs.size() < 16; ++i)
            if (tabs.empty() || s[i] > tabs.back())
                tabs.push_back(s[i]);
        break;
    }
    case 'C':
        if (n == 0) {
            pageLength_ = std::clamp(static_cast<int>(s[3]), 1, 22);
            pageLines_ = 0;
        } else {
            pageLines_ = n;
            pageLength_ = n * lineSpacing();
        }
        sinceTopOfForm_ = 0.0;  // this line is the page's top
        break;
    case 'D':
        tabs_.clear();
        for (size_t i = 2; i + 1 < s.size() && tabs_.size() < 32; ++i)
            if (tabs_.empty() || s[i] > tabs_.back())
                tabs_.push_back(s[i]);
        break;
    case 'E': style_.bold = true; style_.condensed = false; break;
    case 'F': style_.bold = false; break;
    case 'G': style_.doubleStrike = true; break;
    case 'H': style_.doubleStrike = false; break;
    case 'I': printControls_ = on; break;
    case 'J': printLine(microsecond); feed(n / 216.0, microsecond); break;
    case 'j': printLine(microsecond); feed(-n / 216.0, microsecond); break;
    case 'M': style_.elite = true; break;
    case 'P': style_.elite = false; break;
    case 'N': skipLines_ = std::clamp(n, 0, 127); break;
    case 'O': skipLines_ = 0; break;
    case 'Q': rightMarginInches_ = std::min(printableWidth(), leftMarginInches_ + n * characterWidth()); break;
    case 'l': leftMarginInches_ = std::min(rightMarginInches_ - characterWidth(), n * characterWidth()); break;
    case 'R': country_ = std::clamp(n, 0, 8); break;
    case 'S': style_.script = (n & 1); break;
    case 'T': style_.script = -1; break;
    case 'U': unidirectional_ = on; break;
    case 'W': style_.doubleWidth = on; break;
    case 'i': immediate_ = on; break;
    case 'p': style_.proportional = on; break;
    case 's': halfSpeed_ = on; break;
    case 'x': style_.nlq = on; break;
    default: break;
    }
}

double Dmp2000::characterWidth() const
{
    double width = style_.condensed ? 1.0 / (137.0 / 8.0) : style_.elite ? 1.0 / 12.0 : 1.0 / 10.0;
    if (style_.doubleWidth || style_.lineDoubleWidth)
        width *= 2.0;
    return width;
}

double Dmp2000::columnSpacing() const
{
    return characterWidth() / 12.0;
}

int Dmp2000::glyphIndex(uint8_t code) const
{
    const uint8_t low = code & 0x7F;
    if (low == '0' && switches_.slashedZero)
        return 127;  // the slashed zero is kept where DEL would be
    for (int i = 0; i < 12; ++i)
        if (low == kChanged[i] && kCountries[country_][i] >= 0)
            return kCountries[country_][i];
    if (low < 0x20)
        return low;  // with ESC "I" 1: the international glyphs show at 0-31
    return kLowestGlyph + (low - 0x20);
}

char32_t Dmp2000::unicode(int glyph) const
{
    if (glyph < kLowestGlyph)
        return kInternational[glyph];
    if (glyph == 127)
        return switches_.slashedZero ? U'0' : U' ';
    return static_cast<char32_t>(0x20 + glyph - kLowestGlyph);
}

void Dmp2000::printable(uint8_t code, uint64_t microsecond)
{
    const int glyph = glyphIndex(code);
    const bool italic = style_.italic || (code >= 0xA0 && code != 0xFF);
    const bool download = useDownload_ && downloaded_[code];
    const Dmp2000Font::Draft& draft = download ? download_[code] : (italic ? font_.italic : font_.draft)[static_cast<size_t>(glyph)];
    double width = characterWidth();
    if (style_.proportional) {
        // From its first column to its last, and one to space it.
        const int first = (draft.attribute >> 4) & 7, last = draft.attribute & 15;
        width = columnSpacing() * std::clamp(last - first + 2, 2, 12);
    }
    if (leftMarginInches_ + x_ + width > rightMarginInches_ + 1e-9) {
        // A full line: printed, and a new one begun.
        printLine(microsecond);
        carriageReturn();
        feed(lineSpacing(), microsecond);
    }
    const double spacing = columnSpacing();
    const double start = x_ - (style_.proportional ? ((draft.attribute >> 4) & 7) * spacing : 0.0);
    const auto add = [&](double x, uint16_t p, int pass, double dy) {
        if (p)
            line_.push_back({x, dy, p, pass});
    };
    // NLQ has no italics of its own: those are draft's.
    const bool nlq = style_.nlq && !italic && !download;
    const Dmp2000Font::Nlq& quality = font_.nlq[static_cast<size_t>(glyph)];
    for (int column = 0; column < 12; ++column) {
        const double x = start + column * spacing;
        if (nlq && column < 11) {
            add(x, pins(quality.upper[static_cast<size_t>(column)], quality.descends()), 0, 0.0);
            add(x, pins(quality.lower[static_cast<size_t>(column)], quality.descends()), 1, kHalfNeedle);
        } else if (!nlq && column < 11) {
            const uint16_t p = pins(draft.columns[static_cast<size_t>(column)], draft.descends());
            if (style_.script >= 0) {
                // Half height: the top four rows' needles, two passes a
                // half needle apart, high or low in the line.
                const uint8_t rows = draft.columns[static_cast<size_t>(column)];
                uint16_t even = 0, odd = 0;
                for (int r = 0; r < 8; ++r)
                    if (rows & (0x80 >> r))
                        (r % 2 ? odd : even) |= static_cast<uint16_t>(0x100 >> (r / 2 + (style_.script ? 4 : 0)));
                add(x, even, 0, 0.0);
                add(x, odd, 1, kHalfNeedle);
                continue;
            }
            add(x, p, 0, 0.0);
            // Double width: each dot struck again half a column on, the
            // strokes as whole as at the normal width.
            if (style_.doubleWidth || style_.lineDoubleWidth)
                add(x + spacing / 2.0, p, 0, 0.0);
            // Emphasized: each dot again half a dot (1/240 inch) on.
            if (style_.bold)
                add(x + 1.0 / 240.0, p, 0, 0.0);
            if (style_.doubleStrike)
                add(x, p, 1, kDoubleStrike);
        }
        // The ninth needle all along: underline.
        if (style_.underline && column % 2 == 0)
            add(x, 0x001, 0, 0.0);
    }
    lineText_.push_back({0, sheet_, x_, 0.0, width, 9 * kNeedle, unicode(glyph), style_.bold || style_.doubleStrike, italic, style_.underline});
    x_ += width;
    if (immediate_)
        printLine(microsecond);
}

void Dmp2000::graphics(const std::vector<uint8_t>& s, size_t data, int density, bool nine)
{
    // Columns of eight needles (bit 7 the top one) so many to the inch;
    // with ESC "^" two bytes a column, the ninth needle bit 7 of the second.
    // Densities 2 and 3 strike no needle twice running: double speed.
    static constexpr double kPerInch[7] = {60.0, 120.0, 120.0, 240.0, 80.0, 72.0, 90.0};
    const double step = 1.0 / kPerInch[density];
    const size_t width = nine ? 2 : 1;
    for (size_t i = data; i + width <= s.size(); i += width) {
        uint16_t p = static_cast<uint16_t>(s[i] << 1);
        if (nine && (s[i + 1] & 0x80))
            p |= 0x001;
        if (leftMarginInches_ + x_ <= rightMarginInches_ + 1e-9 && p)
            line_.push_back({x_, 0.0, p, 0});
        x_ += step;
    }
    lineGraphics_ = true;
    lineQuad_ = lineQuad_ || density == 3;
}

// ---- the mechanism -----------------------------------------------------------------

double Dmp2000::leftEdge() const
{
    // The printable 8 inches in the middle of the paper's width, less its
    // tractors' strips.
    return std::max(0.0, (paper_.width - printableWidth()) / 2.0);
}

void Dmp2000::carriageReturn()
{
    x_ = 0.0;
    lineGraphics_ = false;
    lineQuad_ = false;
}

void Dmp2000::homeHead(uint64_t microsecond)
{
    if (std::fabs(headX_) < 1e-6)
        return;
    const uint64_t t = later(microsecond);
    const uint64_t end = t + us(headX_ / kReturnSpeed);
    record({Event::Kind::Motion, t, sheet_, {}, {}, {Motion::Kind::Carriage, t, end, headX_ + leftEdge(), leftEdge(), false}});
    ribbon_ += headX_ * kRibbonDrive;
    headX_ = 0.0;
    busyUntil_ = end;
}

void Dmp2000::printLine(uint64_t microsecond)
{
    if (line_.empty() && lineText_.empty()) {
        style_.lineDoubleWidth = false;
        return;
    }
    if (!sheetLoaded_) {
        line_.clear();
        lineText_.clear();
        return;  // nowhere to print: the paper-out lamp is lit
    }
    // How fast: 105 characters a second, but no needle twice in one half
    // dot's time; emphasized, NLQ and ESC "s" at half speed.
    double speed = kPrintSpeed;
    if (style_.condensed && !lineGraphics_)
        speed = std::min(speed, kColumnRate * columnSpacing());
    if (style_.bold || style_.nlq || halfSpeed_ || lineQuad_)
        speed /= 2.0;
    const bool oneWay = unidirectional_ || oneLineUnidirectional_ || lineGraphics_;
    int passes = 1;
    for (const Column& c : line_)
        passes = std::max(passes, c.pass + 1);

    const double origin = leftMarginInches_;
    double left = 1e9, right = -1e9;
    for (const Column& c : line_) {
        left = std::min(left, origin + c.x);
        right = std::max(right, origin + c.x);
    }
    for (const Printed& p : lineText_) {
        left = std::min(left, origin + p.x);
        right = std::max(right, origin + p.x + p.width);
    }
    uint64_t t = later(microsecond);
    for (int pass = 0; pass < passes; ++pass) {
        // Left to right, or from whichever end the head is nearer.
        const bool forward = oneWay || std::fabs(headX_ - left) <= std::fabs(headX_ - right);
        const double from = forward ? left : right, to = forward ? right : left;
        if (std::fabs(headX_ - from) > 1e-6) {
            const uint64_t end = t + us(std::fabs(headX_ - from) / kReturnSpeed);
            record({Event::Kind::Motion, t, sheet_, {}, {}, {Motion::Kind::Carriage, t, end, headX_ + leftEdge(), from + leftEdge(), false}});
            ribbon_ += std::fabs(headX_ - from) * kRibbonDrive;
            t = end;
        }
        const uint64_t end = t + us(std::fabs(to - from) / speed);
        record({Event::Kind::Motion, t, sheet_, {}, {}, {Motion::Kind::Carriage, t, end, from + leftEdge(), to + leftEdge(), true}});
        for (const Column& c : line_) {
            if (c.pass != pass)
                continue;
            const double x = origin + c.x;
            const uint64_t at = t + us(std::fabs(x - from) / speed);
            const float ink = ribbonInk(ribbon_ + std::fabs(x - from) * kRibbonDrive);
            record({Event::Kind::Strike, at, sheet_, {at, sheet_, x + leftEdge(), paperY_ + c.dy, c.pins, !forward, ink, slip_}});
        }
        if (pass == 0)
            for (Printed p : lineText_) {
                const double x = origin + p.x;
                p.microsecond = t + us(std::fabs(x - from) / speed);
                p.x = x + leftEdge();
                p.y = paperY_;
                p.sheet = sheet_;
                record({Event::Kind::Printed, p.microsecond, sheet_, {}, p});
            }
        ribbon_ += std::fabs(to - from) * kRibbonDrive;
        headX_ = to;
        t = end;
    }
    busyUntil_ = t;
    line_.clear();
    lineText_.clear();
    lineQuad_ = false;
    oneLineUnidirectional_ = false;
    style_.lineDoubleWidth = false;
}

void Dmp2000::startSheet(uint64_t microsecond)
{
    record({Event::Kind::SheetStarted, microsecond, sheet_});
}

void Dmp2000::feed(double inches, uint64_t microsecond)
{
    if (!sheetLoaded_ || std::fabs(inches) < 1e-9)
        return;
    // Skip over the perforation: a line that would fall in the page's
    // last lines goes to the next page's top.
    if (skipLines_ > 0 && inches > 0) {
        const double page = pageLines_ ? pageLines_ * lineSpacing() : pageLength_;
        const double next = sinceTopOfForm_ + inches;
        if (next > page - skipLines_ * lineSpacing() + 1e-9 && next < page)
            inches = page - sinceTopOfForm_;
    }
    uint64_t t = later(microsecond);
    double y = paperY_ + inches;
    const uint64_t end = t + us(std::fabs(inches) / kPaperSpeed);
    record({Event::Kind::Motion, t, sheet_, {}, {}, {Motion::Kind::Paper, t, end, paperY_, y, false}});
    busyUntil_ = end;
    slip_ = static_cast<float>(kPaperError) * paperError(++paperMoves_);
    sinceTopOfForm_ += inches;
    const double page = pageLines_ ? pageLines_ * lineSpacing() : pageLength_;
    while (sinceTopOfForm_ >= page - 1e-9)
        sinceTopOfForm_ -= page;
    if (sinceTopOfForm_ < 0)
        sinceTopOfForm_ += page;
    if (paper_.fanFold) {
        // Fan fold: past the perforation, the next sheet is under the head.
        while (y >= paper_.length - 1e-9) {
            record({Event::Kind::SheetDone, end, sheet_});
            ++sheet_;
            y -= paper_.length;
            startSheet(end);
        }
    } else if (y >= paper_.length - 1e-9) {
        // A cut sheet: out, and the lamp lit.
        record({Event::Kind::SheetDone, end, sheet_});
        sheetLoaded_ = false;
        paperOut_ = true;
        record({Event::Kind::PaperOut, end, sheet_});
        if (switches_.beeper)
            beep(end);
        y = 0.0;
    }
    paperY_ = std::max(0.0, y);
}

void Dmp2000::formFeed(uint64_t microsecond)
{
    if (!sheetLoaded_)
        return;
    if (!paper_.fanFold) {
        // The sheet goes out.
        feed(paper_.length - paperY_, microsecond);
        return;
    }
    const double page = pageLines_ ? pageLines_ * lineSpacing() : pageLength_;
    const double rest = page - sinceTopOfForm_;
    feed(rest < 1e-9 ? page : rest, microsecond);
}

void Dmp2000::beep(uint64_t microsecond)
{
    if (switches_.beeper)
        record({Event::Kind::Beep, later(microsecond), sheet_});
}

void Dmp2000::setOnline(bool online, uint64_t microsecond)
{
    (void)microsecond;
    online_ = online;
}

void Dmp2000::lineFeedButton(uint64_t microsecond)
{
    if (online_)
        return;
    if (!sheetLoaded_) {
        loadPaper(microsecond);  // LF takes the sheet in (manual, chapter 1 page 10)
        return;
    }
    feed(1.0 / 6.0, microsecond);
}

void Dmp2000::formFeedButton(uint64_t microsecond)
{
    if (!online_)
        formFeed(microsecond);
}

void Dmp2000::loadPaper(uint64_t microsecond)
{
    if (sheetLoaded_)
        return;
    ++sheet_;
    sheetLoaded_ = true;
    paperOut_ = false;
    const uint64_t t = later(microsecond);
    const double top = paper_.fanFold ? 0.0 : kTopOfSheet;
    const uint64_t end = t + us(top / kLoadSpeed);
    record({Event::Kind::Motion, t, sheet_, {}, {}, {Motion::Kind::Paper, t, end, 0.0, top, false}});
    startSheet(t);
    slip_ = static_cast<float>(kPaperError) * paperError(++paperMoves_);
    paperY_ = top;
    sinceTopOfForm_ = 0.0;
    busyUntil_ = end;
}

}  // namespace tuxape
