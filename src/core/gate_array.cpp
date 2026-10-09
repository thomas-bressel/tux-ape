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
    vsyncFrom_ = vsyncTail_ = 0;
    countsDue_ = vsyncDue_ = 0;
    interrupt_ = false;
    prevHsync_ = prevVsync_ = delayedHsync_ = hsync_ = false;
    rasterMatch_ = rasterDue_ = soundRound_ = false;
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
        // HSYNCs, whatever the CRTC's VSYNC does next. The picture goes
        // black part-way through the character on show, the last of the
        // line before with a VSYNC that comes as its row begins: from its
        // fifth pixel with an HD6845S or an MC6845, its sixth with a
        // UM6845R, its second with the 40226, and its first with the
        // Plus's ASIC (Compendium 16.2.1, which has nothing for the Plus:
        // a photograph of the Shaker's "VSYNC story" on one). Pixels are
        // counted as in render.
        if (!vsyncBlack_) {
            vsyncBlack_ = true;
            vsyncFrom_ = crtc.type() == CrtcType::UM6845R ? 5 : crtc.type() == CrtcType::PreAsic ? 1 : asic ? 0 : 4;
        }
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
        // The mode asked for is taken two microseconds into the pulse,
        // five pixels into the character that starts there, and from then
        // on at once for as long as the Gate Array's own pulse lasts. The
        // character where it is first taken shows the change, if R3 lets
        // any of it be seen (see render).
        if (hsyncAge_ >= 2 && hsyncAge_ <= kHsyncPulse && mode_ != (rmr_ & 3)) {
            if (hsyncAge_ == 2)
                switchedFrom_ = static_cast<int8_t>(mode_);
            mode_ = rmr_ & 3;
        }
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
        if (longEnough && hsyncAge_ <= kHsyncPulse) {
            // A pulse of two microseconds ends as the mode is taken: the
            // picture is back before the change is through, and the
            // character that follows shows both modes (see render).
            if (hsyncAge_ == 2 && mode_ != (rmr_ & 3))
                switchedFrom_ = static_cast<int8_t>(mode_);
            mode_ = rmr_ & 3;
        }
        hsyncAge_ = 0;
        // The end of every HSYNC, however short, advances the interrupt
        // counter.
        if (interruptDelay_ == 0)
            countHsync(monitor);
        else
            countsDue_ |= 1u << (interruptDelay_ - 1);
        // The black of the vertical sync is the picture's affair and ends
        // with the 26th HSYNC itself: with its blanking on the MC6845 and
        // the ASICs, a pixel after it on the HD6845S and the UM6845R
        // (16.2.1).
        if (vsyncBlack_ && ++blackLines_ == 26) {
            vsyncBlack_ = false;
            vsyncTail_ = crtc.type() == CrtcType::HD6845S || crtc.type() == CrtcType::UM6845R;
        }
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

