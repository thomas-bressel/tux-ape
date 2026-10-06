// The cassette: CDT files turned into pulses, the deck with its Play key
// and motor, and a program loaded from tape by the real firmware.
//
// The last part needs the ROM images; without them the test still runs the
// rest and exits with code 77 (skipped).

#include <string>
#include <vector>

#include <cstdlib>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/screen_text.h"
#include "core/setup.h"
#include "core/tape.h"

namespace {

using namespace tuxape;
using Bytes = std::vector<uint8_t>;

Bytes cdt(const Bytes& blocks)
{
    Bytes file = {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20};
    file.insert(file.end(), blocks.begin(), blocks.end());
    return file;
}

void put(Bytes& out, std::initializer_list<unsigned> words)
{
    for (const unsigned word : words) {
        out.push_back(static_cast<uint8_t>(word));
        out.push_back(static_cast<uint8_t>(word >> 8));
    }
}

// A turbo block: leader, two sync pulses, then the bytes.
Bytes turbo(const Bytes& data, unsigned pilot, unsigned pilotCount, unsigned sync, unsigned zero, unsigned one,
            unsigned pauseMs, unsigned usedBits = 8)
{
    Bytes block = {0x11};
    put(block, {pilot, sync, sync, zero, one, pilotCount});
    block.push_back(static_cast<uint8_t>(usedBits));
    put(block, {pauseMs});
    block.push_back(static_cast<uint8_t>(data.size()));
    block.push_back(static_cast<uint8_t>(data.size() >> 8));
    block.push_back(static_cast<uint8_t>(data.size() >> 16));
    block.insert(block.end(), data.begin(), data.end());
    return block;
}

void append(Bytes& to, const Bytes& more)
{
    to.insert(to.end(), more.begin(), more.end());
}

void testBlocks()
{
    CHECK(!Tape::parseCdt(Bytes{}).has_value());
    CHECK(!Tape::parseCdt(Bytes{'Z', 'X', 'T', 'a', 'p', 'e', '?', 0x1A, 1, 20, 0x22}).has_value());
    CHECK(!Tape::parseCdt(cdt({})).has_value());

    Bytes blocks = {0x30, 5, 'h', 'e', 'l', 'l', 'o'};       // a description
    append(blocks, {0x21, 4, 'P', 'a', 'r', 't'});           // a group
    append(blocks, turbo({0xA5, 0x80}, 1000, 3, 500, 100, 200, 2, 1));
    append(blocks, {0x22});                                  // the end of the group
    append(blocks, {0x12, 0x2C, 0x01, 0x04, 0x00});          // a tone: 4 pulses of 300
    append(blocks, {0x13, 2, 0x0A, 0x00, 0x14, 0x00});       // pulses of 10 and 20
    append(blocks, {0x14, 50, 0, 60, 0, 8, 0, 0, 1, 0, 0, 0xC0});  // pure data
    append(blocks, {0x24, 3, 0, 0x12, 7, 0, 2, 0, 0x25});    // a loop of 3: 2 pulses of 7
    append(blocks, {0x20, 0, 0});                            // stop the tape
    append(blocks, {0x15, 100, 0, 0, 0, 4, 2, 0, 0, 0xF0, 0xC0});  // samples: 11110000 1100
    append(blocks, {0x20, 0xE8, 0x03});                      // a second of silence
    append(blocks, {0x77, 2, 0, 0, 0, 1, 2});                // a block not known, with its length
    append(blocks, {0x11, 1, 2, 3});                         // cut short
    const auto tape = Tape::parseCdt(cdt(blocks));
    CHECK(tape.has_value());
    if (!tape)
        return;
    std::string names;
    for (const Tape::Block& block : tape->blocks())
        names += block.name + "|";
    CHECK(names == "Description|Group: Part|Turbo Data|End of Group|Pure Tone|Sequence of Pulses|Pure Data|"
                   "Loop Start 3|Pure Tone|Loop End|Pause|Direct Recording|Pause|Unknown: 119|");

    const uint32_t low = Tape::kLow, high = Tape::kHigh;
    const std::vector<uint32_t> want = {
        // Turbo: leader, sync, A5 = 10100101, the one bit used of the last byte, the pause.
        1000, 1000, 1000, 500, 500,
        200, 200, 100, 100, 200, 200, 100, 100, 100, 100, 200, 200, 100, 100, 200, 200,
        200, 200,
        3500, low | 3500,
        300, 300, 300, 300,
        10, 20,
        // Pure data: C0 = 11000000.
        60, 60, 60, 60, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50,
        7, 7, 7, 7, 7, 7,
        Tape::kStop | low,
        high | 400, low | 400, high | 200, low | 200,
        3500, low | 999 * 3500,
    };
    CHECK(tape->pulses() == want);
    if (tape->pulses() != want)
        for (size_t i = 0; i < tape->pulses().size() && i < want.size(); ++i)
            if (tape->pulses()[i] != want[i]) {
                std::printf("pulse %zu is %08X, want %08X\n", i, tape->pulses()[i], want[i]);
                break;
            }
    CHECK_EQ(tape->blocks()[2].firstPulse, 0);
    CHECK_EQ(tape->blocks()[4].firstPulse, 25);
}

// The deck: the tape moves while Play is down and the motor runs.
void testDeck()
{
    // Pulses of 35, 70 and 105 T-states: 10, 20 and 30 microseconds.
    Bytes blocks = {0x13, 3, 35, 0, 70, 0, 105, 0};
    append(blocks, {0x20, 0, 0});
    append(blocks, {0x12, 35, 0, 2, 0});
    const auto tape = Tape::parseCdt(cdt(blocks));
    CHECK(tape.has_value());
    if (!tape)
        return;

    TapeDeck deck;
    CHECK(!deck.loaded() && !deck.playing());
    CHECK(!deck.level(1000));
    deck.play(1000);
    CHECK(!deck.playing());  // nothing to play

    deck.insert(*tape);
    CHECK(deck.loaded() && !deck.playing() && !deck.atEnd());
    // Not without Play, and not without the motor.
    deck.setMotor(true, 2000);
    CHECK(!deck.level(5000));
    deck.setMotor(false, 5000);
    deck.play(5000);
    CHECK(deck.playing());
    CHECK(!deck.level(9000));
    deck.setMotor(true, 10000);
    CHECK(deck.motor());
    CHECK(deck.level(10000));   // the first pulse starts high
    CHECK(deck.level(10009));
    CHECK(!deck.level(10010));  // 10 microseconds on, the second
    CHECK(!deck.level(10029));
    CHECK(deck.level(10030));
    CHECK_EQ(deck.block(), 0);
    // The motor stopped holds the tape where it is.
    deck.setMotor(false, 10040);
    CHECK(deck.level(20000));
    deck.setMotor(true, 20000);
    CHECK(deck.level(20019));
    // The stop block lets go of Play, with the level low.
    CHECK(!deck.level(20020));
    CHECK(!deck.playing());
    CHECK(!deck.level(30000));
    CHECK_EQ(deck.block(), 1);
    deck.play(30000);
    CHECK(deck.level(30000));
    CHECK(!deck.level(30010));
    CHECK_EQ(deck.block(), 2);
    CHECK(deck.playing());
    // The end of the tape lets go of it too.
    CHECK(!deck.level(30020));
    CHECK(!deck.playing() && deck.atEnd());
    deck.play(30030);
    CHECK(!deck.playing());

    // Rewound, or put at a block, it plays from there.
    deck.rewind(40000);
    CHECK(!deck.atEnd());
    CHECK_EQ(deck.block(), 0);
    deck.play(40000);
    CHECK(deck.level(40005));
    deck.seekBlock(2, 40005);
    CHECK_EQ(deck.block(), 2);
    CHECK(deck.playing());
    CHECK(deck.level(40005));
    CHECK(!deck.level(40015));
    // A clock that starts again, as the machine's does, is no trouble.
    deck.rewind(50);
    deck.play(50);
    CHECK(deck.level(55));
    deck.eject();
    CHECK(!deck.loaded() && !deck.playing());
    CHECK(!deck.level(60));
}

// ---- A file as the CPC's firmware writes it ---------------------------------

uint16_t crc(const uint8_t* data, size_t size)
{
    uint16_t value = 0xFFFF;
    for (size_t i = 0; i < size; ++i) {
        value = static_cast<uint16_t>(value ^ data[i] << 8);
        for (int bit = 0; bit < 8; ++bit)
            value = static_cast<uint16_t>(value & 0x8000 ? value << 1 ^ 0x1021 : value << 1);
    }
    return static_cast<uint16_t>(~value);
}

// One record: a leader of one bits, a zero bit, the sync byte, the data in
// segments of 256 bytes each followed by its check, and a trailer.
Bytes record(uint8_t sync, Bytes data, unsigned halfZero)
{
    data.resize((data.size() + 255) / 256 * 256);
    Bytes bytes = {sync};
    for (size_t at = 0; at < data.size(); at += 256) {
        bytes.insert(bytes.end(), data.begin() + static_cast<ptrdiff_t>(at), data.begin() + static_cast<ptrdiff_t>(at) + 256);
        const uint16_t check = crc(&data[at], 256);
        bytes.push_back(static_cast<uint8_t>(check >> 8));
        bytes.push_back(static_cast<uint8_t>(check));
    }
    bytes.insert(bytes.end(), 4, 0xFF);
    return turbo(bytes, halfZero * 2, 4096, halfZero, halfZero, halfZero * 2, 1000);
}

// A binary file of one block: its header record, then its data. The tape
// starts with a silence, as tapes do: the firmware lets the motor run for
// two and a half seconds before it listens.
Bytes binaryFile(const char* name, const Bytes& code, uint16_t address, unsigned halfZero)
{
    Bytes header(64, 0);
    for (size_t i = 0; name[i] && i < 16; ++i)
        header[i] = static_cast<uint8_t>(name[i]);
    header[16] = 1;     // block 1
    header[17] = 0xFF;  // the last
    header[18] = 0x02;  // binary
    header[19] = header[24] = static_cast<uint8_t>(code.size());
    header[20] = header[25] = static_cast<uint8_t>(code.size() >> 8);
    header[21] = header[26] = static_cast<uint8_t>(address);
    header[22] = header[27] = static_cast<uint8_t>(address >> 8);
    header[23] = 0xFF;  // the first
    Bytes blocks = {0x20, 0xB8, 0x0B};  // three seconds
    append(blocks, record(0x2C, header, halfZero));
    append(blocks, record(0x16, code, halfZero));
    return blocks;
}

// The firmware loads and runs a program from the tape, at the speed given
// (half the length of a zero bit, in T-states of the file).
// A tape's pulses as the samples of a recording of it.
std::vector<int16_t> recorded(const Tape& tape, double rate)
{
    std::vector<int16_t> samples;
    bool high = false;
    double end = 0;  // in T-states
    for (const uint32_t pulse : tape.pulses()) {
        high = (pulse & Tape::kHigh) ? true : (pulse & Tape::kLow) ? false : !high;
        end += pulse & Tape::kLength;
        while (static_cast<double>(samples.size()) < end * rate / 3500000.0)
            samples.push_back(high ? 12000 : -12000);
    }
    return samples;
}

// The samples as a WAV file, of 8 or 16 bits, the same on every channel.
Bytes wav(const std::vector<int16_t>& samples, unsigned rate, unsigned bits, unsigned channels, unsigned format = 1)
{
    const unsigned frame = bits / 8 * channels;
    const unsigned size = static_cast<unsigned>(samples.size()) * frame;
    Bytes file = {'R', 'I', 'F', 'F'};
    put(file, {(36 + size) & 0xFFFF, (36 + size) >> 16});
    append(file, {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(file, {16, 0, format, channels, rate & 0xFFFF, rate >> 16, (rate * frame) & 0xFFFF, (rate * frame) >> 16, frame, bits});
    append(file, {'d', 'a', 't', 'a'});
    put(file, {size & 0xFFFF, size >> 16});
    for (const int16_t sample : samples) {
        for (unsigned channel = 0; channel < channels; ++channel) {
            if (bits == 8)
                file.push_back(static_cast<uint8_t>(sample / 256 + 128));
            else
                put(file, {static_cast<uint16_t>(sample)});
        }
    }
    return file;
}

// And as a Creative Voice file: the old 8-bit block, with its rate as a
// "time constant", or the newer one with 16 bits.
Bytes voc(const std::vector<int16_t>& samples, unsigned rate, bool sixteen)
{
    Bytes file;
    for (const char c : std::string("Creative Voice File\x1A"))
        file.push_back(static_cast<uint8_t>(c));
    put(file, {26, 0x010A, 0x1129});
    const unsigned size = static_cast<unsigned>(samples.size()) * (sixteen ? 2 : 1) + (sixteen ? 12 : 2);
    append(file, {static_cast<uint8_t>(sixteen ? 9 : 1), static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8),
                  static_cast<uint8_t>(size >> 16)});
    if (sixteen) {
        put(file, {rate & 0xFFFF, rate >> 16});
        append(file, {16, 1, 4, 0, 0, 0, 0, 0});
    } else {
        append(file, {static_cast<uint8_t>(256 - 1000000 / rate), 0});
    }
    for (const int16_t sample : samples) {
        if (sixteen)
            put(file, {static_cast<uint16_t>(sample)});
        else
            file.push_back(static_cast<uint8_t>(sample / 256 + 128));
    }
    file.push_back(0);
    return file;
}

// Recordings of a tape: a WAV or a VOC file gives back the tape's pulses,
// to within a sample.
void testRecordings()
{
    // A tone of 20 pulses of 1000 T-states, then 10 of 2000.
    Bytes blocks = {0x12};
    put(blocks, {1000, 20});
    append(blocks, {0x12});
    put(blocks, {2000, 10});
    const auto tape = Tape::parseCdt(cdt(blocks));
    CHECK(tape.has_value());
    if (!tape)
        return;
    const auto same = [&](const std::optional<Tape>& heard, double rate) {
        CHECK(heard.has_value());
        if (!heard)
            return;
        CHECK_EQ(heard->blocks().size(), 1);
        CHECK(!heard->blocks().empty() && heard->blocks()[0].name == "Direct Recording");
        const double sample = 3500000.0 / rate;
        CHECK_EQ(heard->pulses().size(), 30);
        for (size_t i = 0; i < heard->pulses().size() && i < 30; ++i) {
            const double length = heard->pulses()[i] & Tape::kLength, want = i < 20 ? 1000 : 2000;
            CHECK(std::abs(length - want) <= sample + 1);
            // Each says which level it has: high first, as on the tape.
            CHECK(((heard->pulses()[i] & Tape::kHigh) != 0) == (i % 2 == 0));
        }
    };
    const std::vector<int16_t> samples = recorded(*tape, 44100);
    same(Tape::parseWav(wav(samples, 44100, 16, 2)), 44100);
    same(Tape::parseWav(wav(samples, 44100, 8, 1)), 44100);
    same(Tape::parse(wav(recorded(*tape, 22050), 22050, 16, 1)), 22050);
    same(Tape::parseVoc(voc(samples, 44100, true)), 44100);
    same(Tape::parse(voc(recorded(*tape, 40000), 40000, false)), 40000);
    // Whichever kind a file is; and what is none.
    CHECK(Tape::parse(cdt(blocks)).has_value() && Tape::parse(cdt(blocks))->blocks().size() == 2);
    CHECK(!Tape::parseWav(cdt(blocks)).has_value() && !Tape::parseVoc(cdt(blocks)).has_value());
    CHECK(!Tape::parseWav(wav(samples, 44100, 32, 1, 3)).has_value());  // floating point: not read
    CHECK(!Tape::parseWav(wav({}, 44100, 16, 1)).has_value());
    CHECK(!Tape::parse(Bytes{'R', 'I', 'F', 'F'}).has_value());
    CHECK(!Tape::parse(Bytes(100, 0)).has_value());
}

// Loads a program through the firmware, from a CDT file's blocks or from a
// recording of them.
bool loadsFromTape(unsigned halfZero, bool asRecording = false)
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr))
        return false;
    AutoType keys(cpc.keyboard());
    auto frames = [&](int count) {
        for (int i = 0; i < count; ++i) {
            keys.frame();
            cpc.runFrame();
        }
    };
    auto waitFor = [&](const char* text, int seconds) {
        for (int i = 0; i < seconds * 50; ++i) {
            frames(1);
            if (readScreenText(cpc).find(text) != std::string::npos)
                return true;
        }
        std::printf("no \"%s\" on the screen:\n%s\n", text, readScreenText(cpc).c_str());
        return false;
    };

    // Prints its message through TXT OUTPUT and stays there.
    const Bytes code = {0x21, 0x0E, 0x80, 0x7E, 0xB7, 0x28, 0xFE, 0xCD, 0x5A, 0xBB, 0x23, 0x18, 0xF6, 0x00,
                        'T',  'A',  'P',  'E',  ' ',  'O',  'K',  0};
    auto tape = Tape::parseCdt(cdt(binaryFile("TEST", code, 0x8000, halfZero)));
    CHECK(tape.has_value());
    if (!tape)
        return true;
    if (asRecording) {
        tape = Tape::parse(wav(recorded(*tape, 44100), 44100, 16, 1));
        CHECK(tape.has_value());
        if (!tape)
            return true;
    }
    cpc.tape().insert(*tape);
    cpc.tape().play(cpc.microseconds());

    frames(150);
    keys.type("|TAPE\nRUN\"\n");
    CHECK(waitFor("Press PLAY then any key:", 20));
    CHECK(!cpc.tape().motor());
    keys.type(" ");
    CHECK(waitFor("Loading TEST block 1", 30));
    CHECK(cpc.tape().motor());
    CHECK(waitFor("TAPE OK", 60));
    frames(50);
    CHECK(!cpc.tape().motor());  // the firmware stops it when it has its file
    return true;
}

