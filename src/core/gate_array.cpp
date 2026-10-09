#include "core/gate_array.h"

#include <array>
#include <cstring>

#include "core/asic.h"
#include "core/crtc.h"
#include "core/monitor.h"

namespace tuxape {

namespace {

// Red, green and blue level (0, 1 or 2) of each of the 32 hardware colour
// numbers. Five of them duplicate other entries.
constexpr uint8_t kLevels[32][3] = {
    {1, 1, 1}, {1, 1, 1}, {0, 2, 1}, {2, 2, 1}, {0, 0, 1}, {2, 0, 1}, {0, 1, 1}, {2, 1, 1},
    {2, 0, 1}, {2, 2, 1}, {2, 2, 0}, {2, 2, 2}, {2, 0, 0}, {2, 0, 2}, {2, 1, 0}, {2, 1, 2},
    {0, 0, 1}, {0, 2, 1}, {0, 2, 0}, {0, 2, 2}, {0, 0, 0}, {0, 0, 2}, {0, 1, 0}, {0, 1, 2},
    {1, 0, 1}, {1, 2, 1}, {1, 2, 0}, {1, 2, 2}, {1, 0, 0}, {1, 0, 2}, {1, 1, 0}, {1, 1, 2},
};

// For each mode and video byte, the pen of each of the 8 mode-2 pixel
// positions it covers.
struct PenTable {
    uint8_t pens[4][256][8];

