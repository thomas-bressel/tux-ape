#include "core/cpc.h"

namespace tuxape {

Cpc::Cpc()
    : cpu_(*this)
{
    reset();
}

void Cpc::reset()
{
    cpu_.reset();
    memory_.reset();
    crtc_.reset();
    gateArray_.reset();
    ppi_.reset();
    psg_.reset();
}

void Cpc::coldReset()
{
    memory_.clearRam();
    cpu_.powerOn();
    monitor_.reset();
    reset();
}

void Cpc::run(uint32_t microseconds)
{
    if (runUntil_ < clk_)
        runUntil_ = clk_;
    runUntil_ += static_cast<uint64_t>(microseconds) * 4;
    while (clk_ < runUntil_)
        cpu_.step();
}

// The CPC decodes I/O addresses one line at a time: each device answers when
// "its" address bit is low, whatever the other bits are. Several devices can
// therefore be addressed by the same access.

void Cpc::ioWrite(uint16_t port, uint8_t value)
{
    const uint8_t high = static_cast<uint8_t>(port >> 8);

    if (!(port & 0x8000)) {
        if ((value & 0xC0) == 0xC0) {
            memory_.selectRamBank(value, high);
        } else if (port & 0x4000) {
            gateArray_.write(value);
            memory_.setRomEnables(gateArray_.lowerRomEnabled(), gateArray_.upperRomEnabled());
        }
    }
    if (!(port & 0x4000)) {
        if ((high & 3) == 0)
            crtc_.select(value);
        else if ((high & 3) == 1)
            crtc_.write(value);
    }
    if (!(port & 0x2000))
        memory_.selectUpperRom(value);
    if (!(port & 0x0800)) {
        ppi_.write(high & 3, value);
        updatePsgBus();
    }
}

uint8_t Cpc::ioRead(uint16_t port)
{
    const uint8_t high = static_cast<uint8_t>(port >> 8);
    uint8_t value = 0xFF;

    if (!(port & 0x4000)) {
        if ((high & 3) == 2)
            value &= crtc_.readStatus();
        else if ((high & 3) == 3)
            value &= crtc_.readData();
    }
    if (!(port & 0x0800)) {
        uint8_t pins = 0xFF;
        switch (high & 3) {
        case 0:
            // The PSG drives the bus only while BC1 is high and BDIR low.
            if ((ppi_.outputC() >> 6) == 1)
                pins = psg_.read(keyboard_.line(ppi_.outputC() & 0x0F));
            break;
        case 1:
            pins = portB();
            break;
        }
        value &= ppi_.read(high & 3, pins);
    }
    return value;
}

uint8_t Cpc::portB() const
{
    // Bit 7: cassette data in. Bit 6: printer busy (nothing is connected).
    // Bit 5: /EXP. Bit 4: 50 Hz link. Bits 3-1: maker links, 111 = Amstrad.
    uint8_t value = 0x7E;
    if (crtc_.vsync())
        value |= 0x01;
    return value;
}

void Cpc::updatePsgBus()
{
    // Port C bits 7-6 are the PSG's BDIR and BC1; port A is its data bus.
    switch (ppi_.outputC() >> 6) {
    case 3: psg_.selectRegister(ppi_.outputA()); break;
    case 2: psg_.write(ppi_.outputA()); break;
    }
}

}  // namespace tuxape
