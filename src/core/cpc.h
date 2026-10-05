#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/audio.h"
#include "core/crtc.h"
#include "core/fdc.h"
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

    // Calls `hook` just before the instruction at a watched address runs.
    // Tools use this to observe a program, for instance to collect what it
    // prints by watching the firmware's character output routine.
    using ExecHook = std::function<void(uint16_t pc)>;
    void setExecHook(ExecHook hook) { execHook_ = std::move(hook); }
    void watchAddress(uint16_t addr, bool watch = true);

    // "SSM" codes: a program can signal the emulator by executing two
    // do-nothing opcodes in a row, ED ll ED hh (the convention comes from the
    // Logon System Shaker tests). `hook` receives hh * 256 + ll.
    using SsmHook = std::function<void(uint16_t code)>;
    void setSsmHook(SsmHook hook) { ssmHook_ = std::move(hook); }

    // T-states since power on.
    uint64_t clock() const { return clk_; }
    // The same in microseconds, which is what the slower peripherals count in.
    uint64_t microseconds() const { return clk_ >> 2; }

    Z80<Cpc>& cpu() { return cpu_; }
    Memory& memory() { return memory_; }
    Crtc& crtc() { return crtc_; }
    GateArray& gateArray() { return gateArray_; }
    Monitor& monitor() { return monitor_; }
    Ppi& ppi() { return ppi_; }
    Psg& psg() { return psg_; }
    Keyboard& keyboard() { return keyboard_; }
    Fdc& fdc() { return fdc_; }
    // Sound output. Set its sample rate to start receiving samples.
    AudioMixer& audio() { return audio_; }

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
        // The devices see the write as soon as the I/O cycle is under way,
        // before the wait: with OUT (C),r that is the instruction's third
        // microsecond. The CRTC inside an ASIC (types 3 and 4) samples the
        // bus later and catches it on the fourth. (Compendium, 4.4.3.)
        advance(2);
        const bool lateCrtc = crtc_.type() == CrtcType::AsicPlus || crtc_.type() == CrtcType::PreAsic;
        ioWrite(port, value);
        if (!lateCrtc)
            crtcWrite(port, value);
        waitForGateArray();
        advance(1);
        if (lateCrtc)
            crtcWrite(port, value);
        advance(1);
    }

    void tick(int tstates) { advance(tstates); }

    // The Z80 looks at INT as its last T-state begins, so a request raised
    // just as an instruction ends is only seen after the next one.
    bool irq() const { return gateArray_.interruptRequested() && interruptRaisedAt_ + 1 < clk_; }

    void unusedEd(uint8_t op)
    {
        if (ssmHook_)
            ssmOpcode(op);
    }

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
    Fdc fdc_;
    AudioMixer audio_;

    uint64_t clk_ = 0;
    uint64_t soundClk_ = 0;  // microsecond the sound generators have reached
    uint64_t videoClk_ = 0;  // start of the microsecond of video in progress
    uint64_t interruptRaisedAt_ = 0;
    uint64_t runUntil_ = 0;

    SsmHook ssmHook_;
    int ssmLow_ = -1;        // first half of a code, or -1
    uint16_t ssmNextPc_ = 0;  // where the second half must sit

    ExecHook execHook_;
    std::vector<uint8_t> watched_;  // one flag per address; empty when nothing is watched
    int watchedCount_ = 0;

    void advance(unsigned tstates)
    {
        clk_ += tstates;
        while (videoClk_ + 4 <= clk_) {
            // The microsecond that has just ended is drawn last, so that it
            // shows everything the CPU did during it...
            gateArray_.render(crtc_, memory_.baseRam(), monitor_);
            videoClk_ += 4;
            // ...and the CRTC moves on at once: what the CPU reads during
            // the new microsecond (VSYNC through the PPI, say) is current.
            crtc_.tick();
            const bool pending = gateArray_.interruptRequested();
            gateArray_.sync(crtc_, monitor_);
            if (!pending && gateArray_.interruptRequested())
                interruptRaisedAt_ = videoClk_;
        }
    }

    // The Z80 samples WAIT in the middle of a machine cycle and idles until
    // the Gate Array lets it through.
    void waitForGateArray() { advance((kReadyPhase - static_cast<unsigned>(clk_)) & 3); }

    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t value);  // every device but the CRTC
    void crtcWrite(uint16_t port, uint8_t value);
    uint8_t portB() const;
    void ssmOpcode(uint8_t op);
    void updatePsgBus();
    void syncSound();
};

}  // namespace tuxape
