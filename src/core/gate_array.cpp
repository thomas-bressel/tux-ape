#include "core/gate_array.h"

#include <array>
#include <cstring>

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
    fetchedDisplay_ = false;
    r52_ = 0;
    hsyncAge_ = 0;
    vsyncLines_ = 0;
    vsyncSequence_ = vsyncBlack_ = false;
    interrupt_ = false;
    prevHsync_ = prevVsync_ = delayedHsync_ = hsync_ = false;
}

void GateArray::setMonitor(MonitorKind kind, bool linear)
{
    const int level[3] = {0x00, linear ? 0x66 : 0x80, 0xFF};
    for (int c = 0; c < 32; ++c) {
        const uint8_t* l = kLevels[c];
        uint32_t r = level[l[0]], g = level[l[1]], b = level[l[2]];
        if (kind != MonitorKind::Colour) {
            // A monochrome tube shows the firmware's 27 colours as evenly
            // spaced brightness steps: green weighs 9, red 3, blue 1.
            const uint32_t luma = (l[1] * 9 + l[0] * 3 + l[2]) * 255 / 26;
            if (kind == MonitorKind::Green) {
                r = 0;
                g = luma;
                b = 0;
            } else {
                r = g = b = luma;
            }
        }
        colours_[c] = 0xFF000000 | r << 16 | g << 8 | b;
    }
    for (int i = 0; i < 17; ++i)
        rgb_[i] = colours_[ink_[i]];
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

void GateArray::acknowledgeInterrupt()
{
    // Clearing bit 5 guarantees the next interrupt is at least 32 lines away.
    r52_ &= 0x1F;
    interrupt_ = false;
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

    if (vsync && !prevVsync_) {
        // From here the Gate Array times the vertical sync itself, counting
        // HSYNCs, whatever the CRTC's VSYNC does next.
        vsyncLines_ = 0;
        vsyncSequence_ = true;
        vsyncBlack_ = true;
    }

    if (hsync) {
        // Two microseconds into the HSYNC the monitor's sync pulse starts
        // and the requested screen mode takes effect. Shorter pulses do
        // neither.
        if (hsyncAge_ == 2) {
            monitor.hsync();
            mode_ = rmr_ & 3;
        }
        if (hsyncAge_ < 0xFF)
            ++hsyncAge_;
    } else if (prevHsync_ && crtc.previousHsyncCut() == Crtc::HsyncCut::Never) {
        // R3 was cleared just in time: there has been no pulse.
        hsyncAge_ = 0;
    } else if (prevHsync_) {
        // A pulse of exactly two microseconds still sets the mode; one that
        // R3 ended during its second character does not.
        if (hsyncAge_ == 2 && crtc.previousHsyncCut() == Crtc::HsyncCut::None)
            mode_ = rmr_ & 3;
        hsyncAge_ = 0;
        // The end of every HSYNC, however short, advances the interrupt
        // counter.
        if (++r52_ == 52) {
            r52_ = 0;
            interrupt_ = true;
        }
        if (vsyncSequence_) {
            ++vsyncLines_;
            if (vsyncLines_ == 2) {
                // The monitor's vertical sync starts here. The interrupt
                // counter is brought into step with the frame, with an
                // interrupt if the previous one is far enough away.
                monitor.vsync();
                if (r52_ >= 32)
                    interrupt_ = true;
                r52_ = 0;
            } else if (vsyncLines_ == 26) {
                vsyncBlack_ = false;
                vsyncSequence_ = false;
            }
        }
    }
    prevHsync_ = hsync;
    prevVsync_ = vsync;
    hsync_ = hsync;
}

void GateArray::render(const Crtc& crtc, const uint8_t* videoRam, Monitor& monitor)
{
    const bool plusAsic = crtc.type() == CrtcType::AsicPlus;

    // HSYNC blanks the picture. The blanking trails the pulse by a quarter
    // of a character (a little more with the UM6845R), and where R3 cut the
    // pulse short it stops part-way through the character (Compendium
    // 9.3.4.2 and 14.5.4; the Shaker's "R3 JIT" screens show each case).
    const bool discrete = crtc.type() != CrtcType::AsicPlus && crtc.type() != CrtcType::PreAsic;
    const int lag = !discrete ? 0 : crtc.type() == CrtcType::UM6845R ? 5 : 4;
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
        case Crtc::HsyncCut::AtStart: blackTo = blankedBefore_ ? lag : 0; break;
        case Crtc::HsyncCut::Never: blackTo = 0; break;
        }
    } else if (blankedBefore_) {
        blackTo = lag;
    }
    blankedBefore_ = hsync_ && cut == Crtc::HsyncCut::None;
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
            if (!fetchedDisplay_) {
                for (int i = 0; i < split; ++i)
                    out[i] = rgbBefore_[kBorder];
                for (int i = split; i < Monitor::kCellWidth; ++i)
                    out[i] = rgb_[kBorder];
            } else {
                const uint8_t* left = kPenTable.pens[mode_][fetched_[0]];
                const uint8_t* right = kPenTable.pens[mode_][fetched_[1]];
                for (int i = 0; i < 8; ++i) {
                    out[i] = (i < split ? rgbBefore_ : rgb_)[left[i]];
                    out[i + 8] = (i + 8 < split ? rgbBefore_ : rgb_)[right[i]];
                }
            }
            for (int i = blackFrom; i < blackTo; ++i)
                out[i] = kBlack;
        }
    }
    inkChanged_ = false;

    // Fetch the character the CRTC is pointing at; it is drawn next time.
    // The refresh address and the raster line together address 64K:
    // MA13-12 pick the 16K block, RA2-0 the line within the row.
    fetchedDisplay_ = crtc.displayEnable();
    const uint16_t ma = crtc.ma();
    const unsigned addr = (ma & 0x3000u) << 2 | (crtc.ra() & 7u) << 11 | (ma & 0x3FFu) << 1;
    fetched_[0] = videoRam[addr];
    fetched_[1] = videoRam[addr | 1];

    monitor.advance();
}

}  // namespace tuxape
