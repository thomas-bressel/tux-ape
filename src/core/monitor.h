#pragma once

#include <cstdint>
#include <vector>

namespace tuxape {

// The picture tube. It follows the sync pulses it is given, like a real
// monitor, and exposes the part of the raster that WinAPE shows: 48
// characters by 270 scanlines, i.e. 768 x 270 pixels at mode-2 resolution
// (scanlines are doubled when displayed).
class Monitor {
public:
    static constexpr int kCellWidth = 16;  // pixels drawn per microsecond
    static constexpr int kColumns = 48;
    static constexpr int kWidth = kColumns * kCellWidth;
    static constexpr int kHeight = 270;

    Monitor();
    void reset();

    // The 16 pixels under the beam, or nullptr while it is outside the
    // visible area.
    uint32_t* cell()
    {
        const int col = x_ - firstColumn_;
        const int row = y_ - firstLine_;
        if (col < 0 || col >= kColumns || row < 0 || row >= kHeight)
            return nullptr;
        return back_ + row * kWidth + col * kCellWidth;
    }

    // One microsecond has passed.
    void advance()
    {
        if (++x_ >= kFreeRunLine)
            newLine();
    }

    // Start of the horizontal sync pulse. `late` is for the machines whose
    // ASIC sends it a microsecond later than a Gate Array does (CRTC 3 and
    // 4): the monitor sold with them is set for that.
    void hsync(bool late = false);
    void vsync();  // start of the vertical sync pulse

    // Last complete picture, kWidth x kHeight, 0xAARRGGBB.
    const uint32_t* frame() const { return front_; }
    // The picture being drawn: its lines above the beam are this frame's,
    // those under it an older frame's.
    const uint32_t* drawing() const { return back_; }
    // Number of pictures completed so far.
    uint64_t frameNumber() const { return frameNumber_; }

    // Shifts the picture up or down, as the V Hold knob does.
    void setVerticalHold(int lines) { firstLine_ = kFirstLine + lines; }

    int beamX() const { return x_; }
    int beamY() const { return y_; }
    // The line of the picture being drawn: 0 for its first, less above it.
    int rasterLine() const { return y_ - firstLine_; }
    // And the pixel of that line the beam is at: 0 for its first, less
    // before it, kWidth or more after its last.
    int beamColumn() const { return (x_ - firstColumn_) * kCellWidth; }

private:
    // Position of the visible area relative to the sync pulses, chosen so
    // that the firmware's standard screen is centred.
    static constexpr int kFirstColumn = 13;
    static constexpr int kFirstLine = 35;
    // How far the tube lets a line or a frame drift before it retraces on
    // its own, and how early it accepts a sync pulse.
    static constexpr int kMinLine = 56;
    static constexpr int kFreeRunLine = 72;
    static constexpr int kMinFrame = 250;
    static constexpr int kFreeRunFrame = 352;

    std::vector<uint32_t> bufferA_;
    std::vector<uint32_t> bufferB_;
    uint32_t* back_;
    uint32_t* front_;
    int x_ = 0;
    int y_ = 0;
    int firstColumn_ = kFirstColumn;
    int firstLine_ = kFirstLine;
    uint64_t frameNumber_ = 0;

    void newLine();
    void newFrame();
};

}  // namespace tuxape
