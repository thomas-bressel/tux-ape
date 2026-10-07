// The sound chip: tone and noise generators, envelopes, the mixer, and the
// sound a BASIC SOUND command produces on a whole machine.

#include <algorithm>
#include <cmath>
#include <vector>

#include "check.h"
#include "core/audio.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/psg.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

void set(Psg& psg, uint8_t reg, uint8_t value)
{
    psg.selectRegister(reg);
    psg.write(value);
}

void testRegisters()
{
    Psg psg;
    set(psg, 1, 0xFF);  // coarse tone period: 4 bits
    CHECK_EQ(psg.reg(1), 0x0F);
    set(psg, 6, 0xFF);  // noise period: 5 bits
    CHECK_EQ(psg.reg(6), 0x1F);
    set(psg, 8, 0xFF);  // volume: 4 bits plus the envelope flag
    CHECK_EQ(psg.reg(8), 0x1F);
    psg.selectRegister(8);
    CHECK_EQ(psg.read(0xFF), 0x1F);

    // Register numbers above 15 address nothing.
    psg.selectRegister(0x18);
    psg.write(0x55);
    CHECK_EQ(psg.reg(8), 0x1F);
    CHECK_EQ(psg.read(0x00), 0xFF);

    // Port A reads the keyboard when it is an input.
    set(psg, 7, 0x3F);
    psg.selectRegister(14);
    CHECK_EQ(psg.read(0xFB), 0xFB);
}

void testTone()
{
    Psg psg;
    set(psg, 0, 100);   // period 100: 62500 / 100 = 625 Hz
    set(psg, 7, 0x3E);  // tone A only
    set(psg, 8, 15);
    int rises = 0;
    bool high = psg.level(0) != 0;
    for (int tick = 0; tick < AudioMixer::kStepsPerSecond; ++tick) {
        psg.tick();
        const bool now = psg.level(0) != 0;
        rises += now && !high;
        high = now;
    }
    CHECK_EQ(rises, 625);
    CHECK_EQ(psg.level(1), 0);  // B is at volume 0

    // With tone and noise both off, the channel is a plain DAC: this is how
    // digitised sound is played.
    set(psg, 7, 0x3F);
    for (uint8_t v = 0; v < 16; ++v) {
        set(psg, 8, v);
        CHECK_EQ(psg.level(0), v);
    }
    CHECK(Psg::amplitude(0) == 0.0f);
    CHECK(Psg::amplitude(15) == 1.0f);
    for (uint8_t v = 1; v < 16; ++v)
        CHECK(Psg::amplitude(v) > Psg::amplitude(v - 1));
}

void testNoise()
{
    Psg psg;
    set(psg, 6, 1);
    set(psg, 7, 0x37);  // noise on A only
    set(psg, 8, 15);
    // The shift register runs at 62.5 kHz with period 1 and is about half
    // the time high.
    int high = 0, changes = 0;
    bool last = false;
    const int ticks = 100000;
    for (int tick = 0; tick < ticks; ++tick) {
        psg.tick();
        const bool now = psg.level(0) != 0;
        high += now;
        changes += now != last;
        last = now;
    }
    CHECK(high > ticks * 45 / 100 && high < ticks * 55 / 100);
    CHECK(changes > ticks / 5 && changes < ticks / 3);
}

// Levels of the first 48 envelope steps for a shape.
std::vector<int> envelope(uint8_t shape)
{
    Psg psg;
    set(psg, 7, 0x3F);
    set(psg, 8, 0x10);  // channel A follows the envelope
    set(psg, 11, 1);    // one step every 2 ticks
    set(psg, 13, shape);
    std::vector<int> levels;
    for (int step = 0; step < 48; ++step) {
        levels.push_back(psg.level(0));
        psg.tick();
        psg.tick();
    }
    return levels;
}

