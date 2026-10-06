#pragma once

#include <cstdint>
#include <vector>

namespace tuxape {

// Turns the machine's sound into 16-bit stereo samples at the host's rate.
//
// The machine feeds it one value per PSG step (125 000 a second); each
// output sample is the average of the steps that fall within it, which
// keeps fast-changing sounds such as digitised speech clean.
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
        left_ += other;
        right_ += other;
        if (stereo_) {
            left_ += a + b * 0.5f;
            right_ += c + b * 0.5f;
        } else {
            const float mono = (a + b + c) * 0.5f;
            left_ += mono;
            right_ += mono;
        }
        ++steps_;
        phase_ += rate_;
        if (phase_ >= kStepsPerSecond) {
            phase_ -= kStepsPerSecond;
            emitSample();
        }
    }

    // Finished samples, left and right interleaved. The caller takes them
    // and clears the vector.
    std::vector<int16_t>& samples() { return samples_; }

private:
    double rate_ = 0;
    double phase_ = 0;
    bool stereo_ = true;
    int volume_ = 15;
    float left_ = 0;
    float right_ = 0;
    int steps_ = 0;
    std::vector<int16_t> samples_;

    void emitSample();
};

}  // namespace tuxape
