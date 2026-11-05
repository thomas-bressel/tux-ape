#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// The CPC's keyboard heard as it is typed on. Its keys are plungers on
// springs over a membrane, in a hollow plastic case: going down, the finger
// meets the cap, then the plunger strikes the bottom of its travel and the
// case rings with it; coming up, the cap knocks against its top stop, much
// more lightly. The space bar is louder and rings longer; the wide keys
// (SHIFT, RETURN, CONTROL...) a little lower than the letters. Each key is
// heard where it is on the keyboard, from left to right, in a small office,
// not much furnished: a few reflections off the walls and the desk, and a
// short tail.
//
// The strokes are a CPC 464's, recorded: a few takes of each, one picked
// at a time, struck a little harder or softer.
class KeyboardSound {
public:
    // A key of the CPC (by CpcKey's number) went down or came up, at that
    // microsecond of the machine's time.
    void add(uint8_t key, bool down, uint64_t at);
    // Adds the keyboard's sounds to a stretch of sound, stereo samples at
    // so many a second, the machine's time having come to `now`.
    void mix(std::vector<int16_t>& stereo, int rate, uint64_t now);
    // Silence at once.
    void reset();

private:
    enum Kind { Letter, Wide, Space, kKinds };
    struct Stroke {
        uint64_t at;
        uint8_t key;
        bool down;
    };
    struct Voice {
        const std::vector<float>* sound;
        size_t at;
        float left, right;
    };
    // One of the room's echoes: a delay that feeds itself back, through
    // what the room's soft things take of the highs; and one that blurs.
    struct Comb {
        std::vector<float> line;
        size_t at = 0;
        float feedback = 0, damp = 0, kept = 0;
        float step(float in);
    };
    struct Allpass {
        std::vector<float> line;
        size_t at = 0;
        float step(float in);
    };

    std::deque<Stroke> strokes_;
    std::vector<Voice> voices_;
    // Each kind's takes, coming up and going down, at the rate played.
    std::array<std::array<std::vector<std::vector<float>>, 2>, kKinds> takes_;
    int rate_ = 0;
    uint64_t heard_ = 0;  // the machine's time at the end of the last stretch
    uint32_t noise_ = 0x9E3779B9;
    // The room: early reflections from a line of the last few
    // hundredths of a second, then the tail on each side.
    std::vector<float> early_;
    size_t earlyAt_ = 0;
    std::array<std::vector<Comb>, 2> combs_;
    std::array<std::vector<Allpass>, 2> allpasses_;
    int ringing_ = 0;  // samples the room may still be heard

    float random();
    void prepare(int rate);
};