void testEnvelopes()
{
    auto ramp = [](bool up, int step) { return up ? step : 15 - step; };
    for (int shape = 0; shape < 16; ++shape) {
        const std::vector<int> levels = envelope(static_cast<uint8_t>(shape));
        const bool attack = shape & 4;
        for (int step = 0; step < 16; ++step)
            CHECK_EQ(levels[static_cast<size_t>(step)], ramp(attack, step));

        // What follows the first ramp.
        for (int step = 16; step < 48; ++step) {
            const int cycle = step / 16, pos = step % 16;
            int want;
            if (shape < 8)
                want = 0;  // single ramp, then silence
            else if (shape & 1)
                want = ((shape & 2) ? !attack : attack) ? 15 : 0;  // hold
            else if (shape & 2)
                want = ramp(cycle % 2 ? !attack : attack, pos);  // triangle
            else
                want = ramp(attack, pos);  // sawtooth
            if (levels[static_cast<size_t>(step)] != want) {
                std::printf("envelope shape %d, step %d: level %d, want %d\n", shape, step,
                            levels[static_cast<size_t>(step)], want);
                ++g_failures;
                break;
            }
        }
    }
}

void testMixer()
{
    AudioMixer mixer;
    mixer.addStep(1, 1, 1);
    CHECK(mixer.samples().empty());  // off until a rate is set

    mixer.setSampleRate(44100);
    for (int step = 0; step < AudioMixer::kStepsPerSecond; ++step)
        mixer.addStep(1, 0, 0);
    // Exactly one second's worth of stereo samples.
    CHECK_EQ(mixer.samples().size(), 2 * 44100);
    // Channel A is on the left only, at two thirds of full scale, once the
    // filter has seen its 64 steps of it (half a millisecond).
    CHECK(mixer.samples()[0] < 21845);
    for (size_t sample : {size_t{100}, size_t{20000}, size_t{44099}}) {
        CHECK_EQ(mixer.samples()[2 * sample], 21845);
        CHECK_EQ(mixer.samples()[2 * sample + 1], 0);
    }

    mixer.samples().clear();
    mixer.setStereo(false);
    mixer.setVolume(0);
    for (int step = 0; step < 100; ++step)
        mixer.addStep(1, 1, 1);
    CHECK(!mixer.samples().empty());
    CHECK_EQ(mixer.samples()[0], 0);
    mixer.samples().clear();
    mixer.setVolume(15);
    for (int step = 0; step < 100; ++step)
        mixer.addStep(1, 1, 1);
    CHECK_EQ(mixer.samples()[0], 32767);
    CHECK_EQ(mixer.samples()[1], 32767);
}

// The lowest and the highest of the right-hand samples, the first hundred
// (the filter settling) left out.
struct Span {
    int low = 32767;
    int high = -32768;
    int swing() const { return high - low; }
};

Span rightSpan(const std::vector<int16_t>& samples)
{
    Span span;
    for (size_t i = 2 * 100 + 1; i < samples.size(); i += 2) {
        span.low = std::min<int>(span.low, samples[i]);
        span.high = std::max<int>(span.high, samples[i]);
    }
    return span;
}

// A second of channel C at full volume with the tone period given, as the
// host hears it.
Span heardOnTheRight(int period, double rate)
{
    Psg psg;
    set(psg, 4, static_cast<uint8_t>(period));
    set(psg, 5, static_cast<uint8_t>(period >> 8));
    set(psg, 7, 0x38);  // the three tones on, no noise
    set(psg, 10, 15);
    AudioMixer mixer;
    mixer.setSampleRate(rate);
    for (int step = 0; step < AudioMixer::kStepsPerSecond; ++step) {
        psg.tick();
        mixer.addStep(Psg::amplitude(psg.level(0)), Psg::amplitude(psg.level(1)), Psg::amplitude(psg.level(2)));
    }
    return rightSpan(mixer.samples());
}

