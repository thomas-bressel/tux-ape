#include "core/audio.h"

#include <algorithm>
#include <cmath>

namespace tuxape {

namespace {

// A side can reach 1.5 (one channel plus half of the shared one).
constexpr float kFullScale = 32767.0f / 1.5f;

// Guards against a front end that stops collecting samples.
constexpr size_t kMaxBufferedSamples = 2 * 48000 * 2;

}  // namespace

void AudioMixer::setSampleRate(double hz)
{
    rate_ = hz > 0 ? hz : 0;
    if (rate_ <= 0) {
        samples_.clear();
        left_ = right_ = 0;
        steps_ = 0;
        phase_ = 0;
    }
}

void AudioMixer::emitSample()
{
    const float scale = kFullScale * static_cast<float>(volume_) / 15.0f / static_cast<float>(steps_);
    if (samples_.size() < kMaxBufferedSamples) {
        samples_.push_back(static_cast<int16_t>(std::min(std::lround(left_ * scale), 32767L)));
        samples_.push_back(static_cast<int16_t>(std::min(std::lround(right_ * scale), 32767L)));
    }
    left_ = right_ = 0;
    steps_ = 0;
}

}  // namespace tuxape