void GateArray::vsyncStartedByWrite(const Crtc& crtc, bool early)
{
    // R7 made equal to C4 in mid-line (Compendium 16.2.1). The UM6845R and
    // the MC6845 raise their VSYNC within the microsecond, and the picture
    // goes black in the character then on show: from its fifth pixel after
    // a write of the OUTI kind, from its ninth after OUT (C),r. The HD6845S
    // is a microsecond slower, and the black starts in the next character
    // as it does at the start of a row: sync sees to it.
    if (crtc.type() != CrtcType::UM6845R && crtc.type() != CrtcType::MC6845)
        return;
    if (vsyncBlack_)
        return;
    vsyncBlack_ = true;
    vsyncFrom_ = early ? 4 : 8;
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
    //
    // These figures count the pixels of mode 2 as a Gate Array puts them
    // out, the first of a character being the one it shows a pixel early
    // (see below): on the screen, the blanking is one pixel to the left of
    // them, on every chip. With the MC6845 the picture comes back a pixel
    // later than it went (33 pixels of black for a pulse of two characters,
    // 32 on the others), and so it does with the 40226 (CRTC 4), which
    // blanks from its second pixel to the third of the character after the
    // pulse. The Plus's ASIC blanks from the first to the first.
    const bool preAsic = crtc.type() == CrtcType::PreAsic;
    const bool discrete = !plusAsic && !preAsic;
    const int usualLag = plusAsic ? 0 : preAsic ? 1 : crtc.type() == CrtcType::UM6845R ? 5 : crtc.type() == CrtcType::MC6845 ? 3 : 4;
    const int endLag = preAsic ? 2 : crtc.type() == CrtcType::MC6845 ? 4 : usualLag;
    const int lateLag = !discrete ? usualLag : crtc.type() == CrtcType::MC6845 ? 7 : 8;
    const int lag = lateBlanking_ ? lateLag : usualLag;
    const Crtc::HsyncCut cut = hsync_ ? crtc.hsyncCut() : Crtc::HsyncCut::None;
    int blackFrom = 0;
    int blackTo = 0;
    if (hsync_) {
        blackFrom = blankedBefore_ ? 0 : lag;
        blackTo = Monitor::kCellWidth;
        switch (cut) {
        case Crtc::HsyncCut::None: break;
        case Crtc::HsyncCut::AfterQuarter: blackTo = crtc.type() == CrtcType::MC6845 ? 7 : 8; break;
        case Crtc::HsyncCut::SecondHalf: blackTo = 8; break;
        case Crtc::HsyncCut::AtStart: blackTo = blankedBefore_ ? endLag : 0; break;
        case Crtc::HsyncCut::Never: blackTo = 0; break;
        }
    } else if (blankedBefore_) {
        blackTo = endLag + vsyncTail_;
    }
    const bool wasBlanked = blankedBefore_;
    blankedBefore_ = hsync_ && cut == Crtc::HsyncCut::None;
    lateBlanking_ = false;
    vsyncTail_ = 0;
    // A pulse that R3 ended during its third character or later has lasted
    // long enough to set the mode, and the picture that comes back within
    // that character is already in the new one (9.3.4.2).
    int switched = switchedFrom_;
    if (switched >= 0) [[unlikely]]
        switchedFrom_ = -1;
    if ((cut == Crtc::HsyncCut::AfterQuarter || cut == Crtc::HsyncCut::AtStart) && hsyncAge_ >= 3
        && hsyncAge_ <= kHsyncPulse) {
        // Ended on a character's edge, the pulse leaves the change to show
        // in this character as any short pulse does in the one after it.
        if (cut == Crtc::HsyncCut::AtStart && mode_ != (rmr_ & 3))
            switched = mode_;
        mode_ = rmr_ & 3;
    }
    if (vsyncBlack_) {
        // The first character of the vertical sync's black is black from
        // where the Gate Array heard of the VSYNC (see sync), or from where
        // the HSYNC's blanking has it, if that is sooner.
        const int from = vsyncFrom_;
        vsyncFrom_ = 0;
        blackFrom = blackFrom < blackTo && blackFrom < from ? blackFrom : from;
        blackTo = Monitor::kCellWidth;
    }

    if (uint32_t* out = monitor.cell()) {
        // The blanking is a pixel ahead of the picture, and so are, on a
        // Gate Array, the pixels of mode 2, which it shows one sooner than
        // those of the other modes (Compendium 9.1; the Plus's ASIC keeps
        // its modes in line). What comes out early for a character starts
        // where the character before it ended (`before`), unless that is
        // off the screen.
        auto before = [&]() -> uint32_t* { return monitor.beamColumn() > 0 ? out - 1 : &offScreen_; };
        if (blackFrom == 0 && blackTo == Monitor::kCellWidth) {
            *before() = kBlack;
            for (int i = 0; i < Monitor::kCellWidth; ++i)
                out[i] = kBlack;
            // The character's own last pixel is not under its blanking.
            // The Plus's ASIC, whose pulse ends with the character, shows
            // it when the picture comes back.
            tailByte_ = fetched_[1];
            tailShown_ = fetchedDisplay_[1];
        } else if (switched >= 0 && !plusAsic && !plus_) [[unlikely]] {
            drawModeSwitch(out, before(), switched, blackFrom, blackTo, preAsic ? 3 : 5);
        } else {
            if (wasBlanked && blackTo == 0 && plusAsic)
                *before() = rgb_[tailShown_ ? kPenTable.pens[mode_][tailByte_][7] : kBorder];
            // An ink set during this microsecond shows from the middle of
            // the character on a Gate Array, five pixels into it on the
            // Plus's ASIC, whatever the mode: a pixel of mode 0 or 1 can
            // change colour part-way (Compendium 9.2.2; the Shaker's
            // "Gate Array inkerisation").
            const int split = inkChanged_ ? (plusAsic ? 5 : 8) : 0;
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
                // Mode 2, picture and border alike: the character's first
                // pixel goes where the one before it ended, and its last
                // place is filled again by the character that follows. An
                // ink set during the microsecond still changes at the same
                // place on the screen, a pixel further into the character,
                // except on the 40226, where it moves with the pixels
                // (9.2.2).
                const int from = split == 0 || preAsic ? split : split + 1;
                *before() = (0 < from ? rgbBefore_ : rgb_)[fetchedDisplay_[0] ? left[0] : kBorder];
                for (int i = 1; i < 8; ++i)
                    out[i - 1] = (i < from ? rgbBefore_ : rgb_)[fetchedDisplay_[0] ? left[i] : kBorder];
                for (int i = 0; i < 8; ++i)
                    out[i + 7] = (i + 8 < from ? rgbBefore_ : rgb_)[fetchedDisplay_[1] ? right[i] : kBorder];
                out[15] = out[14];
            }
            if (blackFrom < blackTo) {
                if (blackFrom == 0)
                    *before() = kBlack;
                for (int i = blackFrom > 0 ? blackFrom - 1 : 0; i < blackTo - 1; ++i)
                    out[i] = kBlack;
            }
        }
    }
    inkChanged_ = false;

    // Fetch the character the CRTC is pointing at; it is drawn next time.
    // The refresh address and the raster line together address 64K:
    // MA13-12 pick the 16K block, RA2-0 the line within the row.
    fetchedDisplay_[0] = crtc.displayEnable(0);
    fetchedDisplay_[1] = crtc.displayEnable(1);
    const uint16_t ma = crtc.ma();
    unsigned ra = crtc.ra() & 7u;
    if (plus_) {
        // The Plus's vertical scroll shows each line of the row from
        // further down, going round within the row: the CRTC brings the
        // next row in when the line shown is the last (see
        // Crtc::setScrollLines).
        ra = (ra + (asic_->scroll() >> 4 & 7u)) & 7u;
        fetchedFirst_ = crtc.hcc() == 0;
        fetchedX_ = crtc.hcc() * Monitor::kCellWidth;
        fetchedY_ = crtc.frameLine();
    }
    const unsigned addr = (ma & 0x3000u) << 2 | ra << 11 | (ma & 0x3FFu) << 1;
    fetched_[0] = videoRam[addr];
    fetched_[1] = videoRam[addr | 1];

    monitor.advance();
}

