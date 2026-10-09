#pragma once

#include <cstdint>
#include <vector>

namespace tuxape {

// The picture tube. It follows the sync pulses it is given, like a real
// monitor, and exposes the part of the raster that WinAPE shows: 48
// characters by 270 scanlines, i.e. 768 x 270 pixels at mode-2 resolution
// (scanlines are doubled when displayed).
//
// Its lines are timed by an oscillator that the horizontal sync pulses
// steer, as a CTM's is: a line starts two microseconds before the middle of
// the pulse, so that a pulse a microsecond shorter than the usual four puts
// the picture eight pixels to the right (Compendium 14.4); and when the
// pulses move, the line follows over several lines, giving back three
// eighths of the gap on each (the Shaker's "R2 oscillation story" and "R2
// upd during & after HSYNC" on Amspirit-lite: 9, 5, 3, 2, 1 and 10, 6, 3, 2,
// 1 pixels from the mark). Two positions taken in turn settle in between.
class Monitor {
public:
    static constexpr int kCellWidth = 16;  // pixels drawn per microsecond
    static constexpr int kColumns = 48;
    static constexpr int kWidth = kColumns * kCellWidth;
    static constexpr int kHeight = 270;

    Monitor();
    void reset();

    // The 16 pixels under the beam, or nullptr while it is outside the
    // visible area. With the line begun between two microseconds they do
    // not fall on a character's edge; where they straddle the edge of the
    // picture they are drawn aside, and the part that shows is put in place
    // as the beam moves on.
    uint32_t* cell()
    {
        const int row = y_ - firstLine_;
        const int px = ((beam_ + kUnit - 1) >> kUnitShift) - firstPixel_;
        if (px <= -kCellWidth || px >= kWidth || row < 0 || row >= kHeight)
            return nullptr;
        if (px < 0 || px > kWidth - kCellWidth) [[unlikely]]
            return edgeCell(row, px);
        return back_ + row * kWidth + px;
    }

    // One microsecond has passed.
    void advance()
    {
        if (edgeRow_ >= 0) [[unlikely]]
            placeEdge();
        beam_ += kCellWidth << kUnitShift;
        if (beam_ >= limit_)
            lineEvent();
    }

    // Start of the horizontal sync pulse. `late` is for the machines whose
    // ASIC sends it a microsecond later than a Gate Array does (CRTC 3 and
    // 4): the monitor sold with them is set for that.
    void hsync(bool late = false);
    // The pulse has ended, `pixels` after it began (64 for the usual four
    // microseconds).
    void hsyncEnded(int pixels);
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

    // Microseconds since the line began.
    int beamX() const { return beam_ >> (kUnitShift + 4); }
    int beamY() const { return y_; }
    // The line of the picture being drawn: 0 for its first, less above it.
    int rasterLine() const { return y_ - firstLine_; }
    // And the pixel of that line the beam is at: 0 for its first, less
    // before it, kWidth or more after its last.
    int beamColumn() const { return ((beam_ + kUnit - 1) >> kUnitShift) - firstPixel_; }

private:
    // Position of the visible area relative to the sync pulses, chosen so
    // that the firmware's standard screen is centred.
    static constexpr int kFirstColumn = 13;
    static constexpr int kFirstLine = 35;
    // How far the tube lets a line or a frame drift before it retraces on
    // its own, and how early it accepts a sync pulse when it is not locked.
    static constexpr int kMinLine = 56;
    static constexpr int kFreeRunLine = 72;
    static constexpr int kMinFrame = 250;
    static constexpr int kFreeRunFrame = 352;
    // The beam's place along the line is kept in sixteenths of a pixel.
    static constexpr int kUnitShift = 4;
    static constexpr int kUnit = 1 << kUnitShift;
    static constexpr int kLine = (64 * kCellWidth) << kUnitShift;          // 64 microseconds
    static constexpr int kFreeRun = (kFreeRunLine * kCellWidth) << kUnitShift;
    // A pulse that begins within eight microseconds of a line's start, on
    // either side, steers the oscillator; any other is not for it.
    static constexpr int kWindow = (8 * kCellWidth) << kUnitShift;

    std::vector<uint32_t> bufferA_;
    std::vector<uint32_t> bufferB_;
    uint32_t* back_;
    uint32_t* front_;
    int beam_ = 0;           // from the line's start to the start of this microsecond
    int period_ = kFreeRun;  // where the line in progress ends
    int limit_ = kFreeRun;   // ... or, sooner, where its missing pulse is given up
    int y_ = 0;
    int firstPixel_ = kFirstColumn * kCellWidth;
    int firstLine_ = kFirstLine;
    bool locked_ = false;      // the line in progress has had its pulse
    bool own_ = false;         // ... and at its start, not before it
    bool awaited_ = false;     // the line began on the oscillator's word: its pulse may still come
    bool nextLocked_ = false;  // the pulse of the line to come is already here
    bool pulse_ = false;       // a pulse the oscillator listens to is in progress
    int sincePulse_ = 0;       // where the last such pulse began, from this line's start
    int lastInterval_ = 0;     // the time between the last two such pulses, if they were neighbours
    int pace_ = kLine;         // the line's length the oscillator has settled on
    int owed_ = 0;             // what the line to come is to give for a pulse already here
    int lastLack_ = 0;         // by how much the last pulse's middle came early for its length
    // A cell that straddles the picture's edge: the pixel before it, then
    // its sixteen.
    uint32_t edge_[1 + kCellWidth] = {};
    int edgeRow_ = -1;
    int edgePixel_ = 0;
    uint64_t frameNumber_ = 0;

    int correction(int late);
    void lineEvent();
    void countLine();
    uint32_t* edgeCell(int row, int px);
    void placeEdge();
    void newLine();
    void newFrame();
};

}  // namespace tuxape
