#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <QFile>
#include <QString>

// The picture and the sound, written to an AVI file as they are made: one
// JPEG picture for each of the machine's frames (768 x 540, the screen at
// its usual size) and the sound that goes with it, 16 bits stereo. Any
// video player reads it.
class AviRecorder {
public:
    ~AviRecorder() { stop(); }

    // False if the file cannot be created.
    bool start(const QString& path, int sampleRate);
    bool active() const { return file_.isOpen(); }
    // One of the machine's frames: its picture (768 x 270, each line to be
    // shown twice) and the samples made during it, left and right in turn.
    // The recording ends by itself when the file reaches what an AVI file
    // can hold.
    void addFrame(const uint32_t* pixels, std::span<const int16_t> samples);
    // Completes the file: its headers and its index.
    void stop();
    int frames() const { return frames_; }

private:
    struct IndexEntry {
        uint32_t id, flags, offset, size;
    };
    QFile file_;
    int sampleRate_ = 0;
    int frames_ = 0;
    uint64_t sampleFrames_ = 0;
    uint32_t largestPicture_ = 0;
    qint64 moviStart_ = 0;  // where the "movi" word is
    std::vector<IndexEntry> index_;

    void headers();
    void chunk(const char* id, const QByteArray& data, uint32_t flags);
};
