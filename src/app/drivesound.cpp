#include "drivesound.h"

#include <algorithm>
#include <cmath>

void DriveSound::reset()
{
    whirr_ = level_ = 0.0f;
    pending_ = 0;
    untilClick_ = 0;
    clickAge_ = -1;
}

void DriveSound::mix(std::vector<int16_t>& stereo, int rate, bool motor, int steps)
{
    if (rate <= 0)
        return;
    // A head never has more than a disc's width to cross.
    pending_ = std::min(pending_ + std::max(steps, 0), 80);
    if (!motor && level_ < 0.001f && pending_ == 0 && clickAge_ < 0)
        return;
    const auto random = [this] {
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        return static_cast<float>(static_cast<int32_t>(noise_)) / 2147483648.0f;
    };
    const float smooth = 1.0f - std::exp(-2.0f * 3.14159265f * 500.0f / static_cast<float>(rate));
    const float rise = 1.0f / (0.25f * static_cast<float>(rate));  // a quarter of a second up or down
    const int between = rate * 6 / 1000;                           // a step every 6 ms
    const int clickLength = rate * 5 / 1000;
    for (size_t i = 0; i + 1 < stereo.size(); i += 2) {
        level_ = std::clamp(level_ + (motor ? rise : -rise), 0.0f, 1.0f);
        whirr_ += (random() - whirr_) * smooth;
        // The disc turns five times a second, and is heard doing so.
        turn_ += 5.0 / rate;
        if (turn_ >= 1.0)
            turn_ -= 1.0;
        float sound = whirr_ * level_ * (0.75f + 0.25f * static_cast<float>(std::sin(turn_ * 6.283185307))) * 900.0f;
        if (clickAge_ < 0 && pending_ > 0 && --untilClick_ <= 0) {
            --pending_;
            clickAge_ = 0;
            untilClick_ = between;
        }
        if (clickAge_ >= 0) {
            // A knock: a low thud and a rattle that die away at once.
            const float t = static_cast<float>(clickAge_) / static_cast<float>(rate);
            const float fade = std::exp(-t * 900.0f);
            sound += (std::sin(t * 6.2831853f * 1400.0f) * 0.6f + random() * 0.4f) * fade * 3200.0f;
            if (++clickAge_ >= clickLength)
                clickAge_ = -1;
        }
        const int add = static_cast<int>(sound);
        stereo[i] = static_cast<int16_t>(std::clamp(stereo[i] + add, -32768, 32767));
        stereo[i + 1] = static_cast<int16_t>(std::clamp(stereo[i + 1] + add, -32768, 32767));
    }
}