// Record: what the firmware saves goes on a new tape, which makes a CDT
// file of Turbo Data blocks that the firmware loads again.
bool savesToTape(bool fast)
{
    const auto machine = [](Cpc& cpc) { return setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr); };
    Bytes file;
    {
        Cpc cpc;
        if (!machine(cpc))
            return false;
        AutoType keys(cpc.keyboard());
        auto frames = [&](int count) {
            for (int i = 0; i < count; ++i) {
                keys.frame();
                cpc.runFrame();
            }
        };
        auto waitFor = [&](const char* text, int seconds) {
            for (int i = 0; i < seconds * 50; ++i) {
                frames(1);
                if (readScreenText(cpc).find(text) != std::string::npos)
                    return true;
            }
            std::printf("no \"%s\" on the screen:\n%s\n", text, readScreenText(cpc).c_str());
            return false;
        };
        frames(150);
        keys.type(fast ? "|TAPE\nSPEED WRITE 1\n" : "|TAPE\n");
        keys.type("10 PRINT \"KEPT \";3*14\nSAVE \"TEST\"\n");
        CHECK(waitFor("Press REC and PLAY then any key:", 30));
        CHECK(!cpc.tape().recording());
        cpc.tape().record(cpc.microseconds());
        CHECK(cpc.tape().recording() && !cpc.tape().playing());
        // The tape stands still until the motor runs.
        frames(25);
        CHECK_EQ(cpc.tape().recordedTime(cpc.microseconds()), 0);
        keys.type(" ");
        CHECK(waitFor("Saving TEST block 1", 30));
        CHECK(cpc.tape().motor());
        for (int i = 0; i < 60 * 50 && cpc.tape().motor(); ++i)
            frames(1);
        CHECK(!cpc.tape().motor());
        frames(25);
        const uint64_t length = cpc.tape().recordedTime(cpc.microseconds());
        CHECK(length > 3000000 && length < 30000000);
        cpc.tape().stop(cpc.microseconds());
        CHECK(!cpc.tape().recording());
        const std::vector<uint32_t> recorded = cpc.tape().takeRecording();
        CHECK(cpc.tape().takeRecording().empty());
        uint64_t sum = 0;
        for (uint32_t pulse : recorded)
            sum += pulse;
        CHECK_EQ(sum, length);
        file = Tape::cdtFromRecording(recorded);
        CHECK(!file.empty());
    }
    const auto tape = Tape::parse(file);
    CHECK(tape.has_value());
    if (!tape)
        return true;
    // The file's header and the file, each a block of the firmware's.
    int turbo = 0;
    for (const Tape::Block& block : tape->blocks()) {
        CHECK(block.id == 0x11 || block.id == 0x20);
        turbo += block.id == 0x11;
    }
    CHECK_EQ(turbo, 2);
    // Small: the bits, not their pulses.
    CHECK(file.size() < 1200);

    Cpc cpc;
    machine(cpc);
    AutoType keys(cpc.keyboard());
    auto frames = [&](int count) {
        for (int i = 0; i < count; ++i) {
            keys.frame();
            cpc.runFrame();
        }
    };
    cpc.tape().insert(*tape);
    cpc.tape().play(cpc.microseconds());
    frames(150);
    keys.type("|TAPE\nRUN\"\n");
    bool kept = false;
    for (int i = 0; i < 60 * 50 && !kept; ++i) {
        frames(1);
        if (i == 100)
            keys.type(" ");
        kept = readScreenText(cpc).find("KEPT  42") != std::string::npos;
    }
    if (!kept)
        std::printf("the saved program did not load:\n%s\n", readScreenText(cpc).c_str());
    CHECK(kept);
    return true;
}

