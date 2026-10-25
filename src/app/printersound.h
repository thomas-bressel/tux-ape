#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "core/dmp2000.h"

// The DMP2000 heard beside the machine: the buzz of its needles, a click
// for each column struck through the head's ringing; the carriage's
// stepper going along the line, with a knock where it starts and stops;
// the paper's stepper; the beeper. Made here, not taken from a recording,
// from what the printer does and when (core/dmp2000.h).
class PrinterSound {
public:
    // What the printer will do; what has been heard already is let go.
    void add(const std::vector<tuxape::Dmp2000::Event>& events, uint64_t now);
    // Adds the printer's sounds to a stretch of sound, stereo samples at
    // so many a second, the machine's time having come to `now`.
    void mix(std::vector<int16_t>& stereo, int rate, uint64_t now);
    // Silence at once.
    void reset();

private:
    // A ringing part: a two-pole resonator.
    struct Ring {
        float a1 = 0, a2 = 0, gain = 0, y1 = 0, y2 = 0;
        void tune(float hertz, float width, int rate);
        float step(float in)
        {
            const float y = in * gain + a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    struct Strike {
        uint64_t at;
        float force;  // the needles struck, 0 to 1
    };

    std::deque<Strike> strikes_;
    std::deque<tuxape::Dmp2000::Motion> motions_;
    std::deque<uint64_t> beeps_;
    uint64_t heard_ = 0;  // the machine's time at the end of the last stretch
    int rate_ = 0;
    uint32_t noise_ = 0x6C8E9CF5;
    Ring head_, body_, platen_, carriage_, paper_;
    double carriageStep_ = 0.0, paperStep_ = 0.0;  // where each stepper is between two steps
    int burst_ = 0;                                // samples of a strike's rattle still to come
    float burstForce_ = 0.0f;
    uint64_t beepUntil_ = 0;
    double beepTurn_ = 0.0;
    int ringing_ = 0;  // samples the rings may still be heard
    float lowCut_ = 0.0f, lastIn_ = 0.0f, lastOut_ = 0.0f;

    float random();
};
