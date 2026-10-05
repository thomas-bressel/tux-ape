#include "core/gate_array.h"

#include <array>

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
    r52_ = 0;
    vsyncDelay_ = 0;
    hsyncAge_ = 0;
    interrupt_ = false;
    prevHsync_ = prevVsync_ = false;
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

void GateArray::tick(const Crtc& crtc, const uint8_t* videoRam, Monitor& monitor)
{
    const bool hsync = crtc.hsync();
    const bool vsync = crtc.vsync();

    if (hsync) {
        // The monitor gets its sync pulse two characters after the CRTC
        // raises HSYNC, so shorter pulses never reach it.
        if (hsyncAge_ == 2)
            monitor.hsync();
        if (hsyncAge_ < 0xFF)
            ++hsyncAge_;
    } else if (prevHsync_) {
        hsyncAge_ = 0;
        // End of HSYNC: the requested mode takes effect and the interrupt
        // counter advances.
        mode_ = rmr_ & 3;
        if (++r52_ == 52) {
            r52_ = 0;
            interrupt_ = true;
        }
        // Two HSYNCs into the VSYNC the counter is resynchronised with the
        // frame; an interrupt fires if the previous one is far enough away.
        if (vsyncDelay_ && --vsyncDelay_ == 0) {
            if (r52_ >= 32)
                interrupt_ = true;
            r52_ = 0;
        }
    }
    if (vsync && !prevVsync_) {
        vsyncDelay_ = 2;
        monitor.vsync();
    }
    prevHsync_ = hsync;
    prevVsync_ = vsync;

    if (uint32_t* out = monitor.cell()) {
        if (hsync || vsync) {
            for (int i = 0; i < Monitor::kCellWidth; ++i)
                out[i] = kBlack;
        } else if (!crtc.displayEnable()) {
            const uint32_t border = rgb_[kBorder];
            for (int i = 0; i < Monitor::kCellWidth; ++i)
                out[i] = border;
        } else {
            // The refresh address and the raster line together address 64K:
            // MA13-12 pick the 16K block, RA2-0 the line within the row.
            const uint16_t ma = crtc.ma();
            const unsigned addr = (ma & 0x3000u) << 2 | (crtc.ra() & 7u) << 11 | (ma & 0x3FFu) << 1;
            const uint8_t* left = kPenTable.pens[mode_][videoRam[addr]];
            const uint8_t* right = kPenTable.pens[mode_][videoRam[addr | 1]];
            for (int i = 0; i < 8; ++i) {
                out[i] = rgb_[left[i]];
                out[i + 8] = rgb_[right[i]];
            }
        }
    }
    monitor.advance();
}

}  // namespace tuxape