// What is not a block of that kind is kept pulse by pulse.
void testRecordingFile()
{
    CHECK(Tape::cdtFromRecording({}).empty());
    CHECK(Tape::cdtFromRecording(std::vector<uint32_t>{1000000}).empty());
    CHECK(Tape::cdtFromRecording(std::vector<uint32_t>{1000000, 2000000}).empty());
    // Two seconds of tape, five pulses, 20 ms, three more, and a second.
    const std::vector<uint32_t> recorded = {2000000, 100, 200, 300, 400, 500, 20000, 1000, 2000, 3000, 1000000};
    const Bytes file = Tape::cdtFromRecording(recorded);
    const auto tape = Tape::parseCdt(file);
    CHECK(tape.has_value());
    if (!tape)
        return;
    std::vector<uint8_t> ids;
    for (const Tape::Block& block : tape->blocks())
        ids.push_back(block.id);
    CHECK(ids == (std::vector<uint8_t>{0x20, 0x13, 0x20, 0x13, 0x20}));
    const std::vector<uint32_t>& pulses = tape->pulses();
    const size_t first = tape->blocks()[1].firstPulse, second = tape->blocks()[3].firstPulse;
    CHECK_EQ(tape->blocks()[2].firstPulse - first, 5);
    CHECK_EQ(tape->blocks()[4].firstPulse - second, 3);
    for (size_t i = 0; i < 5 && first + i < pulses.size(); ++i)
        CHECK_EQ(pulses[first + i] & Tape::kLength, (i + 1) * 350);
    for (size_t i = 0; i < 3 && second + i < pulses.size(); ++i)
        CHECK_EQ(pulses[second + i] & Tape::kLength, (i + 1) * 3500);

    // A deck: the tape moves with the motor only.
    TapeDeck deck;
    deck.write(true, 10);
    deck.record(1000);
    deck.write(false, 1500);  // standing still
    deck.setMotor(true, 2000);
    deck.write(true, 2300);
    deck.write(true, 2350);  // no change
    deck.write(false, 2700);
    deck.setMotor(false, 3000);
    deck.write(true, 3500);
    deck.setMotor(true, 4000);
    deck.write(false, 4100);
    CHECK_EQ(deck.recordedTime(4200), 1200);
    deck.stop(4250);
    CHECK_EQ(deck.recordedTime(9000), 1250);
    CHECK(deck.takeRecording() == (std::vector<uint32_t>{300, 400, 400, 150}));
}

}  // namespace

