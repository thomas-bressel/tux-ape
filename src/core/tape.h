#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tuxape {

// A cassette: the blocks of a CDT file (a TZX file, under the name CPC
// emulators give it), turned into the pulses the tape plays; or a
// recording of a tape's sound, as a WAV or a VOC file.
class Tape {
public:
    struct Block {
        uint8_t id = 0;
        std::string name;       // as WinAPE's Tape Control lists it, e.g. "Turbo Data"
        size_t firstPulse = 0;  // where the block starts among the pulses
    };

    // A pulse is its length in T-states of a 3.5 MHz clock, the unit of the
    // file format. The signal changes level as each pulse starts, unless
    // the pulse says which level it has; kStop stops the tape there until
    // Play is pressed again.
    static constexpr uint32_t kLow = 0x80000000;
    static constexpr uint32_t kHigh = 0x40000000;
    static constexpr uint32_t kStop = 0x20000000;
    static constexpr uint32_t kLength = 0x1FFFFFFF;

    // Nothing for a file that is not one. A file cut short gives the blocks
    // that are whole.
    static std::optional<Tape> parseCdt(std::span<const uint8_t> file);
    // A recording: a WAV file (PCM of 8 bits or more, any rate, any number
    // of channels) or a Creative Voice file. The signal changes level each
    // time the sound crosses its middle.
    static std::optional<Tape> parseWav(std::span<const uint8_t> file);
    static std::optional<Tape> parseVoc(std::span<const uint8_t> file);
    // Whichever of the three the file is.
    static std::optional<Tape> parse(std::span<const uint8_t> file);

    const std::vector<Block>& blocks() const { return blocks_; }
    const std::vector<uint32_t>& pulses() const { return pulses_; }

private:
    std::vector<Block> blocks_;
    std::vector<uint32_t> pulses_;

    static Tape fromSamples(const std::vector<int16_t>& samples, double rate);
};

// The cassette deck: a tape, the Play key, and the motor, which the CPC
// switches through a relay (PPI port C, bit 4). The tape moves while Play
// is down and the motor runs; what passes the head is bit 7 of PPI port B.
//
// Times are the machine's clock, in microseconds. Nothing is done between
// two calls: the tape is moved on when it is next looked at.
class TapeDeck {
public:
    void insert(Tape tape);
    void eject();
    bool loaded() const { return loaded_; }
    const Tape& tape() const { return tape_; }

    void play(uint64_t now);
    void stop(uint64_t now);
    bool playing() const { return playing_; }
    void rewind(uint64_t now) { seekBlock(0, now); }
    void seekBlock(size_t block, uint64_t now);
    // The block under the head: the one last gone to, or the one the pulse
    // now playing belongs to.
    size_t block() const { return block_; }
    bool atEnd() const { return loaded_ && position_ == tape_.pulses().size() && remaining_ == 0; }

    void setMotor(bool on, uint64_t now);
    bool motor() const { return motor_; }
    bool level(uint64_t now);

private:
    Tape tape_;
    bool loaded_ = false;
    bool playing_ = false;
    bool motor_ = false;
    bool level_ = false;
    size_t position_ = 0;     // the next pulse to start
    size_t block_ = 0;
    uint64_t remaining_ = 0;  // what is left of the one under way, in half T-states
    uint64_t time_ = 0;       // when the tape was last moved on

    void advance(uint64_t now);
};

}  // namespace tuxape
