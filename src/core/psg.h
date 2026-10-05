#pragma once

#include <cstdint>

namespace tuxape {

// General Instrument AY-3-8912 programmable sound generator. Its I/O port A
// is wired to the keyboard matrix.
class Psg {
public:
    Psg() { reset(); }
    void reset();

    // Bus operations, selected by the BDIR and BC1 lines.
    void selectRegister(uint8_t value);
    void write(uint8_t value);
    // A register set without going through the chip's bus, as the Plus's
    // sound channels fed from memory do; the register selected stays.
    void setRegister(int number, uint8_t value);
    // `portA` is what the keyboard puts on the I/O port.
    uint8_t read(uint8_t portA) const;

    // Advances the sound generators by one step. The chip divides its 1 MHz
    // clock by 8, so a step is 8 microseconds.
    void tick();

    // What channel A, B or C is putting out right now, as a step of the
    // chip's 16-level logarithmic DAC (0 is silence).
    uint8_t level(int channel) const
    {
        const bool tone = toneOutput_[channel] || (reg_[7] & (1 << channel));
        const bool noise = (noiseShift_ & 1) || (reg_[7] & (8 << channel));
        if (!(tone && noise))
            return 0;
        const uint8_t volume = reg_[8 + channel];
        return (volume & 0x10) ? envelopeLevel_ : volume & 0x0F;
    }

    // Output voltage of a DAC step, from 0.0 to 1.0.
    static float amplitude(uint8_t level);

    uint8_t reg(int n) const { return reg_[n & 15]; }
    uint8_t selected() const { return selected_; }

private:
    uint8_t reg_[16] = {};
    uint8_t selected_ = 0;
    bool selectValid_ = true;  // the chip ignores register numbers above 15

    uint16_t toneCounter_[3] = {};
    bool toneOutput_[3] = {};
    uint8_t noiseCounter_ = 0;
    uint32_t noiseShift_ = 1;  // 17-bit shift register
    uint32_t envelopeCounter_ = 0;
    uint8_t envelopeStep_ = 0;   // position in the current 16-step ramp
    uint8_t envelopeLevel_ = 0;
    bool envelopeRising_ = false;
    bool envelopeHeld_ = false;

    void restartEnvelope();
    void stepEnvelope();
};

}  // namespace tuxape
