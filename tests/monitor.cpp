// The picture tube's line oscillator: where it puts a line when the
// horizontal sync pulses are shorter than usual, move, or go missing.
//
// The lock on the pulse's middle is the Compendium's (14.4: a pulse a
// microsecond shorter moves the picture eight pixels to the right). The pace
// at which the tube follows a pulse that has moved, three eighths of the gap
// a line, and the way it settles between two positions taken in turn, are
// what Amspirit-lite shows on the Shaker's "R2 oscillation story" and "R2 upd
// during & after HSYNC"; a line left without its pulse runs on for 72
// microseconds, and the first pulse to come late enough in a line after that
// is taken as it is.

#include <cstdint>
#include <vector>

#include "check.h"
#include "core/monitor.h"

namespace {

using namespace tuxape;

constexpr uint32_t kMark = 0xFFFFFFFF;
constexpr uint32_t kGround = 0xFF202020;

// A machine reduced to its sync pulses: lines of 64 microseconds, on each of
// which a pulse begins at `at` (-1: none) and lasts `pixels`; the
// microsecond 30 of every line is drawn white, so that where it lands tells
// where the tube began the line.
struct Tube {
    Monitor monitor;
    int microsecond = 0;  // within the machine's line
    int pulseLeft = -1;   // pixels the pulse in progress has still to last
    int secondAt = -1;    // a second pulse on every line, for the cases that want one
    int length = 64;      // of the machine's line, in microseconds

    // One line of the machine. Returns the pixel where the mark of the line
    // drawn meanwhile begins, or -1000 if it is not in the picture.
    int line(int at, int pixels = 4 * Monitor::kCellWidth)
    {
        int mark = -1000;
        for (int n = 0; n < length; ++n) {
            // The microsecond that ends is drawn, then the beam moves on...
            if (uint32_t* out = monitor.cell()) {
                const bool white = microsecond == 30;
                for (int i = 0; i < Monitor::kCellWidth; ++i)
                    out[i] = white ? kMark : kGround;
                if (white)
                    mark = monitor.beamColumn();
            }
            monitor.advance();
            // ... and the new one brings its pulses.
            microsecond = (microsecond + 1) % length;
            if (pulseLeft >= 0) {
                pulseLeft -= Monitor::kCellWidth;
                if (pulseLeft <= 0) {
                    monitor.hsyncEnded(lasting);
                    pulseLeft = -1;
                }
            }
            if (microsecond == at || microsecond == secondAt) {
                monitor.hsync();
                lasting = pixels;
                pulseLeft = pixels;
            }
        }
        return mark;
    }

    // Lines enough for the tube to be on the pulses, and inside the picture.
    void settle(int at, int pixels = 4 * Monitor::kCellWidth)
    {
        for (int n = 0; n < 300; ++n)
            line(at, pixels);
        monitor.vsync();
        for (int n = 0; n < 4; ++n)
            line(at, pixels);
        monitor.vsyncEnded();
        for (int n = 0; n < 56; ++n)
            line(at, pixels);
    }

