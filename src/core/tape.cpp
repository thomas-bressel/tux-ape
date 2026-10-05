#include "core/tape.h"

#include <algorithm>
#include <cstring>

namespace tuxape {

namespace {

const char* blockName(uint8_t id)
{
    switch (id) {
    case 0x10: return "Standard Data";
    case 0x11: return "Turbo Data";
    case 0x12: return "Pure Tone";
    case 0x13: return "Sequence of Pulses";
    case 0x14: return "Pure Data";
    case 0x15: return "Direct Recording";
    case 0x20: return "Pause";
    case 0x21: return "Group: ";
    case 0x22: return "End of Group";
    case 0x23: return "Goto Block ";
    case 0x24: return "Loop Start ";
    case 0x25: return "Loop End";
    case 0x26: return "Sequence of Calls";
    case 0x27: return "Return From Call";
    case 0x28: return "Select Block";
    case 0x2A: return "48K Spectrum Stop Tape";
    case 0x30: return "Description";
    case 0x31: return "Message";
    case 0x32: return "Archive Information";
    case 0x33: return "Hardware";
    case 0x34: return "Emulation Info";
    case 0x35: return "Custom Info";
    case 0x40: return "Snapshot";
    case 0x5A: return "Appended TZX File";
    default: return "Unknown: ";
    }
}

constexpr uint32_t kMillisecond = 3500;  // in T-states

}  // namespace

// The blocks are those of the TZX format, revision 1.20. Times are in
// T-states of a ZX Spectrum, 3.5 MHz, on the CPC too.
std::optional<Tape> Tape::parseCdt(std::span<const uint8_t> file)
{
    if (file.size() < 10 || std::memcmp(file.data(), "ZXTape!\x1A", 8) != 0)
        return std::nullopt;
    Tape tape;
    std::vector<uint32_t>& pulses = tape.pulses_;
    size_t pos = 10;
    size_t loopStart = 0;
    unsigned loopCount = 0;

    auto word = [&](size_t at) { return static_cast<uint32_t>(file[at] | file[at + 1] << 8); };
    auto triple = [&](size_t at) { return word(at) | static_cast<uint32_t>(file[at + 2]) << 16; };
    auto quad = [&](size_t at) { return triple(at) | static_cast<uint32_t>(file[at + 3]) << 24; };
    auto tone = [&](uint32_t length, uint32_t count) { pulses.insert(pulses.end(), count, length & kLength); };
    // A silence: the edge the data ended on is given a millisecond, then
    // the level is low.
    auto pause = [&](uint32_t milliseconds) {
        if (milliseconds == 0)
            return;
        pulses.push_back(kMillisecond);
        if (milliseconds > 1)
            pulses.push_back(kLow | (milliseconds - 1) * kMillisecond);
    };
    // Bytes, the high bit first, each bit two pulses of the same length.
    auto data = [&](size_t at, size_t length, unsigned usedBits, uint32_t zero, uint32_t one) {
        for (size_t i = 0; i < length; ++i) {
            const unsigned bits = i + 1 == length ? std::min(usedBits, 8u) : 8;
            for (unsigned bit = 0; bit < bits; ++bit)
                tone((file[at + i] << bit & 0x80) ? one : zero, 2);
        }
    };

    while (pos < file.size()) {
        const uint8_t id = file[pos++];
        const size_t left = file.size() - pos;
        // The length of what follows the block's number, or more than is
        // left if the block is cut short.
        auto sized = [&](size_t header, size_t body) { return left < header ? left + 1 : header + body; };
        size_t size = 0;
        switch (id) {
        case 0x10: size = sized(4, left >= 4 ? word(pos + 2) : 0); break;
        case 0x11: size = sized(18, left >= 18 ? triple(pos + 15) : 0); break;
        case 0x12: size = 4; break;
        case 0x13: size = sized(1, left >= 1 ? file[pos] * 2u : 0); break;
        case 0x14: size = sized(10, left >= 10 ? triple(pos + 7) : 0); break;
        case 0x15: size = sized(8, left >= 8 ? triple(pos + 5) : 0); break;
        case 0x20: case 0x23: case 0x24: size = 2; break;
        case 0x21: case 0x30: size = sized(1, left >= 1 ? file[pos] : 0); break;
        case 0x22: case 0x25: case 0x27: size = 0; break;
        case 0x26: size = sized(2, left >= 2 ? word(pos) * 2u : 0); break;
        case 0x28: case 0x32: size = sized(2, left >= 2 ? word(pos) : 0); break;
        case 0x31: size = sized(2, left >= 2 ? file[pos + 1] : 0); break;
        case 0x33: size = sized(1, left >= 1 ? file[pos] * 3u : 0); break;
        case 0x34: size = 8; break;
        case 0x35: size = sized(20, left >= 20 ? quad(pos + 16) : 0); break;
        case 0x40: size = sized(4, left >= 4 ? triple(pos + 1) : 0); break;
        case 0x5A: size = 9; break;
        // Blocks added to the format since carry their length.
        default: size = sized(4, left >= 4 ? quad(pos) : 0); break;
        }
        if (size > left)
            break;

        Block block;
        block.id = id;
        block.name = blockName(id);
        block.firstPulse = pulses.size();
        switch (id) {
        case 0x10: {
            // The ZX Spectrum's own way: a long leader ahead of a header,
            // a shorter one ahead of data.
            const size_t length = word(pos + 2);
            tone(2168, length > 0 && file[pos + 4] < 0x80 ? 8063 : 3223);
            tone(667, 1);
            tone(735, 1);
            data(pos + 4, length, 8, 855, 1710);
            pause(word(pos));
            break;
        }
        case 0x11:
            tone(word(pos), word(pos + 10));
            tone(word(pos + 2), 1);
            tone(word(pos + 4), 1);
            data(pos + 18, triple(pos + 15), file[pos + 12], word(pos + 6), word(pos + 8));
            pause(word(pos + 13));
            break;
        case 0x12: tone(word(pos), word(pos + 2)); break;
        case 0x13:
            for (unsigned i = 0; i < file[pos]; ++i)
                tone(word(pos + 1 + i * 2), 1);
            break;
        case 0x14:
            data(pos + 10, triple(pos + 7), file[pos + 4], word(pos), word(pos + 2));
            pause(word(pos + 5));
            break;
        case 0x15: {
            // Samples: runs of one level become one pulse of that level.
            const size_t length = triple(pos + 5);
            const uint64_t samples = length == 0 ? 0 : (length - 1) * 8 + std::min<unsigned>(file[pos + 4], 8);
            uint64_t run = 0;
            bool high = false;
            for (uint64_t i = 0; i <= samples; ++i) {
                const bool sample = i < samples && (file[pos + 8 + i / 8] << (i % 8) & 0x80);
                if (i == samples || (run > 0 && sample != high)) {
                    if (run > 0)
                        pulses.push_back((high ? kHigh : kLow) | static_cast<uint32_t>(std::min<uint64_t>(run * word(pos), kLength)));
                    run = 0;
                }
                high = sample;
                ++run;
            }
            pause(word(pos + 2));
            break;
        }
        case 0x20:
            if (word(pos) == 0)
                pulses.push_back(kStop | kLow);
            else
                pause(word(pos));
            break;
        case 0x21: block.name.append(reinterpret_cast<const char*>(&file[pos + 1]), file[pos]); break;
        case 0x23: block.name += std::to_string(static_cast<int16_t>(word(pos))); break;
        case 0x24:
            block.name += std::to_string(word(pos));
            loopStart = pulses.size();
            loopCount = word(pos);
            break;
        case 0x25: {
            // Loops are played out: what the loop holds, as many times
            // again as it says.
            const std::vector<uint32_t> body(pulses.begin() + static_cast<ptrdiff_t>(std::min(loopStart, pulses.size())),
                                             pulses.end());
            for (unsigned i = 1; i < loopCount; ++i)
                pulses.insert(pulses.end(), body.begin(), body.end());
            loopCount = 0;
            break;
        }
        default:
            if (block.name == "Unknown: ")
                block.name += std::to_string(id);
            break;
        }
        tape.blocks_.push_back(std::move(block));
        pos += size;
    }
    if (tape.blocks_.empty())
        return std::nullopt;
    return tape;
}

void TapeDeck::insert(Tape tape)
{
    tape_ = std::move(tape);
    loaded_ = true;
    playing_ = false;
    position_ = 0;
    block_ = 0;
    remaining_ = 0;
    level_ = false;
}

void TapeDeck::eject()
{
    insert(Tape());
    loaded_ = false;
}

void TapeDeck::advance(uint64_t now)
{
    // The machine's clock starts again when the machine does.
    const uint64_t elapsed = now > time_ ? now - time_ : 0;
    time_ = now;
    if (!loaded_ || !playing_ || !motor_)
        return;
    // Seven half T-states to the microsecond.
    uint64_t budget = elapsed * 7;
    const std::vector<uint32_t>& pulses = tape_.pulses();
    for (;;) {
        if (budget < remaining_) {
            remaining_ -= budget;
            return;
        }
        budget -= remaining_;
        remaining_ = 0;
        if (position_ == pulses.size()) {
            playing_ = false;  // the end of the tape
            return;
        }
        const std::vector<Tape::Block>& blocks = tape_.blocks();
        while (block_ + 1 < blocks.size() && blocks[block_ + 1].firstPulse <= position_)
            ++block_;
        const uint32_t pulse = pulses[position_++];
        level_ = (pulse & Tape::kHigh) ? true : (pulse & Tape::kLow) ? false : !level_;
        remaining_ = static_cast<uint64_t>(pulse & Tape::kLength) * 2;
        if (pulse & Tape::kStop) {
            playing_ = false;
            return;
        }
    }
}

void TapeDeck::play(uint64_t now)
{
    advance(now);
    playing_ = loaded_ && !atEnd();
}

void TapeDeck::stop(uint64_t now)
{
    advance(now);
    playing_ = false;
}

void TapeDeck::seekBlock(size_t block, uint64_t now)
{
    advance(now);
    const std::vector<Tape::Block>& blocks = tape_.blocks();
    position_ = block < blocks.size() ? blocks[block].firstPulse : tape_.pulses().size();
    block_ = blocks.empty() ? 0 : std::min(block, blocks.size() - 1);
    remaining_ = 0;
    level_ = false;
}

void TapeDeck::setMotor(bool on, uint64_t now)
{
    if (on == motor_)
        return;
    advance(now);
    motor_ = on;
}

bool TapeDeck::level(uint64_t now)
{
    advance(now);
    return level_;
}

}  // namespace tuxape
