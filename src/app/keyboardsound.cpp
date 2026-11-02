#include "keyboardsound.h"

#include <algorithm>
#include <cmath>
#include <span>

#include "core/keyboard.h"

namespace {

constexpr float kPi = 3.14159265f;
// Older than this, a stroke is not heard any more: the machine was held.
constexpr uint64_t kStale = 200000;

// Where each key is across a 6128's keyboard, in widths of a key from its
// left edge (the keypad's right edge is at 18.3); the joystick's lines are
// not keys.
constexpr float kAcross[tuxape::kCpcKeyCount] = {
    16.8f, 17.8f, 16.8f, 17.8f, 17.8f, 17.8f, 13.5f, 17.8f,  // cursors, f9 f6 f3, ENTER, f.
    15.8f, 3.1f,  15.8f, 16.8f, 16.8f, 15.8f, 16.8f, 15.8f,  // left, COPY, f7 f8 f5 f1 f2 f0
    13.5f, 13.0f, 14.4f, 13.3f, 15.8f, 1.1f,  12.8f, 1.1f,   // CLR [ RETURN ] f4 SHIFT \ CONTROL
    12.5f, 11.5f, 12.0f, 11.0f, 12.3f, 11.3f, 11.8f, 10.8f,  // ^ - @ P ; : / .
    10.5f, 9.5f,  10.0f, 9.0f,  10.3f, 9.3f,  8.8f,  9.8f,   // 0 9 O I L K M ,
    8.5f,  7.5f,  8.0f,  7.0f,  7.3f,  8.3f,  7.8f,  8.0f,   // 8 7 U Y H J N SPACE
    6.5f,  5.5f,  5.0f,  6.0f,  6.3f,  5.3f,  6.8f,  5.8f,   // 6 5 R T G F B V
    4.5f,  3.5f,  4.0f,  3.0f,  3.3f,  4.3f,  4.8f,  3.8f,   // 4 3 E W S D C X
    1.5f,  2.5f,  0.5f,  2.0f,  0.8f,  2.3f,  0.9f,  2.8f,   // 1 2 ESC Q TAB A CAPS Z
    9.2f,  9.2f,  9.2f,  9.2f,  9.2f,  9.2f,  9.2f,  14.5f,  // joystick, DEL
};

}  // namespace

float KeyboardSound::random()
{
    noise_ ^= noise_ << 13;
    noise_ ^= noise_ >> 17;
    noise_ ^= noise_ << 5;
    return static_cast<float>(static_cast<int32_t>(noise_)) / 2147483648.0f;
}

float KeyboardSound::Comb::step(float in)
{
    const float out = line[at];
    kept = out * (1.0f - damp) + kept * damp;
    line[at] = in + kept * feedback;
    if (++at == line.size())
        at = 0;
    return out;
}

float KeyboardSound::Allpass::step(float in)
{
    const float delayed = line[at];
    const float out = delayed - in;
    line[at] = in + delayed * 0.5f;
    if (++at == line.size())
        at = 0;
    return out;
}

void KeyboardSound::reset()
{
    strokes_.clear();
    voices_.clear();
    rate_ = 0;  // the room made again, and still
    ringing_ = 0;
}

void KeyboardSound::add(uint8_t key, bool down, uint64_t at)
{
    // Not the joystick's lines.
    const auto cpcKey = static_cast<tuxape::CpcKey>(key);
    if (key >= tuxape::kCpcKeyCount || (cpcKey >= tuxape::CpcKey::JoyUp && cpcKey <= tuxape::CpcKey::JoyFire3))
        return;
    // Without a sound card nothing is mixed: what is too old to be heard
    // goes here too.
    while (!strokes_.empty() && strokes_.front().at + kStale < at)
        strokes_.pop_front();
    strokes_.push_back({at, key, down});
}

