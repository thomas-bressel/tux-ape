#include "core/psg.h"

namespace tuxape {

namespace {

// Bits that exist in each register; the others read back as 0.
constexpr uint8_t kMask[16] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
    0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF,
};

// Measured output of the AY-3-8910 family's DAC for each of its 16 steps.
// Each step is roughly 3 dB above the one before.
constexpr float kDac[16] = {
    0.0000f, 0.0100f, 0.0145f, 0.0211f, 0.0307f, 0.0455f, 0.0645f, 0.1074f,
    0.1266f, 0.2050f, 0.2922f, 0.3728f, 0.4925f, 0.6353f, 0.8056f, 1.0000f,
};

// Envelope shape bits in register 13.
enum : uint8_t { kHold = 1, kAlternate = 2, kAttack = 4, kContinue = 8 };

}  // namespace

float Psg::amplitude(uint8_t level)
{
    return kDac[level & 15];
}

void Psg::reset()
{
    for (uint8_t& r : reg_)
        r = 0;
    selected_ = 0;
    selectValid_ = true;
    for (int ch = 0; ch < 3; ++ch) {
        toneCounter_[ch] = 0;
        toneOutput_[ch] = false;
    }
    noiseCounter_ = 0;
    noiseShift_ = 1;
    restartEnvelope();
}

void Psg::selectRegister(uint8_t value)
{
    selectValid_ = value < 16;
    if (selectValid_)
        selected_ = value;
}

void Psg::write(uint8_t value)
{
    if (!selectValid_)
        return;
    reg_[selected_] = value & kMask[selected_];
    // Writing the shape register starts the envelope again, even with the
    // same shape.
    if (selected_ == 13) {
        restartEnvelope();
        envelopeWritten_ = true;
    }
}

void Psg::setRegister(int number, uint8_t value)
{
    number &= 15;
    reg_[number] = value & kMask[number];
    if (number == 13) {
        restartEnvelope();
        envelopeWritten_ = true;
    }
}

uint8_t Psg::read(uint8_t portA) const
{
    if (!selectValid_)
        return 0xFF;
    if (selected_ == 14) {
        // As an input the port shows the pins; as an output the pins are the
        // wired AND of the latch and whatever drives them.
        return (reg_[7] & 0x40) ? reg_[14] & portA : portA;
    }
    if (selected_ == 15) {
        // The 8912 has no port B pins: as an input it reads as all ones.
        return (reg_[7] & 0x80) ? reg_[15] : 0xFF;
    }
    return reg_[selected_];
}

void Psg::tick()
{
    // Tone: the output flips each time the counter reaches the period, which
    // gives a square wave of 1 MHz / (16 * period). A period of 0 acts as 1.
    for (int ch = 0; ch < 3; ++ch) {
        const uint16_t period = static_cast<uint16_t>(reg_[ch * 2] | reg_[ch * 2 + 1] << 8);
        if (++toneCounter_[ch] >= (period ? period : 1)) {
            toneCounter_[ch] = 0;
            toneOutput_[ch] = !toneOutput_[ch];
        }
    }

    // Noise: a 17-bit shift register clocked at half the tone rate.
    const uint8_t noisePeriod = reg_[6] ? reg_[6] : 1;
    if (++noiseCounter_ >= noisePeriod * 2) {
        noiseCounter_ = 0;
        const uint32_t bit = (noiseShift_ ^ (noiseShift_ >> 3)) & 1;
        noiseShift_ = noiseShift_ >> 1 | bit << 16;
    }

    // Envelope: 16 steps per ramp, a ramp lasting 256 clock cycles per unit
    // of the period, so one step every 2 ticks per unit.
    const uint32_t envelopePeriod = static_cast<uint32_t>(reg_[11] | reg_[12] << 8);
    if (++envelopeCounter_ >= (envelopePeriod ? envelopePeriod : 1) * 2) {
        envelopeCounter_ = 0;
        stepEnvelope();
    }
}

void Psg::restartEnvelope()
{
    envelopeCounter_ = 0;
    envelopeStep_ = 0;
    envelopeHeld_ = false;
    envelopeRising_ = reg_[13] & kAttack;
    envelopeLevel_ = envelopeRising_ ? 0 : 15;
}

void Psg::stepEnvelope()
{
    if (envelopeHeld_)
        return;
    const uint8_t shape = reg_[13];
    if (++envelopeStep_ == 16) {
        if (!(shape & kContinue)) {
            // One ramp, then silence.
            envelopeHeld_ = true;
            envelopeLevel_ = 0;
            return;
        }
        if (shape & kHold) {
            // One ramp, then stay at its last level, or at the opposite one.
            envelopeHeld_ = true;
            const bool high = (shape & kAlternate) ? !envelopeRising_ : envelopeRising_;
            envelopeLevel_ = high ? 15 : 0;
            return;
        }
        if (shape & kAlternate)
            envelopeRising_ = !envelopeRising_;
        envelopeStep_ = 0;
    }
    envelopeLevel_ = envelopeRising_ ? envelopeStep_ : 15 - envelopeStep_;
}

}  // namespace tuxape
