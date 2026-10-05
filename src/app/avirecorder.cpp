#include "avirecorder.h"

#include <QBuffer>
#include <QImage>

#include "core/monitor.h"

namespace {

constexpr int kWidth = tuxape::Monitor::kWidth;
constexpr int kHeight = tuxape::Monitor::kHeight * 2;
constexpr uint32_t kFrameMicroseconds = 312 * 64;
// An AVI file's sizes are 32 bits: the recording stops short of that.
constexpr qint64 kLargestFile = 1900LL * 1024 * 1024;
// Room for the headers, which are written again when the recording ends.
constexpr qint64 kHeaderSize = 12 + (12 + 64 + (12 + 64 + 48) + (12 + 64 + 24)) + 12;

void put(QByteArray& out, uint32_t value, int bytes = 4)
{
    for (int i = 0; i < bytes; ++i)
        out.append(static_cast<char>(value >> (8 * i)));
}

void word(QByteArray& out, const char* four)
{
    out.append(four, 4);
}

}  // namespace

bool AviRecorder::start(const QString& path, int sampleRate)
{
    stop();
    file_.setFileName(path);
    if (!file_.open(QIODevice::WriteOnly))
        return false;
    sampleRate_ = sampleRate;
    frames_ = 0;
    sampleFrames_ = 0;
    largestPicture_ = 0;
    index_.clear();
    headers();
    return true;
}

// "RIFF AVI", the list of headers (the file's, then each stream's with its
// format), and the start of the list of pictures and sound.
void AviRecorder::headers()
{
    // The sound's rate is what was really made over the time recorded: it
    // then lasts exactly as long as the pictures.
    uint32_t rate = static_cast<uint32_t>(sampleRate_);
    if (frames_ > 0 && sampleFrames_ > 0)
        rate = static_cast<uint32_t>(sampleFrames_ * 1000000.0 / (static_cast<double>(frames_) * kFrameMicroseconds) + 0.5);
    const qint64 end = std::max<qint64>(file_.size(), kHeaderSize);

    QByteArray h;
    word(h, "RIFF");
    put(h, static_cast<uint32_t>(end - 8));
    word(h, "AVI ");
    word(h, "LIST");
    put(h, static_cast<uint32_t>(kHeaderSize - 12 - 12 - 8));
    word(h, "hdrl");
    word(h, "avih");
    put(h, 56);
    put(h, kFrameMicroseconds);
    put(h, largestPicture_ * 51 + rate * 4);  // bytes a second, at most
    put(h, 0);
    put(h, 0x10);  // the file has an index
    put(h, static_cast<uint32_t>(frames_));
    put(h, 0);
    put(h, 2);  // two streams
    put(h, largestPicture_);
    put(h, kWidth);
    put(h, kHeight);
    for (int i = 0; i < 4; ++i)
        put(h, 0);

    // The pictures.
    word(h, "LIST");
    put(h, 4 + 64 + 48);
    word(h, "strl");
    word(h, "strh");
    put(h, 56);
    word(h, "vids");
    word(h, "MJPG");
    put(h, 0);
    put(h, 0);
    put(h, 0);
    put(h, kFrameMicroseconds);  // a frame lasts this many
    put(h, 1000000);             // of these to the second
    put(h, 0);
    put(h, static_cast<uint32_t>(frames_));
    put(h, largestPicture_);
    put(h, 0xFFFFFFFF);
    put(h, 0);
    put(h, 0, 2);
    put(h, 0, 2);
    put(h, kWidth, 2);
    put(h, kHeight, 2);
    word(h, "strf");
    put(h, 40);
    put(h, 40);
    put(h, kWidth);
    put(h, kHeight);
    put(h, 1, 2);
    put(h, 24, 2);
    word(h, "MJPG");
    put(h, kWidth * kHeight * 3);
    for (int i = 0; i < 4; ++i)
        put(h, 0);

    // The sound.
    word(h, "LIST");
    put(h, 4 + 64 + 24);
    word(h, "strl");
    word(h, "strh");
    put(h, 56);
    word(h, "auds");
    put(h, 0);
    put(h, 0);
    put(h, 0);
    put(h, 0);
    put(h, 1);
    put(h, rate);
    put(h, 0);
    put(h, static_cast<uint32_t>(sampleFrames_));
    put(h, rate);
    put(h, 0xFFFFFFFF);
    put(h, 4);  // bytes to a sample of both channels
    put(h, 0, 2);
    put(h, 0, 2);
    put(h, 0, 2);
    put(h, 0, 2);
    word(h, "strf");
    put(h, 16);
    put(h, 1, 2);  // PCM
    put(h, 2, 2);
    put(h, rate);
    put(h, rate * 4);
    put(h, 4, 2);
    put(h, 16, 2);

    word(h, "LIST");
    uint32_t moviSize = 4;
    for (const IndexEntry& entry : index_)
        moviSize += 8 + entry.size + (entry.size & 1);
    put(h, moviSize);
    moviStart_ = h.size();
    word(h, "movi");

    file_.seek(0);
    file_.write(h);
    file_.seek(end);
}

void AviRecorder::chunk(const char* id, const QByteArray& data, uint32_t flags)
{
    QByteArray header;
    word(header, id);
    put(header, static_cast<uint32_t>(data.size()));
    index_.push_back({static_cast<uint32_t>(static_cast<uint8_t>(id[0]) | static_cast<uint8_t>(id[1]) << 8
                                             | static_cast<uint8_t>(id[2]) << 16 | static_cast<uint8_t>(id[3]) << 24),
                      flags, static_cast<uint32_t>(file_.pos() - moviStart_), static_cast<uint32_t>(data.size())});
    file_.write(header);
    file_.write(data);
    if (data.size() & 1)
        file_.write("\0", 1);
}

void AviRecorder::addFrame(const uint32_t* pixels, std::span<const int16_t> samples)
{
    if (!active())
        return;
    // Each of the 270 lines twice, as the screen shows them.
    QImage picture(kWidth, kHeight, QImage::Format_RGB32);
    for (int y = 0; y < kHeight; ++y)
        std::memcpy(picture.scanLine(y), pixels + (y / 2) * kWidth, static_cast<size_t>(kWidth) * 4);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "JPEG", 90);
    largestPicture_ = std::max(largestPicture_, static_cast<uint32_t>(jpeg.size()));
    chunk("00dc", jpeg, 0x10);
    ++frames_;

    if (!samples.empty()) {
        QByteArray sound;
        sound.reserve(static_cast<qsizetype>(samples.size() * 2));
        for (const int16_t sample : samples)
            put(sound, static_cast<uint16_t>(sample), 2);
        chunk("01wb", sound, 0x10);
        sampleFrames_ += samples.size() / 2;
    }
    if (file_.pos() > kLargestFile)
        stop();
}

void AviRecorder::stop()
{
    if (!active())
        return;
    // The index: where each picture and each piece of sound is.
    QByteArray index;
    word(index, "idx1");
    put(index, static_cast<uint32_t>(index_.size() * 16));
    for (const IndexEntry& entry : index_) {
        put(index, entry.id);
        put(index, entry.flags);
        put(index, entry.offset);
        put(index, entry.size);
    }
    file_.seek(file_.size());
    // The headers first: they count the list of pictures, not the index.
    headers();
    file_.write(index);
    // And the size of the whole.
    const qint64 size = file_.size();
    file_.seek(4);
    QByteArray total;
    put(total, static_cast<uint32_t>(size - 8));
    file_.write(total);
    file_.close();
    index_.clear();
}
