#include "core/recording.h"

#include "core/psg.h"

namespace tuxape {

namespace {

void little(uint8_t* at, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        at[i] = static_cast<uint8_t>(value >> (8 * i));
}

void big(std::vector<uint8_t>& out, uint32_t value, int bytes)
{
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}

}  // namespace

bool WavRecorder::start(const std::filesystem::path& path, int sampleRate)
{
    stop();
    file_ = std::fopen(path.string().c_str(), "wb");
    if (!file_)
        return false;
    sampleRate_ = sampleRate;
    bytes_ = 0;
    header();
    return true;
}

// The 44 bytes ahead of the samples: PCM, two channels of 16 bits.
void WavRecorder::header()
{
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0};
    little(h + 4, 36 + bytes_, 4);
    little(h + 24, static_cast<uint32_t>(sampleRate_), 4);
    little(h + 28, static_cast<uint32_t>(sampleRate_) * 4, 4);
    h[32] = 4;
    h[34] = 16;
    h[36] = 'd';
    h[37] = 'a';
    h[38] = 't';
    h[39] = 'a';
    little(h + 40, bytes_, 4);
    std::fseek(file_, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof h, file_);
}

void WavRecorder::write(std::span<const int16_t> samples)
{
    if (!file_ || samples.empty())
        return;
    // Low byte first, whatever the host.
    std::vector<uint8_t> bytes(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); ++i)
        little(&bytes[i * 2], static_cast<uint16_t>(samples[i]), 2);
    bytes_ += static_cast<uint32_t>(std::fwrite(bytes.data(), 1, bytes.size(), file_));
}

void WavRecorder::stop()
{
    if (!file_)
        return;
    header();
    std::fclose(file_);
    file_ = nullptr;
}

void YmRecorder::start()
{
    registers_.clear();
    active_ = true;
}

void YmRecorder::frame(Psg& psg)
{
    // The envelope's shape counts when it is written, even with the value
    // it had: FF stands for "not written in this frame".
    const bool shapeWritten = psg.takeEnvelopeWritten();
    if (!active_)
        return;
    for (int number = 0; number < 14; ++number)
        registers_.push_back(number == 13 && !shapeWritten ? 0xFF : psg.reg(number));
    registers_.push_back(0);
    registers_.push_back(0);
}

std::vector<uint8_t> YmRecorder::finish(const std::string& title)
{
    active_ = false;
    const uint32_t count = static_cast<uint32_t>(frames());
    std::vector<uint8_t> out = {'Y', 'M', '5', '!', 'L', 'e', 'O', 'n', 'A', 'r', 'D', '!'};
    big(out, count, 4);
    big(out, 1, 4);        // the registers are given one after the other
    big(out, 0, 2);        // no sampled sounds
    big(out, 1000000, 4);  // the CPC's sound chip runs at 1 MHz
    big(out, 50, 2);       // frames a second
    big(out, 0, 4);        // loops from the start
    big(out, 0, 2);        // nothing more in the header
    out.insert(out.end(), title.begin(), title.end());
    out.push_back(0);
    out.push_back(0);  // no author
    const std::string comment = "Recorded with TuxAPE";
    out.insert(out.end(), comment.begin(), comment.end());
    out.push_back(0);
    for (size_t number = 0; number < 16; ++number)
        for (uint32_t i = 0; i < count; ++i)
            out.push_back(registers_[i * 16 + number]);
    out.insert(out.end(), {'E', 'n', 'd', '!'});
    registers_.clear();
    return out;
}

}  // namespace tuxape
