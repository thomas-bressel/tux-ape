#pragma once

#include <cstdint>
#include <vector>

namespace tuxape {

// Turns the machine's sound into 16-bit stereo samples at the host's rate.
//
// The machine feeds it one value per PSG step (125 000 a second). What the
// host's rate cannot carry is filtered out before the samples are taken: a
// tone period of 0 or 1 is a square wave of 62.5 kHz, which nobody hears on
// a CPC and which must not come back as a whistle at 18.4 kHz because the
// sound card works at 44.1 kHz. (A plain average of the steps within each
// sample lets a good part of it through.)
//
// Nothing is produced until a sample rate is set, so a machine nobody
// listens to costs nothing.
class AudioMixer {
public:
    static constexpr int kStepsPerSecond = 125000;

    // 0 turns the output off. Fractions are allowed: the front end nudges
    // the rate to stay in step with the sound card.
    void setSampleRate(double hz);
    bool enabled() const { return rate_ > 0; }

    // Stereo puts channel A on the left, C on the right and B in the
    // middle; mono mixes the three equally.
    void setStereo(bool stereo) { stereo_ = stereo; }
    // 0 (silent) to 15, like WinAPE's volume slider.
    void setVolume(int volume) { volume_ = volume < 0 ? 0 : volume > 15 ? 15 : volume; }

    // One step of sound: the PSG's three channel outputs, each 0.0 to 1.0,
    // and what else is to be heard on both sides (the tape, say).
    void addStep(float a, float b, float c, float other = 0.0f)
    {
        if (rate_ <= 0)
            return;
        if (at_ == kBuffer)
            filter();
        if (stereo_) {
            left_[at_] = other + a + b * 0.5f;
            right_[at_] = other + c + b * 0.5f;
        } else {
            left_[at_] = right_[at_] = other + (a + b + c) * 0.5f;
        }
        ++at_;
    }

    // Finished samples, left and right interleaved. The caller takes them
    // and clears the vector.
    std::vector<int16_t>& samples()
    {
        if (at_ > filtered_)
            filter();
        return samples_;
    }

private:
    // The filter: a windowed sinc, 64 steps long for 44.1 kHz and longer for
    // lower rates, worked out for 32 positions of the sample between two
    // steps (and for the next step, so that every position has a
    // neighbour to be interpolated with).
    //
    // The steps are kept as they come and filtered a few hundred at a time,
    // when the buffer is full or the samples are asked for: the filter's
    // tables then stay in the processor's cache for the whole batch, which
    // one sample at a time, with the rest of the machine at work in
    // between, they do not.
    static constexpr int kLanes = 16;  // the taps come in sixteens
    static constexpr int kMaxTaps = 128;
    static constexpr int kPositions = 32;
    static constexpr int kBuffer = kMaxTaps + 512;

    double rate_ = 0;
    double perRate_ = 0;  // 1 / rate_: a division for every sample would be felt
    double phase_ = 0;
    double designedFor_ = 0;  // the rate the filter was worked out for
    bool stereo_ = true;
    int volume_ = 15;
    int taps_ = 0;
    int at_ = 0;        // where the next step goes
    int filtered_ = 0;  // the steps before this one have had their samples taken
    float left_[kBuffer] = {};
    float right_[kBuffer] = {};
    std::vector<float> kernel_;  // (kPositions + 1) rows of taps_
    std::vector<int16_t> samples_;

    void design();
    void filter();
    void emitSample(int end);
};

}  // namespace tuxape
