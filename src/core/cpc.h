#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/asic.h"
#include "core/audio.h"
#include "core/cartridge.h"
#include "core/crtc.h"
#include "core/fdc.h"
#include "core/gate_array.h"
#include "core/keyboard.h"
#include "core/memory.h"
#include "core/monitor.h"
#include "core/ppi.h"
#include "core/psg.h"
#include "core/tape.h"
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

    // Brings the machine's clocks into step with one another and forgets
    // any time owed, so that what it does from here depends on its state
    // alone: a recorded session starts with this, when recorded and when
    // played back.
    void alignClocks();

    // For a debugger. One instruction, or one turn of one that repeats
    // (LDIR): a single step. And, for the hook below to stop the machine
    // on a breakpoint, an end to run() before the instruction it was
    // called for; run() then returns early.
    void stepInstruction();
    void stopRun() { stopRun_ = true; }
    // ED FF, which no program uses, as a breakpoint written into the
    // program: run() ends after it, and breakInstructionHit() says so once.
    void setBreakInstructions(bool on) { breakInstructions_ = on; }
    bool breakInstructionHit()
    {
        const bool hit = breakInstructionHit_;
        breakInstructionHit_ = false;
        return hit;
    }

    // Calls `hook` just before the instruction at a watched address runs.
    // Tools use this to observe a program, for instance to collect what it
    // prints by watching the firmware's character output routine.
    using ExecHook = std::function<void(uint16_t pc)>;
    void setExecHook(ExecHook hook) { execHook_ = std::move(hook); }
    void watchAddress(uint16_t addr, bool watch = true);

    // For breakpoints on memory and on input and output. `hook` is called
    // when the program reads or writes a byte at a watched address (not
    // when it fetches an instruction there): a write's hook comes before
    // the byte is stored, with the one it replaces. The other is called at
    // every input and output, with the value that went through.
    using MemoryHook = std::function<void(uint16_t addr, uint8_t value, uint8_t previous, bool write)>;
    void setMemoryHook(MemoryHook hook) { memoryHook_ = std::move(hook); }
    void watchMemory(uint16_t addr, bool reads, bool writes);
    void clearMemoryWatches();
    using IoHook = std::function<void(uint16_t port, uint8_t value, bool write)>;
    void setIoHook(IoHook hook) { ioHook_ = std::move(hook); }

    // "SSM" codes: a program can signal the emulator by executing two
    // do-nothing opcodes in a row, ED ll ED hh (the convention comes from the
    // Logon System Shaker tests). `hook` receives hh * 256 + ll.
    using SsmHook = std::function<void(uint16_t code)>;
    void setSsmHook(SsmHook hook) { ssmHook_ = std::move(hook); }

    // T-states since power on.
    uint64_t clock() const { return clk_; }
    // The same in microseconds, which is what the slower peripherals count in.
    uint64_t microseconds() const { return clk_ >> 2; }
    // The microsecond the instruction about to run starts in, once the
    // Gate Array has let its first cycle through: between two
    // instructions, the difference is the time the program took, to the
    // microsecond.
    uint64_t instructionTime() const { return (clk_ + 7 - ((kReadyPhase + 3) & 3)) >> 2; }

    Z80<Cpc>& cpu() { return cpu_; }
    Memory& memory() { return memory_; }
    Crtc& crtc() { return crtc_; }
    GateArray& gateArray() { return gateArray_; }
    Monitor& monitor() { return monitor_; }
    Ppi& ppi() { return ppi_; }
    Psg& psg() { return psg_; }
    Keyboard& keyboard() { return keyboard_; }
    Fdc& fdc() { return fdc_; }
    TapeDeck& tape() { return tape_; }

    // A Plus machine: a cartridge in place of the ROMs, and the ASIC with
    // what it adds. `discRom` is for a machine with a disc drive, where ROM
    // 7 is the cartridge's AMSDOS. Without a cartridge the machine is a CPC
    // again. Neither resets the machine.
    void setCartridge(const Cartridge* cartridge, bool discRom = true);
    bool plus() const { return plus_; }
    Asic& asic() { return asic_; }
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
        if (memoryWatched_) [[unlikely]]
            memoryAccess(addr, v, v, false);
        advance(1);
        return v;
    }

    void write(uint16_t addr, uint8_t value)
    {
        advance(1);
        waitForGateArray();
        advance(1);
        if (memoryWatched_) [[unlikely]]
            memoryAccess(addr, value, memory_.readRam(addr), true);
        memory_.write(addr, value);
        advance(1);
    }

    uint8_t in(uint16_t port)
    {
        advance(2);
        waitForGateArray();
        advance(1);
        const uint8_t v = ioRead(port);
        // The Gate Array inside the ASIC does not look at which way the
        // data goes: read from, it takes what is left on the bus, the last
        // byte of the instruction, as if it were written.
        if (plus_ && (port & 0xC000) == 0x4000)
            gateArrayWrite(memory_.read(static_cast<uint16_t>(cpu_.pc - 1)), static_cast<uint8_t>(port >> 8));
        if (ioHook_) [[unlikely]]
            ioHook_(port, v, false);
        advance(1);
        return v;
    }

    void out(uint16_t port, uint8_t value)
    {
        // The devices see the write as soon as the I/O cycle is under way,
        // before the wait: with OUT (C),r that is the instruction's third
        // microsecond. The CRTC inside an ASIC (types 3 and 4) samples the
        // bus later and catches it on the fourth. (Compendium, 4.4.3.)
        //
        // Within that microsecond the write does not always come at the
        // same place. An I/O cycle that starts a T-state before the
        // microsecond (OUTI, OUT (n),A) needs no extra wait and reaches the
        // CRTC a quarter of a microsecond sooner than one that starts with
        // it (OUT (C),r): soon enough to have a say in what the chip does
        // with its HSYNC on that character (Compendium 14.5.4).
        const bool early = (clk_ & 3) == 3;
        advance(2);
        const bool lateCrtc = crtc_.type() == CrtcType::AsicPlus || crtc_.type() == CrtcType::PreAsic;
        ioWrite(port, value);
        if (!lateCrtc)
            crtcWrite(port, value, early);
        waitForGateArray();
        advance(1);
        if (lateCrtc)
            crtcWrite(port, value, early);
        if (ioHook_) [[unlikely]]
            ioHook_(port, value, true);
        advance(1);
    }

    void tick(int tstates) { advance(tstates); }

    // The Gate Array's clock runs half a microsecond ahead of the place
    // where this model moves the picture on, and the Z80 looks at INT as its
    // last T-state begins. Put together, an instruction ending one T-state
    // short of a microsecond already sees the interrupt that the end of an
    // HSYNC raises there (Compendium 27.7.2, the two ADD HL,DE in a row).
    bool irq()
    {
        runVideo(clk_ + 1);
        return gateArray_.interruptRequested() || (plus_ && asic_.interruptPending());
    }

    void unusedEd(uint8_t op)
    {
        if (ssmHook_)
            ssmOpcode(op);
        if (breakInstructions_ && op == 0xFF)
            stopRun_ = breakInstructionHit_ = true;
    }

    uint8_t irqAck()
    {
        advance(3);
        waitForGateArray();
        advance(1);
        // The acknowledge takes bit 5 off the line counter. An HSYNC ending
        // within the half microsecond that follows has, for the Gate Array,
        // already ended and been counted (Compendium 27.7.1; which comes
        // first is what the Shaker's "killer" test measures, instruction by
        // instruction).
        runVideo(clk_ + 2);
        const bool raster = gateArray_.interruptRequested();
        gateArray_.acknowledgeInterrupt();
        advance(2);
        // On a Plus the ASIC puts its vector on the bus; on a CPC nothing
        // drives it.
        return plus_ ? asic_.acknowledgeInterrupt(raster) : 0xFF;
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
    TapeDeck tape_;
    Asic asic_;
    bool plus_ = false;
    AudioMixer audio_;

    uint64_t clk_ = 0;
    uint64_t soundClk_ = 0;  // microsecond the sound generators have reached
    uint64_t videoClk_ = 0;  // start of the microsecond of video in progress
    uint64_t runUntil_ = 0;

    SsmHook ssmHook_;
    int ssmLow_ = -1;        // first half of a code, or -1
    uint16_t ssmNextPc_ = 0;  // where the second half must sit

    ExecHook execHook_;
    bool stopRun_ = false;
    bool breakInstructions_ = false;
    bool breakInstructionHit_ = false;
    std::vector<uint8_t> watched_;  // one flag per address; empty when nothing is watched
    int watchedCount_ = 0;
    MemoryHook memoryHook_;
    IoHook ioHook_;
    std::vector<uint8_t> memoryWatch_;  // per address: bit 0 reads, bit 1 writes
    int memoryWatched_ = 0;             // addresses with a bit set
    void memoryAccess(uint16_t addr, uint8_t value, uint8_t previous, bool write);

    void advance(unsigned tstates)
    {
        clk_ += tstates;
        runVideo(clk_);
    }

    // Brings the picture, the CRTC and the Gate Array up to a moment given
    // in T-states.
    void runVideo(uint64_t until)
    {
        while (videoClk_ + 4 <= until) {
            // The microsecond that has just ended is drawn last, so that it
            // shows everything the CPU did during it...
            gateArray_.render(crtc_, memory_.baseRam(), monitor_);
            videoClk_ += 4;
            // ...and the CRTC moves on at once: what the CPU reads during
            // the new microsecond (VSYNC through the PPI, say) is current,
            // and an interrupt raised by the end of an HSYNC can be taken
            // by an instruction that finishes at that very moment. Two
            // sets of measurements on real machines pin this down: the
            // Shaker's video memory test (A1) and the acid test of the
            // interrupt position against HSYNC width.
            crtc_.tick();
            gateArray_.sync(crtc_, monitor_);
        }
    }

    // The Z80 samples WAIT in the middle of a machine cycle and idles until
    // the Gate Array lets it through.
    void waitForGateArray() { advance((kReadyPhase - static_cast<unsigned>(clk_)) & 3); }

    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t value);  // every device but the CRTC
    void crtcWrite(uint16_t port, uint8_t value, bool early = false);
    void gateArrayWrite(uint8_t value, uint8_t portHigh);
    uint8_t portB();
    void ssmOpcode(uint8_t op);
    void updatePsgBus();
    void syncSound();
};

}  // namespace tuxape
