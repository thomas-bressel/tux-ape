#pragma once

#include <cstdint>

namespace tuxape {

// Intel 8255 peripheral interface, mode 0 only (all the CPC uses).
//
// On the CPC: port A is the PSG data bus, port B reads status lines (VSYNC,
// printer busy, cassette input, option links), port C drives the keyboard
// line select, the cassette, and the PSG bus control.
class Ppi {
public:
    void reset()
    {
        control_ = 0x9B;  // every port an input
        latch_[0] = latch_[1] = latch_[2] = 0;
    }

    // `port` is 0-2 for A-C, 3 for the control register.
    void write(int port, uint8_t value)
    {
        if (port < 3) {
            latch_[port] = value;
        } else if (value & 0x80) {
            // Setting the mode clears all output latches.
            control_ = value;
            latch_[0] = latch_[1] = latch_[2] = 0;
        } else {
            // Set or reset a single bit of port C.
            const uint8_t bit = static_cast<uint8_t>(1 << ((value >> 1) & 7));
            if (value & 1)
                latch_[2] |= bit;
            else
                latch_[2] &= ~bit;
        }
    }

    // `pins` are the levels the outside world puts on the port; they are
    // seen only where the port is an input.
    uint8_t read(int port, uint8_t pins) const
    {
        switch (port) {
        case 0: return aIsInput() ? pins : latch_[0];
        case 1: return bIsInput() || plus_ ? pins : latch_[1];
        case 2: {
            uint8_t v = latch_[2];
            if (plus_)
                return v;
            if (control_ & 0x08)
                v = static_cast<uint8_t>((v & 0x0F) | (pins & 0xF0));
            if (control_ & 0x01)
                v = static_cast<uint8_t>((v & 0xF0) | (pins & 0x0F));
            return v;
        }
        // The control register cannot be read back. On the Plus what is
        // read there is port A's pins while port A is an input (`pins` are
        // then port A's), and zero while it is an output.
        default: return !plus_ ? 0xFF : aIsInput() ? pins : 0x00;
        }
    }

    // The Plus has no 8255: its ASIC stands in for one, and not in every
    // respect. Whatever the control register says, port B is an input and
    // port C an output; and the control register reads port A's pins.
    // (Kevin Thacker's "asicppi" test, run on real machines.)
    void setPlus(bool plus) { plus_ = plus; }

    bool aIsInput() const { return control_ & 0x10; }
    bool bIsInput() const { return control_ & 0x02; }

    // Levels on the pins. A port set as input does not drive them, and the
    // pull-ups make them read high.
    uint8_t outputA() const { return aIsInput() ? 0xFF : latch_[0]; }
    uint8_t outputC() const
    {
        uint8_t v = latch_[2];
        if (plus_)
            return v;
        if (control_ & 0x08)
            v |= 0xF0;
        if (control_ & 0x01)
            v |= 0x0F;
        return v;
    }

    uint8_t control() const { return control_; }
    uint8_t latch(int port) const { return latch_[port]; }

private:
    uint8_t control_ = 0x9B;
    uint8_t latch_[3] = {};
    bool plus_ = false;
};

}  // namespace tuxape