// What the host's rate cannot carry is not to be heard as something else.
// A cartridge that leaves channel C at full volume with a tone period of 0
// makes a square wave of 62.5 kHz: silence on a CPC, and a whistle at
// 18.4 kHz in an emulator that only averages the steps of each sample.
void testNothingFoldsBack()
{
    for (double rate : {44100.0, 22050.0, 48000.0}) {
        // Periods 0 and 1 are the same 62.5 kHz; 2 is 31.25 kHz. What is
        // left is the level half way up, steady to a thousandth.
        for (int period : {0, 1, 2}) {
            const Span span = heardOnTheRight(period, rate);
            CHECK(span.swing() <= 22);
            CHECK(span.low > 10922 - 22 && span.high < 10922 + 22);
        }
        // A tone the rate does carry comes through whole: 1953 Hz, with
        // the overshoot a square wave has once its top harmonics are gone.
        const Span tone = heardOnTheRight(32, rate);
        CHECK(tone.low < 0 && tone.low > -2600);
        CHECK(tone.high > 21845 && tone.high < 21845 + 2600);
    }
    // 10.4 kHz is heard at 44.1 kHz: its fundamental alone, which swings
    // 4 / pi times as far as the square wave it comes from.
    const Span high = heardOnTheRight(6, 44100);
    CHECK(high.swing() > 26000 && high.swing() < 28500);

    // The front end's nudges to the rate are not heard: a level that does
    // not move stays where it is.
    AudioMixer mixer;
    mixer.setSampleRate(44100);
    for (int step = 0; step < 30000; ++step) {
        if (step % 2500 == 0)
            mixer.setSampleRate(44100 * (step % 5000 == 0 ? 1.004 : 0.996));
        mixer.addStep(0, 0, 1);
    }
    const Span steady = rightSpan(mixer.samples());
    CHECK_EQ(steady.low, 21845);
    CHECK_EQ(steady.high, 21845);
}

void testBasicSound()
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        std::printf("ROM images not found; BASIC sound check skipped\n");
        return;
    }
    cpc.audio().setSampleRate(44100);
    for (int frame = 0; frame < 150; ++frame)
        cpc.runFrame();
    // Silence at the prompt.
    cpc.audio().samples().clear();
    for (int frame = 0; frame < 10; ++frame)
        cpc.runFrame();
    for (int16_t sample : cpc.audio().samples())
        if (sample != 0) {
            ++g_failures;
            std::printf("sound at the BASIC prompt\n");
            break;
        }

    // With the CPC's 1 MHz clock a tone is 62500 / period: period 142 is
    // the A above middle C, 440.1 Hz.
    AutoType keys(cpc.keyboard());
    keys.type("SOUND 1,142,200,15\n");
    while (keys.active()) {
        keys.frame();
        cpc.runFrame();
    }
    for (int frame = 0; frame < 10; ++frame)
        cpc.runFrame();
    cpc.audio().samples().clear();
    for (int frame = 0; frame < 50; ++frame)
        cpc.runFrame();

    const std::vector<int16_t>& samples = cpc.audio().samples();
    // 50 frames are 0.99840 s.
    CHECK(samples.size() > 2 * 44000 && samples.size() < 2 * 44100);
    int rises = 0;
    int16_t peak = 0;
    bool high = false;
    int offLevel = 0;
    for (size_t i = 0; i < samples.size(); i += 2) {
        const int16_t left = samples[i];
        peak = std::max(peak, left);
        // Channel A plays on the left; the right stays silent.
        if (samples[i + 1] != 0)
            ++g_failures;
        if (!high && left > 8000) {
            ++rises, high = true;
            // Twenty samples on, half way along the top, the filter has
            // forgotten the edge: two thirds of full scale.
            if (i + 40 < samples.size() && samples[i + 40] != 21845)
                ++offLevel;
        } else if (high && left < 2000) {
            high = false;
        }
    }
    CHECK(rises >= 438 && rises <= 441);
    CHECK_EQ(offLevel, 0);
    // The edges overshoot, as those of any square wave without its top
    // harmonics do.
    CHECK(peak > 21845 && peak < 21845 + 2600);
}

}  // namespace

int main()
{
    testRegisters();
    testTone();
    testNoise();
    testEnvelopes();
    testMixer();
    testNothingFoldsBack();
    testBasicSound();
    return checkSummary("psg_sound");
}
