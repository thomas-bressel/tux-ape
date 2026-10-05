#include "core/psg.h"

namespace tuxape {

namespace {

// Bits that exist in each register; the others read back as 0.
constexpr uint8_t kMask[16] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
    0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF,
};

}  // namespace

void Psg::reset()
{
    for (uint8_t& r : reg_)
        r = 0;
    selected_ = 0;
    selectValid_ = true;
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
    if (selected_ == 15)
        return 0xFF;  // the 8912 has no port B pins
    return reg_[selected_];
}

}  // namespace tuxape
