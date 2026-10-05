#pragma once

#include <cstddef>
#include <cstdint>

// Plays 16-bit stereo sound through the host's default audio device (SDL2).
//
// Samples are queued as the emulator produces them. The queue is kept about
// kTargetLatency long: short enough that sound follows the picture, long
// enough to ride out the host's scheduling hiccups.
class AudioOutput {
public:
    static constexpr double kTargetLatency = 0.06;  // seconds, unless set otherwise

    // How much sound is kept waiting to be played. More rides out a busier
    // host, at the price of sound that trails the picture.
    void setTargetLatency(double seconds) { targetLatency_ = seconds < 0.02 ? 0.02 : seconds; }
    double targetLatency() const { return targetLatency_; }

    AudioOutput() = default;
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Opens the device. Returns false if the host has no usable audio
    // output; the emulator then simply runs silent.
    bool open(int sampleRate);
    void close();
    bool isOpen() const { return device_ != 0; }
    int sampleRate() const { return sampleRate_; }

    // Queues samples (left and right interleaved; `count` values in all)
    // and returns how much sound is now waiting to be played, in seconds.
    double queue(const int16_t* samples, size_t count);
    // Drops everything not yet played.
    void clear();

private:
    unsigned device_ = 0;
    int sampleRate_ = 0;
    double targetLatency_ = kTargetLatency;
};