    constexpr PenTable()
        : pens{}
    {
        for (int b = 0; b < 256; ++b) {
            auto bit = [b](int n) { return (b >> n) & 1; };
            // Mode 0: two pixels, four bits each, interleaved.
            const int m0[2] = {
                bit(7) | bit(3) << 1 | bit(5) << 2 | bit(1) << 3,
                bit(6) | bit(2) << 1 | bit(4) << 2 | bit(0) << 3,
            };
            // Mode 1: four pixels, two bits each.
            const int m1[4] = {
                bit(7) | bit(3) << 1,
                bit(6) | bit(2) << 1,
                bit(5) | bit(1) << 1,
                bit(4) | bit(0) << 1,
            };
            // Mode 3 (undocumented): mode 0's width with mode 1's colours.
            const int m3[2] = {m1[0], m1[1]};
            for (int x = 0; x < 8; ++x) {
                pens[0][b][x] = static_cast<uint8_t>(m0[x / 4]);
                pens[1][b][x] = static_cast<uint8_t>(m1[x / 2]);
                pens[2][b][x] = static_cast<uint8_t>(bit(7 - x));
                pens[3][b][x] = static_cast<uint8_t>(m3[x / 4]);
            }
        }
    }
};

constexpr PenTable kPenTable{};

constexpr int kBorder = 16;
constexpr uint32_t kBlack = 0xFF000000;
constexpr int kHsyncPulse = 6;  // longest the Gate Array's own HSYNC pulse gets, in microseconds

}  // namespace

GateArray::GateArray()
{
    setMonitor(MonitorKind::Colour, false);
    reset();
}

void GateArray::reset()
{
    rmr_ = 0;
    mode_ = 0;
    pen_ = 0;
    for (int i = 0; i < 17; ++i) {
        ink_[i] = 0;
        rgb_[i] = colours_[0];
    }
    inkChanged_ = false;
    fetched_[0] = fetched_[1] = 0;
    fetchedDisplay_[0] = fetchedDisplay_[1] = false;
    r52_ = 0;
    hsyncAge_ = 0;
    vsyncLines_ = 0;
    vsyncSequence_ = vsyncBlack_ = false;
    blackLines_ = 0;
    countsDue_ = vsyncDue_ = 0;
    interrupt_ = false;
    prevHsync_ = prevVsync_ = delayedHsync_ = hsync_ = false;
    rasterMatch_ = rasterDue_ = soundRound_ = false;
    lastPixelBlack_ = false;
}

uint32_t GateArray::monitorColour(int hardwareColour, MonitorKind kind, bool linear, int brightness)
{
    const int level[3] = {0x00, linear ? 0x66 : 0x80, 0xFF};
    const uint8_t* l = kLevels[hardwareColour & 31];
    int r = level[l[0]], g = level[l[1]], b = level[l[2]];
    if (kind != MonitorKind::Colour) {
        // A monochrome tube shows the firmware's 27 colours as evenly
        // spaced brightness steps: green weighs 9, red 3, blue 1.
        const int luma = (l[1] * 9 + l[0] * 3 + l[2]) * 255 / 26;
        if (kind == MonitorKind::Green) {
            r = 0;
            g = luma;
            b = 0;
        } else {
            r = g = b = luma;
        }
    }
    // The brightness knob moves the whole picture up or down, black
    // included, as it does on a real tube.
    const int shift = brightness * 128 / 100;
    auto adjusted = [shift](int value) { return static_cast<uint32_t>(value + shift < 0 ? 0 : value + shift > 255 ? 255 : value + shift); };
    if (kind == MonitorKind::Green)
        return 0xFF000000 | adjusted(g) << 8;
    return 0xFF000000 | adjusted(r) << 16 | adjusted(g) << 8 | adjusted(b);
}

uint16_t GateArray::plusColour(int hardwareColour)
{
    // The CPC's three levels are 0, 6 and 15 of the ASIC's sixteen.
    static constexpr int kLevel12[3] = {0x0, 0x6, 0xF};
    const uint8_t* l = kLevels[hardwareColour & 31];
    return static_cast<uint16_t>(kLevel12[l[1]] << 8 | kLevel12[l[0]] << 4 | kLevel12[l[2]]);
}

uint32_t GateArray::monitorColour12(uint16_t grb, MonitorKind kind, int brightness)
{
    const int g4 = grb >> 8 & 0xF, r4 = grb >> 4 & 0xF, b4 = grb & 0xF;
    int r = r4 * 17, g = g4 * 17, b = b4 * 17;
    if (kind != MonitorKind::Colour) {
        const int luma = (g4 * 9 + r4 * 3 + b4) * 255 / (13 * 15);
        r = b = kind == MonitorKind::Green ? 0 : luma;
        g = luma;
    }
    const int shift = brightness * 128 / 100;
    auto adjusted = [shift](int value) { return static_cast<uint32_t>(value + shift < 0 ? 0 : value + shift > 255 ? 255 : value + shift); };
    if (kind == MonitorKind::Green)
        return 0xFF000000 | adjusted(g) << 8;
    return 0xFF000000 | adjusted(r) << 16 | adjusted(g) << 8 | adjusted(b);
}

void GateArray::setMonitor(MonitorKind kind, bool linear, int brightness)
{
    monitorKind_ = kind;
    brightness_ = brightness;
    for (int c = 0; c < 32; ++c)
        colours_[c] = monitorColour(c, kind, linear, brightness);
    for (int i = 0; i < 17; ++i)
        rgb_[i] = plus_ ? monitorColour12(plus12_[i], kind, brightness) : colours_[ink_[i]];
    for (int i = 1; i < 16; ++i)
        spriteRgb_[i] = monitorColour12(plus12_[16 + i], kind, brightness);
}

void GateArray::setPlus(bool plus)
{
    plus_ = plus;
    for (int i = 0; i < 17; ++i)
        rgb_[i] = plus_ ? monitorColour12(plus12_[i], monitorKind_, brightness_) : colours_[ink_[i]];
}

void GateArray::setPlusColour(int index, uint16_t grb)
{
    if (!plus_)
        return;
    index &= 31;
    plus12_[index] = grb;
    const uint32_t rgb = monitorColour12(grb, monitorKind_, brightness_);
    if (index > 16) {
        spriteRgb_[index - 16] = rgb;
        return;
    }
    if (!inkChanged_) {
        std::memcpy(rgbBefore_, rgb_, sizeof rgb_);
        inkChanged_ = true;
    }
    rgb_[index] = rgb;
}

void GateArray::write(uint8_t value)
{
    switch (value >> 6) {
    case 0:  // select pen
        pen_ = (value & 0x10) ? kBorder : value & 0x0F;
        break;
    case 1:  // set the colour of the selected pen
        if (!inkChanged_) {
            std::memcpy(rgbBefore_, rgb_, sizeof rgb_);
            inkChanged_ = true;
        }
        ink_[pen_] = value & 0x1F;
        rgb_[pen_] = colours_[value & 0x1F];
        break;
    case 2:  // screen mode, ROM enables, interrupt counter reset
        rmr_ = value & 0x1F;
        if (value & 0x10) {
            r52_ = 0;
            interrupt_ = false;
        }
        break;
    }
}

void GateArray::restore(uint8_t pen, const uint8_t* inks, uint8_t romAndMode, uint8_t interruptCounter, bool interrupt)
{
    pen_ = pen > kBorder ? kBorder : pen;
    for (int i = 0; i < 17; ++i) {
        ink_[i] = inks[i] & 0x1F;
        rgb_[i] = colours_[ink_[i]];
    }
    if (plus_)
        for (int i = 0; i < 17; ++i)
            setPlusColour(i, plusColour(ink_[i]));
    inkChanged_ = false;
    rmr_ = romAndMode & 0x0F;
    mode_ = rmr_ & 3;
    r52_ = interruptCounter % 52;
    interrupt_ = interrupt;
}

void GateArray::rasterLineRestored(const Crtc& crtc)
{
    const uint8_t line = asic_->rasterInterruptLine();
    rasterMatch_ = crtc.hsync() && line != 0 && crtc.asicLine() == line;
    rasterDue_ = false;
}

void GateArray::acknowledgeInterrupt()
{
    // Clearing bit 5 guarantees the next interrupt is at least 32 lines away.
    r52_ &= 0x1F;
    interrupt_ = false;
}

// The Plus's raster interrupt, looked at every microsecond.
void GateArray::rasterInterrupt(const Crtc& crtc, bool hsync)
{
    // The Plus's raster interrupt: asked for a microsecond after "HSYNC,
    // on the line set" has become true. That is as the HSYNC of the
    // line starts; but also as the line begins, if the previous line's
    // HSYNC is still on, and the line's own HSYNC then asks a second
    // time. For this the pulse counts from the moment the CRTC starts
    // it to a microsecond after it has ended: one that ends with its
    // line is still seen on the next, and one that starts on a line's
    // last character belongs to that line. ("pritest", "PRI trigger
    // bug": on a real machine, two interrupts a frame for every R2 from
    // 64 minus the width up, 63 included.)
    if (rasterDue_) {
        rasterDue_ = false;
        asic_->raiseRasterInterrupt();
        interrupt_ = true;
    }
    const uint8_t line = asic_->rasterInterruptLine();
    const bool match = (hsync || crtc.hsync()) && line != 0 && crtc.asicLine() == line;
    rasterDue_ = match && !rasterMatch_;
    rasterMatch_ = match;

    // The sound channels, while their round for this line lasts.
    if (soundRound_) [[unlikely]]
        soundRound_ = asic_->soundStep();
}

void GateArray::sync(const Crtc& crtc, Monitor& monitor)
{
    const bool asic = crtc.type() == CrtcType::AsicPlus || crtc.type() == CrtcType::PreAsic;

    // A discrete Gate Array reacts to the CRTC's HSYNC at once, a character
    // ahead of the picture it is still drawing. The ASICs keep the two in
    // step, so there everything tied to HSYNC comes a microsecond later.
    const bool hsync = asic ? delayedHsync_ : crtc.hsync();
    delayedHsync_ = crtc.hsync();
    const bool vsync = crtc.vsync();

    // What a delay has kept back, when its time has come.
    if (vsyncDue_ & 1) {
        vsyncLines_ = 0;
        vsyncSequence_ = true;
    }
    if (countsDue_ & 1)
        countHsync(monitor);
    vsyncDue_ >>= 1;
    countsDue_ >>= 1;

    if (vsync && !prevVsync_) {
        // From here the Gate Array times the vertical sync itself, counting
        // HSYNCs, whatever the CRTC's VSYNC does next.
        vsyncBlack_ = true;
        blackLines_ = 0;
        if (interruptDelay_ == 0) {
            vsyncLines_ = 0;
            vsyncSequence_ = true;
        } else {
            vsyncDue_ |= 1u << (interruptDelay_ - 1);
        }
    }

    if (plus_) [[unlikely]]
        rasterInterrupt(crtc, hsync);

    if (hsync) {
        // Of the CRTC's HSYNC the Gate Array makes a pulse of its own, six
        // microseconds at most. Two microseconds in, it starts the sync
        // pulse sent to the monitor; when its pulse ends, the requested
        // screen mode takes effect: a mode asked for during the first six
        // microseconds of a long HSYNC is still in time (Compendium 9.3.2,
        // Shaker "Gate Array moderisation").
        // Its sound channels take an instruction each on every line. An
        // HSYNC that begins on the character where the one before ended
        // starts a round like any other, though the pulse never dropped:
        // with the width at 15, lines of 3, 5 and 15 characters still have
        // their channels served (Kevin Thacker's "dmatest", "CRTC R0 length
        // and dma").
        if (plus_ && (hsyncAge_ == 0 || crtc.hsyncJoinedJustNow()))
            soundRound_ = asic_->soundTick();
        if (hsyncAge_ == 2)
            monitor.hsync(asic);
        if (hsyncAge_ == kHsyncPulse)
            mode_ = rmr_ & 3;
        if (hsyncAge_ < 0xFF)
            ++hsyncAge_;
    } else if (prevHsync_ && crtc.previousHsyncCut() == Crtc::HsyncCut::Never) {
        // R3 was cleared just in time: there has been no pulse.
        hsyncAge_ = 0;
    } else if (prevHsync_) {
        // A shorter HSYNC sets the mode as it ends, provided it lasted two
        // microseconds; one that R3 cut short during its second character
        // does not (9.3.4.1).
        const bool longEnough = hsyncAge_ > 2 || (hsyncAge_ == 2 && crtc.previousHsyncCut() == Crtc::HsyncCut::None);
        if (longEnough && hsyncAge_ <= kHsyncPulse)
            mode_ = rmr_ & 3;
        hsyncAge_ = 0;
        // The end of every HSYNC, however short, advances the interrupt
        // counter.
        if (interruptDelay_ == 0)
            countHsync(monitor);
        else
            countsDue_ |= 1u << (interruptDelay_ - 1);
        // The black of the vertical sync is the picture's affair and ends
        // with the 26th HSYNC itself.
        if (vsyncBlack_ && ++blackLines_ == 26)
            vsyncBlack_ = false;
    }
    prevHsync_ = hsync;
    prevVsync_ = vsync;
    hsync_ = hsync;
}

void GateArray::countHsync(Monitor& monitor)
{
    // With a line set for the Plus's raster interrupt, the counter goes on
    // counting but interrupts no more.
    const bool ownInterrupts = !(plus_ && asic_->rasterInterruptLine() != 0);
    if (++r52_ == 52) {
        r52_ = 0;
        if (ownInterrupts)
            interrupt_ = true;
    }
    if (vsyncSequence_) {
        ++vsyncLines_;
        if (vsyncLines_ == 2) {
            // The monitor's vertical sync starts here. The interrupt
            // counter is brought into step with the frame, with an
            // interrupt if the previous one is far enough away.
            monitor.vsync();
            if (r52_ >= 32 && ownInterrupts)
                interrupt_ = true;
            r52_ = 0;
        } else if (vsyncLines_ == 26) {
            vsyncSequence_ = false;
        }
    }
}

void GateArray::hsyncStartedByWrite(const Crtc& crtc, bool late)
{
    // For everything but the blanking, the pulse counts from the start of
    // the character, as one programmed in advance would (Compendium 14.7.1).
    if (crtc.type() == CrtcType::AsicPlus || crtc.type() == CrtcType::PreAsic) {
        delayedHsync_ = true;
        return;
    }
    if (hsync_)
        return;
    hsync_ = prevHsync_ = true;
    hsyncAge_ = 1;
    lateBlanking_ = late;
}

void GateArray::hsyncEndedByWrite(const Crtc& crtc)
{
    // The ASICs' pulse, which is seen here a character late, has had its
    // last character: the one in progress. (The discrete chips' cuts are
    // told through Crtc::hsyncCut.)
    if (crtc.type() == CrtcType::AsicPlus || crtc.type() == CrtcType::PreAsic)
        delayedHsync_ = false;
}

void GateArray::render(const Crtc& crtc, const uint8_t* videoRam, Monitor& monitor)
{
    const bool plusAsic = crtc.type() == CrtcType::AsicPlus;

    // HSYNC blanks the picture. The blanking trails the pulse by a quarter
    // of a character (a little more with the UM6845R), and where R3 cut the
    // pulse short it stops part-way through the character (Compendium
    // 9.3.4.2 and 14.5.4; the Shaker's "R3 JIT" screens show each case).
    // Started by an R2 write that came after the chip had looked at the
    // character, the blanking begins three or four pixels further on.
    const bool discrete = crtc.type() != CrtcType::AsicPlus && crtc.type() != CrtcType::PreAsic;
    const int usualLag = !discrete ? 0 : crtc.type() == CrtcType::UM6845R ? 5 : crtc.type() == CrtcType::MC6845 ? 3 : 4;
    const int lateLag = crtc.type() == CrtcType::MC6845 ? 7 : 8;
    const int lag = lateBlanking_ ? lateLag : usualLag;
    const Crtc::HsyncCut cut = hsync_ ? crtc.hsyncCut() : Crtc::HsyncCut::None;
    int blackFrom = 0;
    int blackTo = 0;
    if (hsync_) {
        blackFrom = blankedBefore_ ? 0 : lag;
        blackTo = Monitor::kCellWidth;
        switch (cut) {
        case Crtc::HsyncCut::None: break;
        case Crtc::HsyncCut::AfterQuarter: blackTo = 8; break;
        case Crtc::HsyncCut::SecondHalf: blackTo = 8; break;
        case Crtc::HsyncCut::AtStart: blackTo = blankedBefore_ ? usualLag : 0; break;
        case Crtc::HsyncCut::Never: blackTo = 0; break;
        }
    } else if (blankedBefore_) {
        blackTo = usualLag;
    }
    blankedBefore_ = hsync_ && cut == Crtc::HsyncCut::None;
    lateBlanking_ = false;
    // A pulse that R3 ended during its third character or later has lasted
    // long enough to set the mode, and the picture that comes back within
    // that character is already in the new one (9.3.4.2).
    if ((cut == Crtc::HsyncCut::AfterQuarter || cut == Crtc::HsyncCut::AtStart) && hsyncAge_ >= 3
        && hsyncAge_ <= kHsyncPulse)
        mode_ = rmr_ & 3;
    if (vsyncBlack_) {
        blackFrom = 0;
        blackTo = Monitor::kCellWidth;
    }

    if (uint32_t* out = monitor.cell()) {
        if (blackFrom == 0 && blackTo == Monitor::kCellWidth) {
            for (int i = 0; i < Monitor::kCellWidth; ++i)
                out[i] = kBlack;
        } else {
            // An ink set during this microsecond shows from the middle of
            // the character on a Gate Array, from a quarter of the way in
            // on the Plus ASIC.
            const int split = inkChanged_ ? (plusAsic ? 4 : 8) : 0;
            // Each byte is either picture or border.
            const uint8_t* left = kPenTable.pens[mode_][fetched_[0]];
            const uint8_t* right = kPenTable.pens[mode_][fetched_[1]];
            if (plus_) {
                drawPlus(out, left, right, split);
            } else if (mode_ != 2 || plusAsic) {
                for (int i = 0; i < 8; ++i) {
                    out[i] = (i < split ? rgbBefore_ : rgb_)[fetchedDisplay_[0] ? left[i] : kBorder];
                    out[i + 8] = (i + 8 < split ? rgbBefore_ : rgb_)[fetchedDisplay_[1] ? right[i] : kBorder];
                }
            } else {
                // A Gate Array is a pixel early in mode 2: picture and
                // border alike come out one pixel sooner than in the other
                // modes (Compendium 9.1; the Plus's ASIC keeps its modes
                // in line). The character's first pixel therefore goes
                // where the one before it ended, unless that is off the
                // screen or blanked, and its last place is filled again
                // by the character that follows. An ink set during the
                // microsecond still changes at the same place on the
                // screen, a pixel further into the character, except on
                // the 40226 (CRTC 4), where it moves with the pixels
                // (9.2.2).
                const int from = split == 0 || crtc.type() == CrtcType::PreAsic ? split : split + 1;
                uint32_t pixels[16];
                for (int i = 0; i < 8; ++i) {
                    pixels[i] = (i < from ? rgbBefore_ : rgb_)[fetchedDisplay_[0] ? left[i] : kBorder];
                    pixels[i + 8] = (i + 8 < from ? rgbBefore_ : rgb_)[fetchedDisplay_[1] ? right[i] : kBorder];
                }
                if (!lastPixelBlack_ && monitor.beamColumn() > 0)
                    out[-1] = pixels[0];
                for (int i = 1; i < 16; ++i)
                    out[i - 1] = pixels[i];
                out[15] = pixels[15];
            }
            for (int i = blackFrom; i < blackTo; ++i)
                out[i] = kBlack;
        }
    }
    lastPixelBlack_ = blackFrom < blackTo && blackTo == Monitor::kCellWidth;
    inkChanged_ = false;

    // Fetch the character the CRTC is pointing at; it is drawn next time.
    // The refresh address and the raster line together address 64K:
    // MA13-12 pick the 16K block, RA2-0 the line within the row.
    fetchedDisplay_[0] = crtc.displayEnable(0);
    fetchedDisplay_[1] = crtc.displayEnable(1);
    uint16_t ma = crtc.ma();
    unsigned ra = crtc.ra() & 7u;
    if (plus_) {
        // The Plus's vertical scroll shows each row from a line further
        // down, the lines missing at its foot being the first of the row
        // below.
        ra += asic_->scroll() >> 4 & 7u;
        if (ra > 7) {
            ra &= 7;
            ma = static_cast<uint16_t>(ma + crtc.reg(1));
        }
        fetchedFirst_ = crtc.hcc() == 0;
        fetchedX_ = crtc.hcc() * Monitor::kCellWidth;
        fetchedY_ = crtc.frameLine();
    }
    const unsigned addr = (ma & 0x3000u) << 2 | ra << 11 | (ma & 0x3FFu) << 1;
    fetched_[0] = videoRam[addr];
    fetched_[1] = videoRam[addr | 1];

    monitor.advance();
}

// A character of a Plus's picture: the screen, moved to the right by the
// soft scroll, then the sprites on top of it.
void GateArray::drawPlus(uint32_t* out, const uint8_t* left, const uint8_t* right, int split)
{
    // What the memory holds, shown or not: the scroll delays that, and the
    // border is laid over the result where the CRTC says so. The picture
    // therefore still ends where the display does, its last pixels lost,
    // and begins with the end of whatever was read before the line.
    uint8_t pens[16];
    for (int i = 0; i < 8; ++i) {
        pens[i] = left[i];
        pens[i + 8] = right[i];
    }
    const uint8_t scroll = asic_->scroll();
    const int shift = scroll & 0x0F;
    // The border can be drawn over the first character of each line, to
    // hide where the scrolled picture comes in. It is border like the
    // rest, which Amstrad's description puts in front of the sprites: they
    // do not show on it either. The trees of Eerie Forest and the zombies
    // of Ghosts'n Goblins (2018) come in from behind it.
    const bool masked = (scroll & 0x80) && fetchedFirst_;
    for (int i = 0; i < 16; ++i) {
        const bool shown = fetchedDisplay_[i >> 3] && !masked;
        const uint8_t pen = !shown ? static_cast<uint8_t>(kBorder) : i < shift ? pensBefore_[16 - shift + i] : pens[i - shift];
        out[i] = (i < split ? rgbBefore_ : rgb_)[pen];
    }
    std::memcpy(pensBefore_, pens, sizeof pens);

    if (!asic_->spritesShown() || masked || !(fetchedDisplay_[0] || fetchedDisplay_[1]))
        return;
    unsigned covered = 0;
    for (int number = 0; number < 16 && covered != 0xFFFF; ++number) {
        const Asic::Sprite& sprite = asic_->sprite(number);
        if (sprite.magX == 0 || sprite.magY == 0)
            continue;
        const int dy = fetchedY_ - sprite.y;
        const int dx = fetchedX_ - sprite.x;
        if (dy < 0 || dy >= 16 * sprite.magY || dx + 15 < 0 || dx >= 16 * sprite.magX)
            continue;
        const int row = dy / sprite.magY;
        for (int i = 0; i < 16; ++i) {
            const int x = dx + i;
            if (x < 0 || x >= 16 * sprite.magX || (covered >> i & 1) || !fetchedDisplay_[i >> 3])
                continue;
            if (const uint8_t pixel = asic_->spritePixel(number, x / sprite.magX, row)) {
                out[i] = spriteRgb_[pixel];
                covered |= 1u << i;
            }
        }
    }
}

}  // namespace tuxape
