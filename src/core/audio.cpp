#include "core/audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tuxape {

namespace {

// A side can reach 1.5 (one channel plus half of the shared one).
constexpr float kFullScale = 32767.0f / 1.5f;

// Guards against a front end that stops collecting samples.
constexpr size_t kMaxBufferedSamples = 2 * 48000 * 2;

// The filter's stopband: 60 dB down, from a little above half the host's
// rate (24 kHz for 44.1 kHz: what is left between the two comes back above
// 20 kHz, and weak).
constexpr double kAttenuation = 60.0;
constexpr double kStopEdge = 0.545;
constexpr double kPi = 3.14159265358979323846;

int16_t rounded(float value)
{
    const float limited = std::clamp(value, -32768.0f, 32767.0f);
    return static_cast<int16_t>(limited + (limited < 0 ? -0.5f : 0.5f));
}

// The modified Bessel function of order 0, for the Kaiser window.
double bessel0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 40; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-12)
            break;
    }
    return sum;
}

}  // namespace

void AudioMixer::setSampleRate(double hz)
{
    // What has been heard so far was heard at the rate there was.
    if (rate_ > 0 && at_ > filtered_)
        filter();
    rate_ = hz > 0 ? std::min(hz, static_cast<double>(kStepsPerSecond)) : 0;
    if (rate_ <= 0) {
        samples_.clear();
        phase_ = 0;
        designedFor_ = 0;
        return;
    }
    perRate_ = 1.0 / rate_;
    // The front end's nudges, a fraction of a per cent, leave the filter as
    // it is.
    if (std::abs(rate_ - designedFor_) > designedFor_ * 0.02)
        design();
}

void AudioMixer::design()
{
    designedFor_ = rate_;
    taps_ = std::clamp(kLanes * static_cast<int>(std::lround(4.0 * 44100.0 / rate_)), kLanes, kMaxTaps);

    // Kaiser's rules give the window's shape for the attenuation wanted and
    // the width of the slope that this many taps can make; the cut-off sits
    // in the middle of the slope.
    const double beta = 0.1102 * (kAttenuation - 8.7);
    const double slope = (kAttenuation - 7.95) / (14.36 * (taps_ - 1));  // in cycles per step
    const double stop = std::min(kStopEdge * rate_ / kStepsPerSecond, 0.5);
    const double cutoff = std::max(stop - slope / 2, 0.02);
    const double half = taps_ / 2.0;
    const double window0 = bessel0(beta);

    kernel_.assign(static_cast<size_t>(kPositions + 1) * static_cast<size_t>(taps_), 0.0f);
    for (int position = 0; position <= kPositions; ++position) {
        // How far the sample's moment lies before a step. (The filter's
        // middle is taps_ / 2 - 1 steps before the newest one.)
        const double behind = static_cast<double>(position) / kPositions;
        double row[kMaxTaps];
        double sum = 0;
        for (int tap = 0; tap < taps_; ++tap) {
            const double t = tap - half + behind;
            double value = 0;
            if (std::abs(t) < half) {
                const double x = 2 * cutoff * t * kPi;
                const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(x) / x;
                value = sinc * bessel0(beta * std::sqrt(1 - (t / half) * (t / half))) / window0;
            }
            row[tap] = value;
            sum += value;
        }
        // Every row lets a steady level through unchanged: a level that
        // does not move must not be heard.
        float* out = &kernel_[static_cast<size_t>(position) * static_cast<size_t>(taps_)];
        for (int tap = 0; tap < taps_; ++tap)
            out[tap] = static_cast<float>(row[tap] / sum);
    }

    // Silence before the first step.
    std::memset(left_, 0, sizeof left_);
    std::memset(right_, 0, sizeof right_);
    at_ = filtered_ = taps_ - 1;
    phase_ = 0;
}

// Takes the samples that fall among the steps not yet gone through, then
// moves the steps the filter still needs back to the start of the buffer.
void AudioMixer::filter()
{
    // Each step adds the host's rate to the phase; a sample is due when
    // that makes a full second's worth of steps.
    int step = filtered_;
    for (;;) {
        int due = static_cast<int>((kStepsPerSecond - phase_) * perRate_);
        while (phase_ + due * rate_ < kStepsPerSecond)
            ++due;
        if (step + due > at_) {
            phase_ += (at_ - step) * rate_;
            break;
        }
        step += due;
        phase_ += due * rate_ - kStepsPerSecond;
        emitSample(step);
    }
    const size_t keep = static_cast<size_t>(taps_ - 1);
    std::memmove(left_, left_ + at_ - keep, keep * sizeof(float));
    std::memmove(right_, right_ + at_ - keep, keep * sizeof(float));
    at_ = filtered_ = taps_ - 1;
}

// One sample, from the taps_ steps that end before the one given.
void AudioMixer::emitSample(int end)
{
    // What is left in the phase says how far the sample's moment lies
    // before the last of those steps.
    const float behind = std::max(static_cast<float>(phase_ * perRate_), 0.0f) * kPositions;
    const int position = std::min(static_cast<int>(behind), kPositions - 1);
    const float between = std::min(behind - static_cast<float>(position), 1.0f);
    const float* near = &kernel_[static_cast<size_t>(position) * static_cast<size_t>(taps_)];
    const float* far = near + taps_;
    const float* left = left_ + end - taps_;
    const float* right = right_ + end - taps_;

    // Sixteen sums side by side for each ear: the compiler makes vectors of
    // them, and none has to wait for the one before.
    float sumLeft[kLanes] = {};
    float sumRight[kLanes] = {};
    for (int tap = 0; tap < taps_; tap += kLanes)
        for (int lane = 0; lane < kLanes; ++lane) {
            const float weight = near[tap + lane] + between * (far[tap + lane] - near[tap + lane]);
            sumLeft[lane] += left[tap + lane] * weight;
            sumRight[lane] += right[tap + lane] * weight;
        }
    // ...and they are added up two by two, for the same reason.
    for (int half = kLanes / 2; half > 0; half /= 2)
        for (int lane = 0; lane < half; ++lane) {
            sumLeft[lane] += sumLeft[lane + half];
            sumRight[lane] += sumRight[lane + half];
        }

    if (samples_.size() < kMaxBufferedSamples) {
        const float scale = kFullScale * static_cast<float>(volume_) / 15.0f;
        samples_.push_back(rounded(sumLeft[0] * scale));
        samples_.push_back(rounded(sumRight[0] * scale));
    }
}

}  // namespace tuxape
