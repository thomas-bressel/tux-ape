// The cassette: CDT files turned into pulses, the deck with its Play key
// and motor, and a program loaded from tape by the real firmware.
//
// The last part needs the ROM images; without them the test still runs the
// rest and exits with code 77 (skipped).

#include <string>
#include <vector>

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
bool loadsFromTape(unsigned halfZero)
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
    const auto tape = Tape::parseCdt(cdt(binaryFile("TEST", code, 0x8000, halfZero)));
    CHECK(tape.has_value());
    if (!tape)
        return true;
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

}  // namespace

int main()
{
    testBlocks();
    testDeck();
    // The firmware's two speeds: 1000 baud and 2000 baud.
    const bool roms = loadsFromTape(1167) && loadsFromTape(583);
    if (!roms)
        std::printf("ROM images not found; loading through the firmware was not tested\n");
    const int result = checkSummary("tape");
    return result != 0 ? result : roms ? 0 : 77;
}