// The takes of each kind of stroke, and the room, for that many samples a
// second.
void KeyboardSound::prepare(int rate)
{
    rate_ = rate;
    const float fs = static_cast<float>(rate);
    // A part of the key or of the case, struck: it rings at its pitch for
    // a while, from `start` seconds into the stroke.
    const auto ring = [&](std::vector<float>& out, float start, float hertz, float lasts, float loud) {
        const float phase = random() * kPi;
        for (size_t i = static_cast<size_t>(start * fs); i < out.size(); ++i) {
            const float t = static_cast<float>(i) / fs - start;
            out[i] += loud * std::exp(-t / lasts) * std::sin(2.0f * kPi * hertz * t + phase);
        }
    };
    // A click: noise that dies at once, its lows taken off.
    const auto click = [&](std::vector<float>& out, float start, float lasts, float loud) {
        float last = 0.0f;
        for (size_t i = static_cast<size_t>(start * fs); i < out.size(); ++i) {
            const float t = static_cast<float>(i) / fs - start;
            const float n = random();
            out[i] += loud * std::exp(-t / lasts) * (n - last);
            last = n;
        }
    };
    struct Mode {
        float hertz, lasts, loud;
    };
    // The case and the plunger's ringing, struck at the bottom of the
    // travel; the cap's, higher, at the top.
    static constexpr Mode kBottom[] = {{185, 0.022f, 0.35f}, {510, 0.013f, 0.5f}, {1320, 0.006f, 0.55f},
                                       {2550, 0.0032f, 0.45f}, {4100, 0.0018f, 0.3f}};
    static constexpr Mode kTop[] = {{720, 0.006f, 0.25f}, {1900, 0.0035f, 0.6f}, {3300, 0.002f, 0.5f}, {5200, 0.0012f, 0.3f}};
    for (int kind = 0; kind < kKinds; ++kind)
        for (int down = 0; down < 2; ++down)
            for (int take = 0; take < kTakes; ++take) {
                // Lower for a wide key, lower still for the bar.
                const float pitch = (kind == Space ? 0.72f : kind == Wide ? 0.86f : 1.0f) * (1.0f + 0.05f * random());
                std::vector<float> sound(static_cast<size_t>((kind == Space ? 0.10f : 0.07f) * fs));
                float hit = 0.0f;
                if (down) {
                    // The finger on the cap, then the bottom of the travel
                    // some milliseconds later.
                    click(sound, 0.0f, 0.0006f, 0.22f);
                    hit = 0.004f + 0.0015f * static_cast<float>(take) + 0.001f * random();
                }
                click(sound, hit, down ? 0.0009f : 0.0006f, down ? 1.0f : 0.8f);
                for (const Mode& mode : down ? std::span<const Mode>(kBottom) : std::span<const Mode>(kTop))
                    ring(sound, hit, mode.hertz * pitch * (1.0f + 0.04f * random()), mode.lasts * (kind == Space ? 1.5f : 1.0f),
                         mode.loud * (1.0f + 0.15f * random()));
                // The bar's wire, rattling in its clips a few times.
                if (kind == Space)
                    for (int bounce = 1; bounce <= 3; ++bounce)
                        click(sound, hit + 0.0035f * static_cast<float>(bounce), 0.0005f, 0.5f / static_cast<float>(bounce));
                else if (kind == Wide)
                    click(sound, hit + 0.003f, 0.0004f, 0.25f);
                // As loud as its kind is, whatever the take.
                float peak = 0.0f;
                for (const float s : sound)
                    peak = std::max(peak, std::fabs(s));
                const float loudness = (down ? 1.0f : 0.55f) * (kind == Space ? 1.25f : kind == Wide ? 1.1f : 1.0f);
                for (float& s : sound)
                    s *= loudness / std::max(peak, 1e-6f);
                takes_[static_cast<size_t>(kind)][static_cast<size_t>(down)][static_cast<size_t>(take)] = std::move(sound);
            }

    // The room: a small office, its walls a couple of metres away, a desk,
    // a few shelves. A tail of some 0.4 s, its highs taken by what is soft.
    early_.assign(static_cast<size_t>(0.03f * fs) + 1, 0.0f);
    earlyAt_ = 0;
    constexpr float kTail = 0.4f;  // seconds to fall by 60 dB
    static constexpr int kCombs[] = {557, 617, 691, 751, 809, 877};  // samples at 44.1 kHz
    static constexpr int kAllpasses[] = {225, 341, 441};
    for (int side = 0; side < 2; ++side) {
        combs_[static_cast<size_t>(side)].clear();
        allpasses_[static_cast<size_t>(side)].clear();
        // The two sides a little apart, as two ears are.
        const int spread = side ? 23 : 0;
        for (const int length : kCombs) {
            Comb comb;
            comb.line.assign(static_cast<size_t>(static_cast<float>(length + spread) * fs / 44100.0f), 0.0f);
            const float seconds = static_cast<float>(comb.line.size()) / fs;
            comb.feedback = std::pow(10.0f, -3.0f * seconds / kTail);
            comb.damp = 0.35f;
            combs_[static_cast<size_t>(side)].push_back(std::move(comb));
        }
        for (const int length : kAllpasses) {
            Allpass pass;
            pass.line.assign(static_cast<size_t>(static_cast<float>(length + spread) * fs / 44100.0f), 0.0f);
            allpasses_[static_cast<size_t>(side)].push_back(std::move(pass));
        }
    }
}

