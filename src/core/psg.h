#pragma once

#include <cstdint>

namespace tuxape {

// General Instrument AY-3-8912 programmable sound generator. Its I/O port A
// is wired to the keyboard matrix.
class Psg {
public:
    void reset();

    // Bus operations, selected by the BDIR and BC1 lines.
    void selectRegister(uint8_t value);
    void write(uint8_t value);
    // `portA` is what the keyboard puts on the I/O port.
    uint8_t read(uint8_t portA) const;

    uint8_t reg(int n) const { return reg_[n & 15]; }
    uint8_t selected() const { return selected_; }

private:
    uint8_t reg_[16] = {};
    uint8_t selected_ = 0;
    bool selectValid_ = true;  // the chip ignores register numbers above 15
};

}  // namespace tuxape