// The character in which a Gate Array changes screen mode, after an HSYNC
// too short to hide it (Compendium 9.3.4.3, and 9.3.4.5 for the 40226). The
// chip goes on shifting the byte it was showing, and the new mode takes it
// as the old one has left it. A byte is shifted as each of its pixels ends:
// at every pixel in mode 2, whose first comes a pixel early; at the third,
// fifth and seventh pixel of the byte in mode 1; at the fifth in modes 0
// and 3. The change comes on the character's sixth pixel (its fourth on
// the 40226); the bits that come in at the other end of the byte are zeros.
// What the new mode makes of the byte from there fills the rest of its
// place, which in mode 2 ends a pixel sooner. Before the change, whatever
// the blanking leaves is still in the old mode: one pixel, or none with the
// UM6845R, which blanks a pixel more.
void GateArray::drawModeSwitch(uint32_t* out, uint32_t* before, int was, int blackFrom, int blackTo, int change)
{
    // How many times a mode has shifted the byte by a given pixel.
    auto shifts = [](int mode, int at) { return mode == 2 ? at : mode == 1 ? (at - 1) / 2 : (at - 1) / 4; };
    // Pens along the character, one for each pixel of mode 2 the chip puts
    // out: the first is the one that goes a pixel early, the seventeenth
    // falls where the next character's first will.
    uint8_t pens[17];
    const uint8_t first = fetched_[0];
    const uint8_t second = fetched_[1];
    const uint8_t* old = kPenTable.pens[was][first];
    pens[0] = was == 2 ? old[0] : 0;
    for (int i = 1; i < change; ++i)
        pens[i] = old[was == 2 ? i : i - 1];
    const int done = shifts(was, change) - shifts(mode_, change);
    const int end = mode_ == 2 ? 8 : 9;
    for (int i = change; i < end; ++i)
        pens[i] = kPenTable.pens[mode_][static_cast<uint8_t>(first << (done + shifts(mode_, i)))][0];
    const uint8_t* rest = kPenTable.pens[mode_][second];
    for (int i = 0; i < 8; ++i)
        pens[end + i] = rest[i];
    if (mode_ == 2)
        pens[16] = pens[15];
    for (int i = 0; i < 17; ++i) {
        const bool shown = fetchedDisplay_[i < end ? 0 : 1];
        (i == 0 ? *before : out[i - 1]) = i >= blackFrom && i < blackTo ? kBlack : rgb_[shown ? pens[i] : kBorder];
    }
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
