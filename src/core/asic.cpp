#include "core/asic.h"

#include <algorithm>

#include "core/crtc.h"
#include "core/gate_array.h"

namespace tuxape {

namespace {

// The lock's sequence, which a byte that is not zero comes before. Its last
// byte decides: with it, whatever byte comes next unlocks the ASIC; anything
// else in its place locks it. (Amstrad's own description has a seventeenth
// byte, &EE; real machines take any, as Kevin Thacker's "asiclock" test
// shows.)
constexpr uint8_t kKey[] = {0x00, 0xFF, 0x77, 0xB3, 0x51, 0xA8, 0xD4, 0x62, 0x39, 0x9C, 0x46, 0x2B, 0x15, 0x8A, 0xCD};
constexpr int kLast = sizeof kKey - 1;

// Addresses within the page.
constexpr int kSprites = 0x0000;     // &4000: sixteen sprites of 256 pixels
constexpr int kAttributes = 0x2000;  // &6000: eight bytes to a sprite
constexpr int kPalette = 0x2400;     // &6400: 32 colours of two bytes
constexpr int kRaster = 0x2800;      // &6800: PRI, SPLT, SSA, SSCR, IVR
constexpr int kAnalogue = 0x2808;    // &6808: eight analogue inputs
constexpr int kDma = 0x2C00;         // &6C00: the sound channels' registers
constexpr uint8_t kUnused = 0xB0;    // what a place with nothing behind it reads

}  // namespace

Asic::Asic()
{
    reset();
}

void Asic::reset()
{
    unlocked_ = false;
    sequenceAt_ = 0;
    previous_ = 0;
    rmr2_ = 0;
    page_.fill(kUnused);
    std::fill_n(page_.begin() + kSprites, 0x1000, 0);
    std::fill_n(page_.begin() + kAttributes, 0x80, 0);
    std::fill_n(page_.begin() + kPalette, 0x40, 0);
    // Nothing is plugged into the analogue ports.
    static constexpr uint8_t kInputs[8] = {0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x00, 0x3F, 0x00};
    std::copy_n(kInputs, 8, page_.begin() + kAnalogue);
    palette_.fill(0);
    sprites_.fill(Sprite());
    spritesShown_ = false;
    pri_ = splt_ = sscr_ = 0;
    // The vector register starts with every bit set: the raster's
    // interrupt then puts FE on the bus (what Kevin Thacker's "pritest"
    // notes it saw on a real machine before any write to the register),
    // and a program left in interrupt mode 0, as the Z80 is after a reset,
    // runs a harmless CP n where a CPC would run RST &38. With 0 it ran
    // LD B,n: Pro Tennis Tour then waited for the frame flyback on the
    // wrong port, for ever.
    ivr_ = 0xFF;
    ssa_ = 0;
    dcsr_ = 0;
    channels_.fill(Channel());
    showDcsr();
    if (gateArray_)
        for (int index = 0; index < 32; ++index)
            gateArray_->setPlusColour(index, 0);
    if (crtc_)
        crtc_->setSplit(0, 0);
}

void Asic::raiseRasterInterrupt()
{
    // Nothing shows in the status register yet: its bit 7 tells which
    // interrupt the Z80 took last (see acknowledgeInterrupt).
}

void Asic::raiseChannelInterrupt(int channel)
{
    dcsr_ |= static_cast<uint8_t>(0x40 >> channel);
    showDcsr();
}

// An instruction is a word. Its top four bits say what it is:
//   0RDD  put DD in register R of the sound chip
//   1NNN  the next instruction comes NNN units later, a unit being as many
//         lines as the prescaler says, and one more
//   2NNN  the instructions from here are to be gone through NNN times
//   4xxx  bit 0: go back to the last 2NNN while its count lasts;
//         bit 4: ask for an interrupt; bit 5: stop the channel
void Asic::soundTick()
{
    if (!readRam_)
        return;
    for (int number = 0; number < 3; ++number) {
        if (!(dcsr_ & 1 << number))
            continue;
        Channel& channel = channels_[static_cast<size_t>(number)];
        if (channel.pause > 0) {
            if (channel.pauseLines > 0) {
                --channel.pauseLines;
            } else {
                channel.pauseLines = channel.prescaler;
                --channel.pause;
            }
            continue;
        }
        const uint16_t instruction =
            static_cast<uint16_t>(readRam_(channel.address) | readRam_(static_cast<uint16_t>(channel.address + 1)) << 8);
        channel.address = static_cast<uint16_t>(channel.address + 2);
        if ((instruction & 0x7000) == 0) {
            if (writeSound_)
                writeSound_(instruction >> 8 & 0x0F, static_cast<uint8_t>(instruction));
            continue;
        }
        if ((instruction & 0x1000) && (instruction & 0x0FFF) != 0) {
            // The line of the instruction itself is the pause's first.
            channel.pause = instruction & 0x0FFF;
            channel.pauseLines = channel.prescaler;
            if (channel.pauseLines > 0) {
                --channel.pauseLines;
            } else {
                channel.pauseLines = channel.prescaler;
                --channel.pause;
            }
        }
        if (instruction & 0x2000) {
            channel.repeats = instruction & 0x0FFF;
            channel.loopStart = channel.address;
        }
        if (instruction & 0x4000) {
            // The count is of times through, the first included.
            if ((instruction & 0x01) && channel.repeats > 0 && --channel.repeats > 0)
                channel.address = channel.loopStart;
            if (instruction & 0x10)
                raiseChannelInterrupt(number);
            if (instruction & 0x20) {
                dcsr_ &= static_cast<uint8_t>(~(1 << number));
                showDcsr();
            }
        }
    }
}

uint8_t Asic::acknowledgeInterrupt(bool raster)
{
    // The raster's interrupt comes first, then the channels' from 0 to 2;
    // which one it is shows in bits 2 and 1 of the vector, bit 0 being
    // always 0.
    //
    // Bit 7 of the status register says whether the interrupt just taken
    // was the raster's, and goes on saying so until the next one is taken:
    // a handler reads it to know what it is there for (Copter 271 and No
    // Exit do, and never drew a picture while the bit was dropped here),
    // and Kevin Thacker's "pritest" expects &80 once the handler is back.
    uint8_t vector = ivr_ & 0xF8;
    if (raster) {
        vector |= 0x06;
        dcsr_ |= 0x80;
    } else if (dcsr_ & 0x70) {
        dcsr_ &= 0x7F;
        const int channel = dcsr_ & 0x40 ? 0 : dcsr_ & 0x20 ? 1 : 2;
        vector |= static_cast<uint8_t>((2 - channel) << 1);
        // Taken, a channel's interrupt goes by itself. With bit 0 of the
        // vector register set it stays until its flag is written to;
        // channel 2's still goes. (Amstrad's description has the bit the
        // other way round, and says nothing of channel 2; this is what
        // Kevin Thacker's "dmatest" finds on a real machine.)
        if (!(ivr_ & 1) || channel == 2)
            dcsr_ &= static_cast<uint8_t>(~(0x40 >> channel));
    }
    showDcsr();
    return vector;
}

void Asic::sequence(uint8_t value)
{
    // A byte that is not zero, then a zero, start the sequence. More zeros
    // after that one change nothing: Switchblade sends FF, 00, 00, FF, 77...
    // (and, a slip in its loop, 238 bytes of its own code after the key).
    if (value == 0 && (previous_ != 0 || sequenceAt_ == 1))
        sequenceAt_ = 0;
    previous_ = value;
    if (sequenceAt_ < 0)
        return;  // out, until the sequence is started again
    if (sequenceAt_ <= kLast) {
        const bool match = value == kKey[sequenceAt_];
        if (sequenceAt_ == kLast && !match)
            unlocked_ = false;
        sequenceAt_ = match ? sequenceAt_ + 1 : -1;
        return;
    }
    unlocked_ = true;
    sequenceAt_ = -1;
}

void Asic::setColour(int index, uint16_t grb)
{
    grb &= 0x0FFF;
    palette_[static_cast<size_t>(index)] = grb;
    page_[static_cast<size_t>(kPalette + index * 2)] = static_cast<uint8_t>(grb);
    page_[static_cast<size_t>(kPalette + index * 2 + 1)] = static_cast<uint8_t>(grb >> 8);
    if (gateArray_)
        gateArray_->setPlusColour(index, grb);
}

void Asic::setHardwareColour(int pen, int hardwareColour)
{
    setColour(pen & 31, GateArray::plusColour(hardwareColour));
}

void Asic::showDcsr()
{
    // The status reads the same all over the block.
    std::fill_n(page_.begin() + kDma, 16, dcsr_);
}

// The chunk, as the snapshot format (version 3) lays it out:
//   &000  the sprites' pixels, two to a byte
//   &800  eight bytes to a sprite: X, Y, magnification
//   &880  the palette
//   &8C0  PRI, SPLT, SSA (high, low), SSCR, IVR, a spare byte
//   &8C7  the eight analogue inputs
//   &8CF  four bytes to a sound channel: address, prescaler
//   &8DE  the status register
//   &8DF  seven bytes to a channel: loop count, loop start, what is left
//         of a pause, and of its unit
//   &8F4  RMR2; whether the ASIC is unlocked; where its lock's sequence is
std::vector<uint8_t> Asic::snapshot() const
{
    std::vector<uint8_t> chunk(kSnapshotSize, 0);
    for (size_t i = 0; i < 0x800; ++i)
        chunk[i] = static_cast<uint8_t>(page_[i * 2] << 4 | page_[i * 2 + 1]);
    auto magnification = [](uint8_t width) { return width == 0 ? 0 : width == 1 ? 1 : width == 2 ? 2 : 3; };
    for (size_t n = 0; n < 16; ++n) {
        const Sprite& sprite = sprites_[n];
        uint8_t* at = &chunk[0x800 + n * 8];
        at[0] = static_cast<uint8_t>(sprite.x);
        at[1] = static_cast<uint8_t>(sprite.x >> 8);
        at[2] = static_cast<uint8_t>(sprite.y);
        at[3] = static_cast<uint8_t>(sprite.y >> 8);
        at[4] = static_cast<uint8_t>(magnification(sprite.magX) << 2 | magnification(sprite.magY));
    }
    for (size_t n = 0; n < 32; ++n) {
        chunk[0x880 + n * 2] = static_cast<uint8_t>(palette_[n]);
        chunk[0x881 + n * 2] = static_cast<uint8_t>(palette_[n] >> 8);
    }
    chunk[0x8C0] = pri_;
    chunk[0x8C1] = splt_;
    chunk[0x8C2] = static_cast<uint8_t>(ssa_ >> 8);
    chunk[0x8C3] = static_cast<uint8_t>(ssa_);
    chunk[0x8C4] = sscr_;
    chunk[0x8C5] = ivr_;
    std::copy_n(page_.begin() + kAnalogue, 8, chunk.begin() + 0x8C7);
    for (size_t n = 0; n < 3; ++n) {
        const Channel& channel = channels_[n];
        chunk[0x8CF + n * 4] = static_cast<uint8_t>(channel.address);
        chunk[0x8D0 + n * 4] = static_cast<uint8_t>(channel.address >> 8);
        chunk[0x8D1 + n * 4] = channel.prescaler;
        uint8_t* at = &chunk[0x8DF + n * 7];
        at[0] = static_cast<uint8_t>(channel.repeats);
        at[1] = static_cast<uint8_t>(channel.repeats >> 8);
        at[2] = static_cast<uint8_t>(channel.loopStart);
        at[3] = static_cast<uint8_t>(channel.loopStart >> 8);
        at[4] = static_cast<uint8_t>(channel.pause);
        at[5] = static_cast<uint8_t>(channel.pause >> 8);
        at[6] = channel.pauseLines;
    }
    chunk[0x8DE] = dcsr_;
    chunk[0x8F4] = static_cast<uint8_t>(0xA0 | rmr2_);
    chunk[0x8F5] = unlocked_ ? 1 : 0;
    chunk[0x8F6] = static_cast<uint8_t>(sequenceAt_ < 0 ? 0 : sequenceAt_);
    return chunk;
}

void Asic::restore(std::span<const uint8_t> chunk)
{
    if (chunk.size() < kSnapshotSize)
        return;
    for (size_t i = 0; i < 0x800; ++i) {
        page_[i * 2] = chunk[i] >> 4;
        page_[i * 2 + 1] = chunk[i] & 0x0F;
    }
    for (int n = 0; n < 16; ++n) {
        const uint16_t at = static_cast<uint16_t>(kAttributes + n * 8);
        for (int i = 0; i < 4; ++i)
            write(static_cast<uint16_t>(at + i), chunk[static_cast<size_t>(0x800 + n * 8 + i)]);
        write(static_cast<uint16_t>(at + 4), chunk[static_cast<size_t>(0x800 + n * 8 + 4)]);
    }
    for (int n = 0; n < 32; ++n)
        setColour(n, static_cast<uint16_t>(chunk[static_cast<size_t>(0x880 + n * 2)]
                                           | chunk[static_cast<size_t>(0x881 + n * 2)] << 8));
    write(kRaster, chunk[0x8C0]);
    write(kRaster + 1, chunk[0x8C1]);
    write(kRaster + 2, chunk[0x8C2]);
    write(kRaster + 3, chunk[0x8C3]);
    write(kRaster + 4, chunk[0x8C4]);
    write(kRaster + 5, chunk[0x8C5]);
    for (size_t n = 0; n < 3; ++n) {
        Channel& channel = channels_[n];
        channel.address = static_cast<uint16_t>(chunk[0x8CF + n * 4] | chunk[0x8D0 + n * 4] << 8);
        channel.prescaler = chunk[0x8D1 + n * 4];
        const uint8_t* at = &chunk[0x8DF + n * 7];
        channel.repeats = static_cast<uint16_t>((at[0] | at[1] << 8) & 0x0FFF);
        channel.loopStart = static_cast<uint16_t>(at[2] | at[3] << 8);
        channel.pause = static_cast<uint16_t>((at[4] | at[5] << 8) & 0x0FFF);
        channel.pauseLines = at[6];
    }
    dcsr_ = chunk[0x8DE];
    showDcsr();
    rmr2_ = chunk[0x8F4] & 0x1F;
    unlocked_ = chunk[0x8F5] & 1;
    sequenceAt_ = -1;
    previous_ = 0;
}

void Asic::write(uint16_t address, uint8_t value)
{
    const int at = address & (kPageSize - 1);
    if (at < 0x1000) {
        page_[static_cast<size_t>(at)] = value & 0x0F;
        return;
    }
    if (at >= kAttributes && at < kAttributes + 0x80) {
        Sprite& sprite = sprites_[static_cast<size_t>((at - kAttributes) >> 3)];
        const int base = at & ~7;
        if (at & 4) {
            // Magnification: 0 hides the sprite, 1 to 3 give pixels of
            // 1, 2 and 4.
            sprite.magX = static_cast<uint8_t>((value >> 2 & 3) ? 1 << ((value >> 2 & 3) - 1) : 0);
            sprite.magY = static_cast<uint8_t>((value & 3) ? 1 << ((value & 3) - 1) : 0);
            spritesShown_ = std::any_of(sprites_.begin(), sprites_.end(),
                                        [](const Sprite& s) { return s.magX != 0 && s.magY != 0; });
            return;
        }
        // The position. X has ten bits, of which the top quarter stands
        // for -256 to -1; Y has nine, signed. Read back, a negative
        // position has all the bits above set.
        switch (at & 3) {
        case 0: sprite.x = static_cast<int16_t>((sprite.x & 0xFF00) | value); break;
        case 1: sprite.x = static_cast<int16_t>((sprite.x & 0x00FF) | ((value & 3) == 3 ? 0xFF00 : (value & 3) << 8)); break;
        case 2: sprite.y = static_cast<int16_t>((sprite.y & 0xFF00) | value); break;
        case 3: sprite.y = static_cast<int16_t>((sprite.y & 0x00FF) | ((value & 1) ? 0xFF00 : 0)); break;
        }
        // The four bytes of the position read the same four bytes on.
        for (int mirror = 0; mirror < 8; mirror += 4) {
            page_[static_cast<size_t>(base + mirror)] = static_cast<uint8_t>(sprite.x);
            page_[static_cast<size_t>(base + mirror + 1)] = static_cast<uint8_t>(sprite.x >> 8);
            page_[static_cast<size_t>(base + mirror + 2)] = static_cast<uint8_t>(sprite.y);
            page_[static_cast<size_t>(base + mirror + 3)] = static_cast<uint8_t>(sprite.y >> 8);
        }
        return;
    }
    if (at >= kPalette && at < kPalette + 0x40) {
        const int index = (at - kPalette) >> 1;
        const uint16_t now = palette_[static_cast<size_t>(index)];
        setColour(index, (at & 1) ? static_cast<uint16_t>((now & 0x00FF) | (value & 0x0F) << 8)
                                  : static_cast<uint16_t>((now & 0x0F00) | value));
        return;
    }
    switch (at) {
    case kRaster: pri_ = value; return;
    case kRaster + 1:
    case kRaster + 2:
    case kRaster + 3:
        if (at == kRaster + 1)
            splt_ = value;
        else if (at == kRaster + 2)
            ssa_ = static_cast<uint16_t>((ssa_ & 0x00FF) | (value & 0x3F) << 8);
        else
            ssa_ = static_cast<uint16_t>((ssa_ & 0xFF00) | value);
        if (crtc_)
            crtc_->setSplit(splt_, ssa_);
        return;
    case kRaster + 4: sscr_ = value; return;
    case kRaster + 5: ivr_ = value; return;
    // The channels' registers: the list's address (even), then the
    // prescaler.
    case kDma: case kDma + 4: case kDma + 8:
        channels_[static_cast<size_t>((at - kDma) >> 2)].address =
            static_cast<uint16_t>((channels_[static_cast<size_t>((at - kDma) >> 2)].address & 0xFF00) | (value & 0xFE));
        return;
    case kDma + 1: case kDma + 5: case kDma + 9:
        channels_[static_cast<size_t>((at - kDma) >> 2)].address =
            static_cast<uint16_t>((channels_[static_cast<size_t>((at - kDma) >> 2)].address & 0x00FF) | value << 8);
        return;
    // Written during a pause, the prescaler ends the unit under way there
    // and then: the rest of the pause is in units of the new length.
    case kDma + 2: case kDma + 6: case kDma + 10: {
        Channel& channel = channels_[static_cast<size_t>((at - kDma) >> 2)];
        channel.prescaler = value;
        channel.pauseLines = value;
        if (channel.pause > 0)
            --channel.pause;
        return;
    }
    case kDma + 15:
        // The channels' enables; a one written over an interrupt flag
        // takes the flag down.
        dcsr_ = static_cast<uint8_t>((dcsr_ & 0xF0 & ~(value & 0x70)) | (value & 0x07));
        showDcsr();
        return;
    default: return;
    }
}

}  // namespace tuxape
