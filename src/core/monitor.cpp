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
    locked_ = nextLocked_ = awaited_ = pulse_ = stray_ = strayLine_ = false;
    strayMark_ = 0;
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
    if (beam_ <= kWindow && awaited_) {
        // The line has just begun and waits for its pulse: this is it, on
        // time or late.
        locked_ = true;
        countLine();
        pulse_ = true;
        // The line it has started is the one that gives or takes: the next
        // starts nearer the mark, this one where it did.
        const int give = correction(beam_);
        limit_ = period_ = pace_ + give;
    } else if (locked_ && beam_ >= period_ - kEarly) {
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
        locked_ = true;
        awaited_ = false;
        limit_ = period_ = pace_;
        pulse_ = true;
        sincePulse_ = 0;
        lastInterval_ = 0;
    } else if (locked_ && beam_ - sincePulse_ >= kDeaf) {
        // Any other pulse, with the line on its own: it pulls from afar
        // (see strayPulse), by where it is after the line's own.
        stray_ = true;
        strayAt_ = beam_ - sincePulse_;
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
    // (strayMark_: what a pulse elsewhere on the line before has made of
    // the mark, see strayPulse.)
    const int error = late - jitter / 2 + strayMark_;
    // (Rounded away from nothing: the last sixteenth of a pixel is given
    // back too, and the line ends up on the mark exactly.)
    return (error * 3 + (error > 0 ? 7 : -7)) / 8;
}

void Monitor::hsyncEnded(int pixels)
{
    if (stray_) {
        stray_ = false;
        if (locked_ && !pulse_)
            strayPulse(pixels);
        return;
    }
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

// A pulse that is not the line's own, somewhere along it. The tube's
// detector no longer tells how far it is, only on which side: in the first
// half of the line it moves the mark for the next line's pulse later by a
// fixed amount, in the second half sooner by seven thirteenths of that,
// and around the middle the one turns into the other, by way of nothing
// (the short pulses of the Shaker's "R3 JIT", 32 microseconds after the
// line's own, leave the picture alone). The amount grows with the pulse's
// length, and in the six microseconds that follow the line's own pulse the
// tube hears nothing. Figures from Amspirit-lite on the
// Shaker's "2 x CSYNC relative" (a second pulse of 4, 3, 2 or 1
// microseconds 7 to 19 microseconds after the first: the picture 23, 16, 10
// and 3 pixels to the left; 29 and 30 after: 10 and 5 for the longest; 31:
// none; 7 to 11 before the next: 13, 9, 6 and 2 to the right) and on "R2
// upd during & after HSYNC" (10 to the left).
void Monitor::strayPulse(int pixels)
{
    const int length = pixels << kUnitShift;
    const int late = length > kStrayLost ? (length - kStrayLost) * 5 / 12 : 0;
    const int early = late * 7 / 13;
    if (strayAt_ <= kStrayLate)
        strayMark_ = late;
    else if (strayAt_ < kStrayTurn)
        strayMark_ = late * (kStrayTurn - strayAt_) / (kStrayTurn - kStrayLate);
    else if (strayAt_ < kStrayBack)
        strayMark_ = 0;
    else if (strayAt_ < kStrayEarly)
        strayMark_ = -early * (strayAt_ - kStrayBack) / (kStrayEarly - kStrayBack);
    else
        strayMark_ = -early;
    strayLine_ = true;
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
    if (!strayLine_)
        strayMark_ = 0;
    strayLine_ = false;
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
