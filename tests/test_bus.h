#pragma once

#include <cstdint>
#include <cstring>

// Plain 64K machine for exercising the Z80 core on its own: flat RAM, no
// wait states, and a T-state counter.
struct TestBus {
    uint8_t ram[0x10000];
    uint64_t tstates = 0;
    bool irqLine = false;
    uint8_t irqData = 0xFF;
    uint8_t portIn = 0xFF;

    TestBus() { std::memset(ram, 0, sizeof ram); }

    uint8_t m1(uint16_t addr)
    {
        tstates += 4;
        return ram[addr];
    }
    uint8_t read(uint16_t addr)
    {
        tstates += 3;
        return ram[addr];
    }
    void write(uint16_t addr, uint8_t v)
    {
        tstates += 3;
        ram[addr] = v;
    }
    uint8_t in(uint16_t)
    {
        tstates += 4;
        return portIn;
    }
    void out(uint16_t, uint8_t) { tstates += 4; }
    void tick(int n) { tstates += n; }
    bool irq() const { return irqLine; }
    uint8_t irqAck()
    {
        tstates += 6;
        return irqData;
    }
};
