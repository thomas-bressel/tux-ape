#include "core/monitor.h"

#include <algorithm>

namespace tuxape {

namespace {
constexpr uint32_t kBlack = 0xFF000000;
}

Monitor::Monitor()
    : bufferA_(kWidth * kHeight, kBlack)
    , bufferB_(kWidth * kHeight, kBlack)
    , back_(bufferA_.data())
    , front_(bufferB_.data())
{
}

void Monitor::reset()
{
    std::fill(bufferA_.begin(), bufferA_.end(), kBlack);
    std::fill(bufferB_.begin(), bufferB_.end(), kBlack);
    beam_ = y_ = 0;
    limit_ = period_ = kFreeRun;
    locked_ = nextLocked_ = own_ = awaited_ = pulse_ = false;
    owed_ = lastLack_ = sincePulse_ = lastInterval_ = 0;
    pace_ = kLine;
    edgeRow_ = -1;
}

void Monitor::hsync(bool late)
{
    // The ASICs (CRTC 3 and 4) keep the HSYNC in step with the picture,
    // which a Gate Array draws a character behind it: their pulse reaches
    // the monitor a microsecond later. On a CTM 640 or 644 the picture
    // would sit a character to the left. But Amstrad set the monitors of
    // those machines, the Plus's CM14 and the CTM sold with the last CPCs,
    // so that the frame is centred all the same (Compendium 15.1): the
    // visible area starts a character sooner after the pulse.
    firstPixel_ = (late ? kFirstColumn - 1 : kFirstColumn) * kCellWidth;
    if (beam_ <= kWindow && !own_ && (awaited_ || locked_)) {
        // The line has just begun: the pulse is its own, on time or late.
        // (A second one close behind is not heard: two short pulses a line
        // leave the picture where the first alone would put it, as the
        // Shaker's "CSYNC4 vs 2 x CSYNC2" has it.)
        own_ = true;
        locked_ = true;
        if (awaited_)
            countLine();
        pulse_ = true;
        // The line it has started is the one that gives or takes: the next
        // starts nearer the mark, this one where it did.
        const int give = correction(beam_);
        limit_ = period_ = pace_ + give;
    } else if (locked_ && beam_ >= period_ - kWindow) {
        // Shortly before the line's end: the pulse of the next one, early.
        // The line in progress is cut short for it at once; if it is the
        // other way the pulse pushes, the line it belongs to gives.
        nextLocked_ = true;
        pulse_ = true;
        const int give = correction(beam_ - period_);
        if (give < 0)
            limit_ = period_ += give;
        else
            owed_ = give;
    } else if (!locked_ && !awaited_ && beam_ >= (kMinLine * kCellWidth) << kUnitShift) {
        // Running free, the tube takes the pulse as it comes.
        newLine();
        beam_ = 0;
        own_ = true;
        locked_ = true;
        awaited_ = false;
        limit_ = period_ = pace_;
        pulse_ = true;
        sincePulse_ = 0;
        lastInterval_ = 0;
    }
}

// The beam has reached the end of its line, or the point where a line begun
// without its pulse stops waiting for it.
void Monitor::lineEvent()
{
    if (!awaited_) {
        newLine();
        return;
    }
    // No pulse: the line before is taken to have run on to here, as a tube
    // left to itself lets it, and from here the oscillator runs free.
    countLine();
    beam_ -= kWindow;
    sincePulse_ -= kWindow;
    limit_ = period_ = kFreeRun;
}

// What a pulse that begins `late` after the start of its line (before it,
// if negative) makes the oscillator give: three eighths of the way to a
// point half-way between where this pulse and the one before would put the
// line. A pulse that comes a line's time after the last one is followed
// alone; of two positions taken in turn the tube keeps the middle.
int Monitor::correction(int late)
{
    // The oscillator's own pace is the time from one pulse to the next
    // once two such times in a row are the same: a machine whose lines are
    // not of 64 microseconds is followed without the picture moving aside
    // (a cartridge like Plotting has lines of 65), while pulses that come
    // and go around a steady pace leave it alone.
    const int interval = beam_ - sincePulse_;
    sincePulse_ = beam_;
    int jitter = 0;
    if (interval >= kLine - kWindow && interval <= kLine + kWindow) {
        if (interval == lastInterval_)
            pace_ = interval;
        lastInterval_ = interval;
        // How much later than a line's time after the one before this
        // pulse begins.
        jitter = interval - pace_;
    } else {
        lastInterval_ = 0;
    }
    const int error = late - jitter / 2;
    // (Rounded away from nothing: the last sixteenth of a pixel is given
    // back too, and the line ends up on the mark exactly.)
    return (error * 3 + (error > 0 ? 7 : -7)) / 8;
}

void Monitor::hsyncEnded(int pixels)
{
    if (!pulse_)
        return;
    pulse_ = false;
    // The mark is the pulse's middle, two microseconds after the line's
    // start: a pulse cut short has its middle sooner by half of what it
    // lacks, and the line is drawn back as much, with the same share for
    // the pulse before as for where the pulses begin.
    const int lack = ((4 * kCellWidth - pixels) << kUnitShift) / 2;
    const int error = -(lack + lastLack_) / 2;
    lastLack_ = lack;
    if (error != 0)
        limit_ = period_ += (error * 3 - 7) / 8;
}

void Monitor::vsync()
{
    if (y_ >= kMinFrame)
        newFrame();
}

void Monitor::newLine()
{
    beam_ -= period_;
    sincePulse_ -= period_;
    own_ = false;
    // A line that follows one with its pulse starts on the oscillator's
    // word and waits for its own, which may be late.
    awaited_ = locked_ && !nextLocked_;
    locked_ = nextLocked_;
    nextLocked_ = false;
    period_ = (locked_ ? pace_ : kFreeRun) + owed_;
    limit_ = awaited_ ? kWindow : period_;
    owed_ = 0;
    // (A line that waits for its pulse is counted when the pulse comes, or
    // when it is given up: a vertical sync that falls in between finds the
    // tube on the line before, as one that had run on would be.)
    if (!awaited_ && ++y_ >= kFreeRunFrame)
        newFrame();
}

void Monitor::countLine()
{
    awaited_ = false;
    if (++y_ >= kFreeRunFrame)
        newFrame();
}

uint32_t* Monitor::edgeCell(int row, int px)
{
    edgeRow_ = row;
    edgePixel_ = px;
    // What the picture has before the cell, for a cell that changes it.
    edge_[0] = px > 0 ? back_[row * kWidth + px - 1] : kBlack;
    return edge_ + 1;
}

void Monitor::placeEdge()
{
    uint32_t* line = back_ + edgeRow_ * kWidth;
    for (int i = -1; i < kCellWidth; ++i) {
        const int x = edgePixel_ + i;
        if (x >= 0 && x < kWidth)
            line[x] = edge_[1 + i];
    }
    edgeRow_ = -1;
}

void Monitor::newFrame()
{
    y_ = 0;
    std::swap(back_, front_);
    std::fill(back_, back_ + kWidth * kHeight, kBlack);
    ++frameNumber_;
}

}  // namespace tuxape
