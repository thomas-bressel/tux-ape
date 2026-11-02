// The CPC's keyboard heard: the 464's recorded strokes found, a key going
// down louder than coming up, heard from where it is, the room's tail dying
// away; the space bar duller than a letter; the joystick and strokes too
// old for the stretch not heard.
//
//   keyboard_sound [file.wav]   also writes a line typed, to be listened to

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "core/keyboard.h"
#include "keyboardsound.h"

namespace {

constexpr int kRate = 44100;
constexpr uint64_t kStretch = 10000;  // microseconds a stretch

// The keyboard's sound from 0 to `seconds`, the strokes given first.
std::vector<int16_t> play(const std::vector<std::tuple<tuxape::CpcKey, bool, uint64_t>>& strokes, double seconds)
{
    KeyboardSound sound;
    for (const auto& [key, down, at] : strokes)
        sound.add(static_cast<uint8_t>(key), down, at);
    std::vector<int16_t> all;
    for (uint64_t now = kStretch; now <= static_cast<uint64_t>(seconds * 1e6); now += kStretch) {
        std::vector<int16_t> stretch(static_cast<size_t>(kRate * kStretch / 1000000) * 2, 0);
        sound.mix(stretch, kRate, now);
        all.insert(all.end(), stretch.begin(), stretch.end());
    }
    return all;
}

// Of one side (0 left, 1 right), from and to so many milliseconds.
double peak(const std::vector<int16_t>& s, int side, int from, int to)
{
    double most = 0;
    for (size_t i = static_cast<size_t>(from) * kRate / 1000; i < static_cast<size_t>(to) * kRate / 1000 && 2 * i + 1 < s.size(); ++i)
        most = std::max(most, std::fabs(static_cast<double>(s[2 * i + side])));
    return most;
}

double power(const std::vector<int16_t>& s, int from, int to)
{
    double sum = 0;
    size_t n = 0;
    for (size_t i = static_cast<size_t>(from) * kRate / 1000; i < static_cast<size_t>(to) * kRate / 1000 && 2 * i + 1 < s.size(); ++i, ++n)
        sum += static_cast<double>(s[2 * i]) * s[2 * i] + static_cast<double>(s[2 * i + 1]) * s[2 * i + 1];
    return n ? std::sqrt(sum / static_cast<double>(n) / 2) : 0;
}

// How often the sound crosses zero from so many milliseconds: high for a
// bright sound, low for a dull one.
double crossings(const std::vector<int16_t>& s, int from, int to)
{
    int n = 0;
    for (size_t i = static_cast<size_t>(from) * kRate / 1000 + 1; i < static_cast<size_t>(to) * kRate / 1000; ++i)
        n += (s[2 * i] >= 0) != (s[2 * i - 2] >= 0);
    return n;
}

void writeWav(const std::string& name, const std::vector<int16_t>& s)
{
    FILE* f = std::fopen(name.c_str(), "wb");
    if (!f)
        return;
    const uint32_t bytes = static_cast<uint32_t>(s.size() * 2), rate = kRate, byteRate = kRate * 4, riff = 36 + bytes, fmt = 16;
    const uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&channels, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&bytes, 4, 1, f);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv)
{
    using tuxape::CpcKey;
    // A, at the left: down at 20 ms, up at 120 ms.
    const auto a = play({{CpcKey::A, true, 20000}, {CpcKey::A, false, 120000}}, 1.2);
    const double down = peak(a, 0, 20, 60), up = peak(a, 0, 120, 160);
    std::printf("A: down %.0f, up %.0f; left %.0f right %.0f\n", down, up, peak(a, 0, 20, 60), peak(a, 1, 20, 60));
    CHECK(down > 2000 && down < 20000);
    // On the 464 recorded, a key coming up is some four times softer.
    CHECK(up > 0.05 * down && up < 0.6 * down);
    CHECK(peak(a, 0, 20, 60) > peak(a, 1, 20, 60));
    CHECK_EQ(peak(a, 0, 0, 19), 0);
    // The room: still heard a quarter of a second later, quiet, then gone.
    const double tail = power(a, 300, 400), body = power(a, 120, 140);
    std::printf("room: %.1f at 300-400 ms against %.1f as it came up\n", tail, body);
    CHECK(tail > 0 && tail < body / 8);
    CHECK_EQ(peak(a, 0, 1050, 1200) + peak(a, 1, 1050, 1200), 0);
    // ENTER, at the right, on the right.
    const auto enter = play({{CpcKey::Return, true, 20000}}, 0.2);
    CHECK(peak(enter, 1, 20, 60) > peak(enter, 0, 20, 60));
    // The space bar, duller than a letter.
    const auto space = play({{CpcKey::Space, true, 20000}}, 0.2);
    const auto k = play({{CpcKey::K, true, 20000}}, 0.2);
    std::printf("crossings from 30 to 70 ms: space %.0f, K %.0f\n", crossings(space, 30, 70), crossings(k, 30, 70));
    CHECK(crossings(space, 30, 70) < crossings(k, 30, 70));
    // The joystick is not the keyboard; a stroke the machine was held over
    // too long is let go.
    CHECK_EQ(power(play({{CpcKey::JoyFire1, true, 20000}}, 0.2), 0, 200), 0);
    {
        KeyboardSound late;
        late.add(static_cast<uint8_t>(CpcKey::A), true, 1000);
        std::vector<int16_t> stretch(2 * kRate / 10, 0);
        late.mix(stretch, kRate, 900000);
        bool silent = true;
        for (const int16_t s : stretch)
            silent = silent && s == 0;
        CHECK(silent);
    }
    if (argc > 1) {
        // RUN"DISC, as a hand types it, with SHIFT for the quotes.
        std::vector<std::tuple<CpcKey, bool, uint64_t>> line;
        uint64_t at = 200000;
        const auto tap = [&](CpcKey key, uint64_t hold, uint64_t gap) {
            line.push_back({key, true, at});
            line.push_back({key, false, at + hold});
            at += gap;
        };
        for (const CpcKey key : {CpcKey::R, CpcKey::U, CpcKey::N})
            tap(key, 85000, 170000);
        line.push_back({CpcKey::Shift, true, at});
        tap(CpcKey::Num2, 80000, 120000);
        line.push_back({CpcKey::Shift, false, at});
        at += 150000;
        for (const CpcKey key : {CpcKey::D, CpcKey::I, CpcKey::S, CpcKey::C})
            tap(key, 75000, 150000);
        at += 150000;
        tap(CpcKey::Return, 110000, 600000);
        for (int n = 0; n < 6; ++n)
            tap(CpcKey::Space, 70000, 200000);
        writeWav(argv[1], play(line, static_cast<double>(at) / 1e6 + 0.8));
    }
    return checkSummary("keyboard_sound");
}
