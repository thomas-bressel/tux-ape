#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace tuxape {

class Psg;

// Sound written to a WAV file as it is made: 16 bits, stereo.
class WavRecorder {
public:
    ~WavRecorder() { stop(); }

    // False if the file cannot be created.
    bool start(const std::filesystem::path& path, int sampleRate);
    // Samples, left and right in turn.
    void write(std::span<const int16_t> samples);
    // Completes the file: its header says how long it is.
    void stop();
    bool active() const { return file_ != nullptr; }
    int sampleRate() const { return sampleRate_; }

private:
    std::FILE* file_ = nullptr;
    int sampleRate_ = 0;
    uint32_t bytes_ = 0;

    void header();
};

// The sound chip's registers, noted fifty times a second, as a YM file
// (version 5, not packed): a tune as the players of YM music take it.
class YmRecorder {
public:
    void start();
    bool active() const { return active_; }
    // To be called once a frame.
    void frame(Psg& psg);
    int frames() const { return static_cast<int>(registers_.size() / 16); }
    // Ends the recording. The file: its header, the sixteen registers one
    // after the other (all the frames of register 0, then of register 1,
    // and so on), and "End!".
    std::vector<uint8_t> finish(const std::string& title = {});

private:
    bool active_ = false;
    std::vector<uint8_t> registers_;  // sixteen to a frame
};

}  // namespace tuxape
