// Recordings: the sound as a WAV file, and the sound chip's registers as a
// YM file.

#include <filesystem>
#include <vector>

#include "check.h"
#include "core/files.h"
#include "core/psg.h"
#include "core/recording.h"

namespace {

using namespace tuxape;

uint32_t little(const std::vector<uint8_t>& data, size_t at, int bytes)
{
    uint32_t value = 0;
    for (int i = 0; i < bytes; ++i)
        value |= static_cast<uint32_t>(data[at + static_cast<size_t>(i)]) << (8 * i);
    return value;
}

uint32_t big(const std::vector<uint8_t>& data, size_t at, int bytes)
{
    uint32_t value = 0;
    for (int i = 0; i < bytes; ++i)
        value = value << 8 | data[at + static_cast<size_t>(i)];
    return value;
}

void testWav()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "tuxape_test.wav";
    WavRecorder wav;
    CHECK(!wav.active());
    CHECK(wav.start(path, 22050));
    CHECK(wav.active());
    const int16_t first[] = {0, 0, 1000, -1000, 32767, -32768};
    const int16_t second[] = {0x1234, -2};
    wav.write(first);
    wav.write(second);
    wav.write({});
    wav.stop();
    CHECK(!wav.active());
    const auto file = readFile(path);
    CHECK(file.has_value());
    if (file) {
        CHECK_EQ(file->size(), 44 + 16);
        CHECK(std::string(file->begin(), file->begin() + 4) == "RIFF");
        CHECK_EQ(little(*file, 4, 4), 36 + 16);
        CHECK(std::string(file->begin() + 8, file->begin() + 16) == "WAVEfmt ");
        CHECK_EQ(little(*file, 16, 4), 16);
        CHECK_EQ(little(*file, 20, 2), 1);       // PCM
        CHECK_EQ(little(*file, 22, 2), 2);       // stereo
        CHECK_EQ(little(*file, 24, 4), 22050);
        CHECK_EQ(little(*file, 28, 4), 22050 * 4);
        CHECK_EQ(little(*file, 32, 2), 4);
        CHECK_EQ(little(*file, 34, 2), 16);
        CHECK(std::string(file->begin() + 36, file->begin() + 40) == "data");
        CHECK_EQ(little(*file, 40, 4), 16);
        CHECK_EQ(little(*file, 44 + 4, 2), 1000);
        CHECK_EQ(little(*file, 44 + 6, 2), 0xFC18);  // -1000
        CHECK_EQ(little(*file, 44 + 12, 2), 0x1234);
        CHECK_EQ(little(*file, 44 + 14, 2), 0xFFFE);
    }
    // A file that cannot be made.
    CHECK(!wav.start(std::filesystem::temp_directory_path() / "no such folder" / "x.wav", 44100));
    CHECK(!wav.active());
    std::filesystem::remove(path);
}

void testYm()
{
    Psg psg;
    psg.reset();
    YmRecorder ym;
    CHECK(!ym.active());
    ym.frame(psg);  // not recording: nothing kept
    ym.start();
    CHECK(ym.active());
    // Three frames: a tone and its volume; the envelope's shape written;
    // nothing new.
    psg.setRegister(0, 0x55);
    psg.setRegister(8, 0x0F);
    ym.frame(psg);
    psg.setRegister(0, 0x56);
    psg.setRegister(13, 0x0A);
    ym.frame(psg);
    ym.frame(psg);
    CHECK_EQ(ym.frames(), 3);
    const std::vector<uint8_t> file = ym.finish("Tune");
    CHECK(!ym.active());
    CHECK(std::string(file.begin(), file.begin() + 12) == "YM5!LeOnArD!");
    CHECK_EQ(big(file, 12, 4), 3);        // frames
    CHECK_EQ(big(file, 16, 4), 1);        // interleaved
    CHECK_EQ(big(file, 20, 2), 0);
    CHECK_EQ(big(file, 22, 4), 1000000);
    CHECK_EQ(big(file, 26, 2), 50);
    CHECK_EQ(big(file, 28, 4), 0);
    CHECK_EQ(big(file, 32, 2), 0);
    CHECK(std::string(file.begin() + 34, file.begin() + 39) == std::string("Tune\0", 5));
    CHECK_EQ(file[39], 0);  // no author
    size_t data = 40;
    while (file[data] != 0)
        ++data;
    ++data;
    CHECK_EQ(file.size(), data + 16 * 3 + 4);
    // Register 0 over the three frames, then register 1, and so on.
    CHECK_EQ(file[data + 0], 0x55);
    CHECK_EQ(file[data + 1], 0x56);
    CHECK_EQ(file[data + 2], 0x56);
    CHECK_EQ(file[data + 8 * 3], 0x0F);
    // The envelope's shape: only in the frame it was written in.
    CHECK_EQ(file[data + 13 * 3 + 0], 0xFF);
    CHECK_EQ(file[data + 13 * 3 + 1], 0x0A);
    CHECK_EQ(file[data + 13 * 3 + 2], 0xFF);
    CHECK(std::string(file.end() - 4, file.end()) == "End!");
    CHECK_EQ(ym.frames(), 0);
}

}  // namespace

int main()
{
    testWav();
    testYm();
    return checkSummary("recording");
}