    int lasting = 0;
};

// Where the mark is with the usual pulse of four microseconds at 48: 46
// microseconds after the pulse begins, 13 of them before the picture.
constexpr int kHome = (64 - 48 + 30 - 13) * Monitor::kCellWidth;

void testLock()
{
    Tube tube;
    tube.settle(48);
    for (int n = 0; n < 20; ++n)
        CHECK_EQ(tube.line(48), kHome);
    // The picture itself has the mark there, and nowhere else on the line.
    const uint32_t* row = tube.monitor.drawing() + (tube.monitor.rasterLine() - 1) * Monitor::kWidth;
    CHECK_EQ(row[kHome], kMark);
    CHECK_EQ(row[kHome + 15], kMark);
    CHECK_EQ(row[kHome - 1], kGround);
    CHECK_EQ(row[kHome + 16], kGround);
}

// A pulse of less than four microseconds has its middle sooner: the line
// starts sooner by half of what the pulse lacks, the picture is as much to
// the right (Compendium 14.4; the Shaker's "CSYNC4 vs 2 x CSYNC2").
void testShortPulses()
{
    for (const int microseconds : {3, 2, 1}) {
        const int right = (4 - microseconds) * 8;
        Tube tube;
        tube.settle(48, microseconds * Monitor::kCellWidth);
        for (int n = 0; n < 8; ++n)
            CHECK_EQ(tube.line(48, microseconds * Monitor::kCellWidth), kHome + right);
        // Back to four microseconds: the line drawn while the first such
        // pulse comes and the one that pulse starts stay where they were;
        // the next has gone half of three eighths of the way, the pulse
        // being weighed with the short one before it; then three eighths
        // of what is left each time, down to nothing.
        CHECK_EQ(tube.line(48), kHome + right);
        CHECK_EQ(tube.line(48), kHome + right);
        int last = tube.line(48);
        CHECK_EQ(last, kHome + right - right * 3 / 16);
        double gap = right - right * 3 / 16.0;
        for (int n = 0; n < 5; ++n) {
            const int mark = tube.line(48);
            gap = gap * 5 / 8;
            CHECK(mark <= last && mark - kHome >= static_cast<int>(gap) && mark - kHome <= static_cast<int>(gap) + 1);
            last = mark;
        }
        for (int n = 0; n < 12; ++n)
            last = tube.line(48);
        CHECK_EQ(last, kHome);
    }
    // A pulse cut a quarter of a microsecond short (R3 just in time): two
    // pixels, which is what the Shaker's one-pixel scroll in mode 1 is
    // made of.
    Tube tube;
    tube.settle(48, 3 * Monitor::kCellWidth + 12);
    CHECK_EQ(tube.line(48, 3 * Monitor::kCellWidth + 12), kHome + 2);
}

// A second pulse on every line. Within six microseconds of the line's own
// it is not heard (the Shaker's "CSYNC4 vs 2 x CSYNC2": two pulses of two
// microseconds leave the picture where the first alone would put it).
// Further along it pulls from afar, by an amount that hangs on its length
// and on the half of the line it is in, not on where exactly: later in the
// first half, sooner and less in the second, nothing where the two meet
// (the Shaker's "2 x CSYNC relative" on Amspirit-lite: 23, 16, 10 and 3
// pixels to the left for pulses of 4, 3, 2 and 1 microseconds, 13, 9, 6
// and 2 to the right).
void testSecondPulse()
{
    {
        Tube tube;
        tube.secondAt = 53;
        tube.settle(48, 2 * Monitor::kCellWidth);
        for (int n = 0; n < 8; ++n)
            CHECK_EQ(tube.line(48, 2 * Monitor::kCellWidth), kHome + 16);
    }
    struct Case {
        int at;            // where the second pulse begins
        int microseconds;  // how long both pulses last
        int left;          // how far left of its place the picture rests
    };
    const Case cases[] = {
        {55, 4, 23}, {60, 4, 23}, {3, 4, 23}, {10, 4, 23},  // 7 to 26 microseconds after the first
        {55, 3, 16}, {55, 2, 10}, {55, 1, 3},
        {13, 4, 10}, {14, 4, 5}, {15, 4, 0},               // 29, 30, 31: the turn
        {13, 3, 7}, {14, 3, 3}, {13, 2, 4}, {14, 2, 2},
        {30, 4, -13}, {41, 4, -13},                        // the second half of the line
        {41, 3, -9}, {41, 2, -6}, {41, 1, -2},
    };
    for (const Case& c : cases) {
        Tube tube;
        const int pixels = c.microseconds * Monitor::kCellWidth;
        tube.settle(48, pixels);
        tube.secondAt = c.at;
        for (int n = 0; n < 40; ++n)
            tube.line(48, pixels);
        const int home = kHome + (4 - c.microseconds) * 8;
        for (int n = 0; n < 4; ++n)
            CHECK_EQ(tube.line(48, pixels), home - c.left);
        // With the second pulse gone the picture comes back.
        tube.secondAt = -1;
        for (int n = 0; n < 30; ++n)
            tube.line(48, pixels);
        CHECK_EQ(tube.line(48, pixels), home);
    }
}

// The pulses come a microsecond later from one line on: the line that pulse
// starts does not move; the next ones go to the new place, half-way at
// first, the pulse being weighed with the one before it, then by three
// eighths of what is left.
void testPulseMoves()
{
    Tube tube;
    tube.settle(48);
    CHECK_EQ(tube.line(49), kHome);  // drawn before the late pulse
    CHECK_EQ(tube.line(49), kHome);  // the line it starts: where the oscillator had it
    int last = tube.line(49);
    CHECK_EQ(last, kHome - 3);       // (16 - 8) * 3 / 8
    double gap = 13;
    for (int n = 0; n < 10; ++n) {
        const int mark = tube.line(49);
        gap = gap * 5 / 8;
        CHECK(mark <= last && mark >= kHome - 16);
        CHECK(kHome - 16 + static_cast<int>(gap) + 1 >= mark && kHome - 16 + static_cast<int>(gap) - 1 <= mark);
        last = mark;
    }
    CHECK_EQ(tube.line(49), kHome - 16);

    // Sooner again: the first pulse to come early cuts short the line in
    // progress, and the line it starts is already on its way.
    const int first = tube.line(48);
    CHECK_EQ(first, kHome - 16);
    const int second = tube.line(48);
    CHECK_EQ(second, kHome - 16 + 3);
    for (int n = 0; n < 14; ++n)
        tube.line(48);
    CHECK_EQ(tube.line(48), kHome);
}

// Two places taken in turn: the tube settles between them, whatever lies
// between (the Compendium's "the CTM tries to synchronise the line between
// these 2 positions").
void testTwoPlaces()
{
    for (const int apart : {1, 2, 3}) {
        Tube tube;
        tube.settle(48);
        for (int n = 0; n < 24; ++n) {
            tube.line(48 + apart);
            tube.line(48);
        }
        for (int n = 0; n < 6; ++n) {
            const int middle = kHome - apart * 8;
            const int a = tube.line(48 + apart);
            const int b = tube.line(48);
            CHECK(a >= middle - 1 && a <= middle + 1);
            CHECK(b >= middle - 1 && b <= middle + 1);
        }
    }
}

// A machine whose lines are not of 64 microseconds: once two pulses in a row
// have come at its pace the oscillator runs at it, and the picture is where
// the pulses put it, not aside by what a loop that only follows would need
// to keep up. (The Plotting cartridge has lines of 65 microseconds.)
void testOtherPace()
{
    for (const int length : {65, 63}) {
        Tube tube;
        tube.length = length;
        tube.settle(48);
        const int home = (length - 48 + 30 - 13) * Monitor::kCellWidth;
        for (int n = 0; n < 8; ++n)
            CHECK_EQ(tube.line(48), home);
        // And a pulse cut short still moves it by half of what it lacks.
        for (int n = 0; n < 30; ++n)
            tube.line(48, 2 * Monitor::kCellWidth);
        CHECK_EQ(tube.line(48, 2 * Monitor::kCellWidth), home + 16);
    }
}

// No pulse: the oscillator keeps its pace and the picture stays where it
// was (the Shaker's "no HSYNC for xx lines"); when the pulses come back
// where they were, nothing has happened.
void testNoPulse()
{
    Tube tube;
    tube.settle(48);
    const int before = tube.monitor.beamY();
    for (int n = 0; n < 40; ++n)
        CHECK_EQ(tube.line(-1), kHome);
    for (int n = 0; n < 5; ++n)
        CHECK_EQ(tube.line(48), kHome);
    CHECK_EQ(tube.monitor.beamY() - before, 45);

    // They come back elsewhere, 20 microseconds later in the line: the
    // tube has to look for them. Its lines last 72 microseconds until a
    // pulse begins 56 microseconds or more into one, which starts a line
    // there and then.
    for (int n = 0; n < 10; ++n)
        tube.line(-1);
    for (int n = 0; n < 12; ++n)
        tube.line(4);
    const int moved = (64 - 4 + 30 - 13) * Monitor::kCellWidth - 64 * Monitor::kCellWidth;
    for (int n = 0; n < 5; ++n)
        CHECK_EQ(tube.line(4), moved);
}

// No pulse while the vertical sync is on: the line runs on to 72
// microseconds, and so do the next ones; the first pulse to begin 56
// microseconds or more into such a line starts a new one there and then
// (the Shaker's "VSYNC Gate Array" J screens).
void testNoPulseInVerticalSync()
{
    Tube tube;
    tube.settle(48);
    CHECK_EQ(tube.line(48), kHome);
    tube.monitor.vsync();
    const int before = tube.monitor.beamY();
    // Five lines of the machine without a pulse: the tube's lines last 72
    // microseconds, and the mark comes 8 microseconds sooner on each.
    CHECK_EQ(tube.line(-1), kHome);
    CHECK_EQ(tube.line(-1), kHome - 8 * Monitor::kCellWidth);
    CHECK_EQ(tube.line(-1), kHome - 16 * Monitor::kCellWidth);
    tube.line(-1);
    tube.line(-1);
    // 336 microseconds since the last pulse: four lines of 72 have ended.
    CHECK_EQ(tube.monitor.beamY() - before, 4);
    // The pulses are back. They begin 24, then 16, 8 and 0 microseconds
    // into the tube's lines, where it does not hear them; the next one
    // comes 64 microseconds into its line and is taken.
    tube.monitor.vsyncEnded();
    for (int n = 0; n < 5; ++n)
        tube.line(48);
    for (int n = 0; n < 5; ++n)
        CHECK_EQ(tube.line(48), kHome);
    // Fifteen lines of the machine in all, for fourteen of the tube's.
    CHECK_EQ(tube.monitor.beamY() - before, 14);
}

}  // namespace

int main()
{
    testLock();
    testShortPulses();
    testSecondPulse();
    testPulseMoves();
    testTwoPlaces();
    testOtherPace();
    testNoPulse();
    testNoPulseInVerticalSync();
    return checkSummary("monitor");
}