void KeyboardSound::mix(std::vector<int16_t>& stereo, int rate, uint64_t now)
{
    const size_t frames = stereo.size() / 2;
    const uint64_t from = std::max(heard_, now > kStale ? now - kStale : 0);
    heard_ = now;
    while (!strokes_.empty() && strokes_.front().at + kStale < now)
        strokes_.pop_front();
    if (rate <= 0 || frames == 0 || now <= from)
        return;
    if (strokes_.empty() && voices_.empty() && ringing_ <= 0)
        return;
    if (rate != rate_)
        prepare(rate);
    const float fs = static_cast<float>(rate);
    // The reflections: when, how strong, on which side (seconds, gain,
    // left share).
    static constexpr struct {
        float after, gain, left;
    } kReflections[] = {{0.0031f, 0.42f, 0.8f}, {0.0047f, 0.36f, 0.2f}, {0.0072f, 0.28f, 0.65f},
                        {0.0098f, 0.24f, 0.35f}, {0.0135f, 0.18f, 0.55f}, {0.0181f, 0.14f, 0.45f}};
    constexpr float kDry = 1.0f, kEarly = 0.55f, kTailIn = 0.045f, kTailOut = 1.0f, kLoud = 7000.0f;
    const double step = static_cast<double>(now - from) / static_cast<double>(frames);
    double t = static_cast<double>(from);
    for (size_t i = 0; i < frames; ++i) {
        t += step;
        while (!strokes_.empty() && static_cast<double>(strokes_.front().at) <= t) {
            const Stroke stroke = strokes_.front();
            strokes_.pop_front();
            const auto key = static_cast<tuxape::CpcKey>(stroke.key);
            const Kind kind = key == tuxape::CpcKey::Space ? Space
                              : key == tuxape::CpcKey::Shift || key == tuxape::CpcKey::Return || key == tuxape::CpcKey::Enter
                                      || key == tuxape::CpcKey::Control || key == tuxape::CpcKey::CapsLock
                                      || key == tuxape::CpcKey::Tab || key == tuxape::CpcKey::Copy
                                  ? Wide
                                  : Letter;
            const int take = static_cast<int>((random() * 0.5f + 0.5f) * kTakes) % kTakes;
            // Struck a little harder or softer; heard from where it is,
            // the listener before the middle of the keyboard.
            const float force = 0.85f + 0.15f * random();
            const float pan = std::clamp((kAcross[stroke.key] - 9.15f) / 9.15f * 0.35f, -0.35f, 0.35f);
            voices_.push_back({&takes_[static_cast<size_t>(kind)][stroke.down ? 1u : 0u][static_cast<size_t>(take)], 0,
                               force * std::sqrt(0.5f - pan), force * std::sqrt(0.5f + pan)});
        }
        float left = 0.0f, right = 0.0f, mono = 0.0f;
        for (auto voice = voices_.begin(); voice != voices_.end();) {
            const float s = (*voice->sound)[voice->at];
            left += s * voice->left;
            right += s * voice->right;
            mono += s * (voice->left + voice->right) * 0.5f;
            if (++voice->at >= voice->sound->size())
                voice = voices_.erase(voice);
            else
                ++voice;
        }
        if (mono != 0.0f)
            ringing_ = static_cast<int>(0.8f * fs);
        if (ringing_ <= 0)
            continue;
        --ringing_;
        // The walls and the desk: a few echoes, then the tail.
        early_[earlyAt_] = mono;
        float earlyLeft = 0.0f, earlyRight = 0.0f;
        for (const auto& reflection : kReflections) {
            const size_t back = static_cast<size_t>(reflection.after * fs);
            const float s = early_[(earlyAt_ + early_.size() - back) % early_.size()] * reflection.gain;
            earlyLeft += s * reflection.left;
            earlyRight += s * (1.0f - reflection.left);
        }
        if (++earlyAt_ == early_.size())
            earlyAt_ = 0;
        float tail[2] = {};
        const float into = (mono + (earlyLeft + earlyRight) * 0.5f) * kTailIn;
        for (int side = 0; side < 2; ++side) {
            float sum = 0.0f;
            for (Comb& comb : combs_[static_cast<size_t>(side)])
                sum += comb.step(into);
            for (Allpass& pass : allpasses_[static_cast<size_t>(side)])
                sum = pass.step(sum);
            tail[side] = sum;
        }
        const int l = static_cast<int>((left * kDry + earlyLeft * kEarly + tail[0] * kTailOut) * kLoud);
        const int r = static_cast<int>((right * kDry + earlyRight * kEarly + tail[1] * kTailOut) * kLoud);
        stereo[2 * i] = static_cast<int16_t>(std::clamp(stereo[2 * i] + l, -32768, 32767));
        stereo[2 * i + 1] = static_cast<int16_t>(std::clamp(stereo[2 * i + 1] + r, -32768, 32767));
    }
}