// "Tape Loading Sounds": the tape's signal reaches the sound output while
// a program reads it, and only when asked for.
void testSound()
{
    // A long tone: pulses of 1750 T-states, half a millisecond each.
    Bytes blocks = {0x12};
    put(blocks, {1750, 4000});
    const auto tape = Tape::parseCdt(cdt(blocks));
    CHECK(tape.has_value());
    if (!tape)
        return;
    // How much the sound moves while a program polls the tape for a
    // twentieth of a second: the sum of the steps between samples.
    const auto heard = [&](bool sound, bool play) {
        Cpc cpc;
        cpc.out(0x7F00, 0x8C);
        // 8000 LD B,#F5 / 8002 IN A,(C) / 8004 JR 8002
        const uint8_t program[] = {0x06, 0xF5, 0xED, 0x78, 0x18, 0xFC};
        for (size_t i = 0; i < sizeof program; ++i)
            cpc.memory().write(static_cast<uint16_t>(0x8000 + i), program[i]);
        cpc.cpu().pc = 0x8000;
        cpc.cpu().iff1 = cpc.cpu().iff2 = false;
        cpc.setTapeSound(sound);
        CHECK(cpc.tapeSound() == sound);
        cpc.audio().setSampleRate(44100);
        cpc.tape().insert(*tape);
        if (play)
            cpc.tape().play(cpc.microseconds());
        cpc.out(0xF700, 0x82);  // the PPI's port C as outputs...
        cpc.out(0xF600, 0x10);  // ...and the tape's motor on
        cpc.run(50000);
        const std::vector<int16_t>& samples = cpc.audio().samples();
        long moves = 0;
        for (size_t i = 2; i < samples.size(); i += 2)
            moves += std::abs(samples[i] - samples[i - 2]);
        CHECK(samples.size() > 4000);
        return moves;
    };
    const long silent = heard(false, true);
    const long loud = heard(true, true);
    const long stopped = heard(true, false);
    CHECK_EQ(silent, 0);
    CHECK_EQ(stopped, 0);
    // A square wave of 1 kHz: a hundred edges in the twentieth of a second.
    CHECK(loud > 100 * 2000);
}

int main()
{
    testBlocks();
    testDeck();
    testSound();
    testRecordings();
    // The firmware's two speeds: 1000 baud and 2000 baud; and a recording
    // of the first, as a WAV file.
    testRecordingFile();
    const bool roms = loadsFromTape(1167) && loadsFromTape(583) && loadsFromTape(1167, true) && savesToTape(false) &&
                      savesToTape(true);
    if (!roms)
        std::printf("ROM images not found; loading through the firmware was not tested\n");
    const int result = checkSummary("tape");
    return result != 0 ? result : roms ? 0 : 77;
}
