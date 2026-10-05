#pragma once

#include <cstdint>

#include "core/crtc.h"
#include "core/gate_array.h"
#include "core/keyboard.h"
#include "core/memory.h"
#include "core/monitor.h"
#include "core/ppi.h"
#include "core/psg.h"
#include "core/z80.h"

namespace tuxape {

// A complete Amstrad CPC: the Z80 and every chip around it, wired together.
//
// Time is counted in Z80 T-states (4 MHz). The Gate Array only lets the Z80
// proceed on one T-state in four, so every machine cycle is stretched until
// it lines up with the video hardware; that stretching happens here, in the
// bus functions the CPU core calls.
class Cpc {
public:
    // The firmware's standard frame: 312 lines of 64 microseconds.
    static constexpr uint32_t kFrameMicroseconds = 312 * 64;

    Cpc();

    // The reset button. RAM contents survive.
    void reset();
    // Power off and on again: reset, with RAM cleared.
    void coldReset();

    // Runs the machine for a length of emulated time. The last instruction
    // may run over; the excess is taken off the next call.
    void run(uint32_t microseconds);
    void runFrame() { run(kFrameMicroseconds); }

    // T-states since power on.
    uint64_t clock() const { return clk_; }

    Z80<Cpc>& cpu() { return cpu_; }
    Memory& memory() { return memory_; }
    Crtc& crtc() { return crtc_; }
    GateArray& gateArray() { return gateArray_; }
    Monitor& monitor() { return monitor_; }
    Ppi& ppi() { return ppi_; }
    Psg& psg() { return psg_; }
    Keyboard& keyboard() { return keyboard_; }

    // ---- Z80 bus (see z80.h) -----------------------------------------------

    uint8_t m1(uint16_t addr)
    {
        advance(1);
        waitForGateArray();
        advance(1);
        const uint8_t v = memory_.read(addr);
        advance(2);
        return v;
    }

    uint8_t read(uint16_t addr)
    {
        advance(1);
        waitForGateArray();
        advance(1);
        const uint8_t v = memory_.read(addr);
        advance(1);
        return v;
    }

    void write(uint16_t addr, uint8_t value)
    {
        advance(1);
        waitForGateArray();
        advance(1);
        memory_.write(addr, value);
        advance(1);
    }

    uint8_t in(uint16_t port)
    {
        advance(2);
        waitForGateArray();
        advance(1);
        const uint8_t v = ioRead(port);
        advance(1);
        return v;
    }

    void out(uint16_t port, uint8_t value)
    {
        advance(2);
        waitForGateArray();
        advance(1);
        ioWrite(port, value);
        advance(1);
    }

    void tick(int tstates) { advance(tstates); }

    bool irq() const { return gateArray_.interruptRequested(); }

    uint8_t irqAck()
    {
        advance(3);
        waitForGateArray();
        advance(1);
        gateArray_.acknowledgeInterrupt();
        advance(2);
        return 0xFF;  // nothing drives the data bus
    }

private:
    // T-state, within each microsecond, on which the Gate Array releases
    // the WAIT line.
    static constexpr unsigned kReadyPhase = 1;

    Z80<Cpc> cpu_;
    Memory memory_;
    Crtc crtc_;
    GateArray gateArray_;
    Monitor monitor_;
    Ppi ppi_;
    Psg psg_;
    Keyboard keyboard_;

    uint64_t clk_ = 0;
    uint64_t videoClk_ = 0;  // start of the next microsecond of video to draw
    uint64_t runUntil_ = 0;

    void advance(unsigned tstates)
    {
        clk_ += tstates;
        while (videoClk_ + 4 <= clk_) {
            crtc_.tick();
            gateArray_.tick(crtc_, memory_.baseRam(), monitor_);
            videoClk_ += 4;
        }
    }

    // The Z80 samples WAIT in the middle of a machine cycle and idles until
    // the Gate Array lets it through.
    void waitForGateArray() { advance((kReadyPhase - static_cast<unsigned>(clk_)) & 3); }

    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t value);
    uint8_t portB() const;
    void updatePsgBus();
};

}  // namespace tuxape
