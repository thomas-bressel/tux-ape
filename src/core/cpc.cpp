#include "core/cpc.h"

namespace tuxape {

Cpc::Cpc()
    : cpu_(*this)
{
    asic_.attach(&gateArray_, &crtc_);
    asic_.attachSound([this](uint16_t address) { return memory_.readRam(address); },
                      [this](int number, uint8_t value) { psg_.setRegister(number, value); });
    gateArray_.attach(&asic_);
    memory_.setAsic(&asic_);
    reset();
}

void Cpc::setCartridge(const Cartridge* cartridge, bool discRom)
{
    plus_ = cartridge != nullptr && cartridge->pages() > 0;
    memory_.setCartridge(plus_ ? std::span<const uint8_t>(cartridge->data) : std::span<const uint8_t>(), discRom);
    gateArray_.setPlus(plus_);
    ppi_.setPlus(plus_ || plusPpi_);
    asic_.reset();
}

void Cpc::reset()
{
    cpu_.reset();
    memory_.reset();
    crtc_.reset();
    gateArray_.reset();
    ppi_.reset();
    psg_.reset();
    fdc_.reset();
    tape_.setMotor(false, microseconds());
    asic_.reset();
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
    if (watchedCount_ == 0 && !breakInstructions_ && memoryWatched_ == 0 && !ioHook_ && !turbo_) {
        while (clk_ < runUntil_)
            cpu_.step();
    } else {
        stopRun_ = false;
        while (clk_ < runUntil_) {
            // Not when an interrupt is about to be taken: the instruction
            // will run, and be reported, after the handler returns.
            if (watchedCount_ != 0 && watched_[cpu_.pc] && execHook_ && !cpu_.interruptDue())
                execHook_(cpu_.pc);
            if (stopRun_)
                break;
            turboLeft_ = 4;
            cpu_.step();
            if (stopRun_)
                break;
        }
        // Stopped: the time not used is not owed.
        if (stopRun_)
            runUntil_ = clk_;
        stopRun_ = false;
    }
    syncSound();
}

void Cpc::alignClocks()
{
    syncSound();
    clk_ = (clk_ + 3) & ~uint64_t(3);
    videoClk_ = clk_;
    runUntil_ = clk_;
    stopRun_ = false;
}

void Cpc::stepInstruction()
{
    turboLeft_ = 4;
    cpu_.step();
    runUntil_ = clk_;
    syncSound();
}

void Cpc::watchAddress(uint16_t addr, bool watch)
{
    if (watched_.empty())
        watched_.assign(0x10000, 0);
    watchedCount_ += static_cast<int>(watch) - static_cast<int>(watched_[addr] != 0);
    watched_[addr] = watch;
}

void Cpc::watchMemory(uint16_t addr, bool reads, bool writes)
{
    if (memoryWatch_.empty())
        memoryWatch_.assign(0x10000, 0);
    const uint8_t before = memoryWatch_[addr];
    memoryWatch_[addr] = static_cast<uint8_t>(before | (reads ? 1 : 0) | (writes ? 2 : 0));
    memoryWatched_ += static_cast<int>(memoryWatch_[addr] != 0) - static_cast<int>(before != 0);
}

void Cpc::clearMemoryWatches()
{
    memoryWatch_.clear();
    memoryWatched_ = 0;
}

void Cpc::memoryAccess(uint16_t addr, uint8_t value, uint8_t previous, bool write)
{
    if (memoryHook_ && (memoryWatch_[addr] & (write ? 2 : 1)))
        memoryHook_(addr, value, previous, write);
}

// The CPC decodes I/O addresses one line at a time: each device answers when
// "its" address bit is low, whatever the other bits are. Several devices can
// therefore be addressed by the same access.

void Cpc::ioWrite(uint16_t port, uint8_t value)
{
    const uint8_t high = static_cast<uint8_t>(port >> 8);

    if (!(port & 0x8000)) {
        if ((value & 0xC0) == 0xC0)
            memory_.selectRamBank(value, high);
        else if (port & 0x4000)
            gateArrayWrite(value, high);
    }
    if (!(port & 0x2000))
        memory_.selectUpperRom(value);
    if (!(port & 0x1000)) {
        // The printer's port: seven bits of data and the strobe, which
        // the port turns upside down. A character goes as the strobe
        // falls; a Digiblaster makes sound of the whole byte instead.
        if (digiblaster_) {
            syncSound();
            dac_ = static_cast<uint8_t>(value ^ 0x80);
        } else if (printerHook_ && (value & 0x80) && !(printerLatch_ & 0x80)) {
            printerHook_(value & 0x7F);
        }
        printerLatch_ = value;
    }
    if (amDrum_ && high == 0xFF) {
        syncSound();
        dac_ = value;
    }
    if (!(port & 0x0800)) {
        ppi_.write(high & 3, value);
        // The mouse gives its next step once the joystick's line has been
        // left and come back to.
        if ((ppi_.outputC() & 0x0F) != 9)
            amxArmed_ = true;
        updatePsgBus();
        tape_.setMotor(ppi_.outputC() & 0x10, microseconds());
    }
    // Disc interface: &FA7E is the motor latch, &FB7F the FDC's data port.
    if (!(port & 0x0480)) {
        if (!(port & 0x0100))
            fdc_.writeMotor(value, microseconds());
        else if (port & 1)
            fdc_.writeData(value, microseconds());
    }
}

void Cpc::gateArrayWrite(uint8_t value, uint8_t portHigh)
{
    if ((value & 0xC0) == 0xC0) {
        memory_.selectRamBank(value, portHigh);
    } else if (plus_ && asic_.unlocked() && (value & 0xE0) == 0xA0) {
        // Unlocked, the ASIC keeps half of the Gate Array's third register
        // for itself: RMR2.
        asic_.setRmr2(value);
        memory_.setRmr2(value);
    } else {
        gateArray_.write(value);
        memory_.setRomEnables(gateArray_.lowerRomEnabled(), gateArray_.upperRomEnabled());
        if (plus_ && (value & 0xC0) == 0x40)
            asic_.setHardwareColour(gateArray_.selectedPen(), value & 0x1F);
    }
}

void Cpc::crtcWrite(uint16_t port, uint8_t value, bool early)
{
    if (port & 0x4000)
        return;
    switch ((port >> 8) & 3) {
    case 0:
        crtc_.select(value);
        if (plus_)
            asic_.sequence(value);
        break;
    case 1: {
        const bool hsync = crtc_.hsync();
        crtc_.write(value, early);
        if (crtc_.hsync() && !hsync)
            gateArray_.hsyncStartedByWrite(crtc_, !early);
        break;
    }
    }
}

uint8_t Cpc::ioRead(uint16_t port)
{
    const uint8_t high = static_cast<uint8_t>(port >> 8);
    uint8_t value = 0xFF;

    if (!(port & 0x4000)) {
        switch (high & 3) {
        case 2: value &= crtc_.readStatus(); break;
        case 3: value &= crtc_.readData(); break;
        default:
            // The CRTC is not wired to the Z80's read and write lines: an
            // input from one of its write addresses is a write, of whatever
            // is on the bus. The Compendium (4.4.2) reports the high address
            // byte arriving, as with IN A,(n).
            crtcWrite(port, high);
            break;
        }
    }
    if (!(port & 0x0800)) {
        uint8_t pins = 0xFF;
        switch (high & 3) {
        case 3:
            // A Plus reads port A here too.
            if (!plus_)
                break;
            [[fallthrough]];
        case 0:
            // The PSG drives the bus only while BC1 is high and BDIR low.
            if ((ppi_.outputC() >> 6) == 1)
                pins = psg_.read(keyboardLine(ppi_.outputC() & 0x0F));
            break;
        case 1:
            pins = portB();
            break;
        }
        value &= ppi_.read(high & 3, pins);
    }
    // Disc interface: &FB7E is the FDC's status port, &FB7F its data port.
    // When an address selects the PPI as well, it is the PPI that is read:
    // on a real 6128 port B reads the same at every address that selects
    // it, the disc interface's included.
    if (!(port & 0x0480) && (port & 0x0100) && (port & 0x0800))
        value &= (port & 1) ? fdc_.readData(microseconds()) : fdc_.readStatus(microseconds());
    return value;
}

// A line of the keyboard as the machine reads it: the joystick's has the
// mouse on it too, when there is one.
uint8_t Cpc::keyboardLine(int line)
{
    uint8_t value = keyboard_.line(line);
    if (!amx_ || line != 9)
        return value;
    if (amxArmed_) {
        amxArmed_ = false;
        amxPulse_ = 0;
        if (amxY_ < 0) {
            amxPulse_ |= 0x01;  // up
            ++amxY_;
        } else if (amxY_ > 0) {
            amxPulse_ |= 0x02;  // down
            --amxY_;
        }
        if (amxX_ < 0) {
            amxPulse_ |= 0x04;  // left
            ++amxX_;
        } else if (amxX_ > 0) {
            amxPulse_ |= 0x08;  // right
            --amxX_;
        }
    }
    return static_cast<uint8_t>(value & ~(amxPulse_ | amxButtons_));
}

uint8_t Cpc::portB()
{
    // Bit 7: cassette data in. Bit 6: printer busy (nothing is connected).
    // Bit 5: /EXP, low with nothing on the expansion port. Bit 4: 50 Hz
    // link. Bits 3-1: maker links, 111 = Amstrad.
    uint8_t value = 0x5E;
    // Bit 6: the printer is busy, unless there is one.
    if (printerHook_ && !digiblaster_)
        value &= static_cast<uint8_t>(~0x40);
    if (crtc_.vsync())
        value |= 0x01;
    // A program loading from tape reads this port all the time: the sound
    // is brought up to date before the level it finds can change.
    if (tapeSound_ && tape_.playing() && tape_.motor())
        syncSound();
    tapeHeard_ = tape_.level(microseconds());
    if (tapeHeard_)
        value |= 0x80;
    return value;
}

void Cpc::ssmOpcode(uint8_t op)
{
    // The opcodes the SSM convention allows: those with no function at all.
    const bool allowed = op <= 0x3F || (op >= 0x7F && op <= 0x9F) || op >= 0xC0
                         || ((op & 0xE4) == 0xA4);
    // The second half must be the very next instruction.
    const uint16_t at = static_cast<uint16_t>(cpu_.pc - 2);
    if (allowed && ssmLow_ >= 0 && at == ssmNextPc_) {
        const uint16_t code = static_cast<uint16_t>(op << 8 | ssmLow_);
        ssmLow_ = -1;
        ssmHook_(code);
        return;
    }
    ssmLow_ = allowed ? op : -1;
    ssmNextPc_ = cpu_.pc;
}

void Cpc::syncSound()
{
    // The sound chip is only brought up to date when something is about to
    // change it, and at the end of each run.
    const uint64_t now = microseconds();
    if (!audio_.enabled()) {
        soundClk_ = now;
        return;
    }
    // The tape, if it is to be heard: as the program last found it.
    const float tape = tapeSound_ && tapeHeard_ && tape_.playing() && tape_.motor() ? 0.3f : 0.0f;
    // And the converters, around their middle.
    const float other = tape + static_cast<float>(dac_ - 0x80) / 256.0f;
    while (soundClk_ + 8 <= now) {
        psg_.tick();
        audio_.addStep(Psg::amplitude(psg_.level(0)), Psg::amplitude(psg_.level(1)),
                       Psg::amplitude(psg_.level(2)), other);
        soundClk_ += 8;
    }
}

void Cpc::updatePsgBus()
{
    syncSound();
    // Port C bits 7-6 are the PSG's BDIR and BC1; port A is its data bus.
    switch (ppi_.outputC() >> 6) {
    case 3: psg_.selectRegister(ppi_.outputA()); break;
    case 2: psg_.write(ppi_.outputA()); break;
    }
}

}  // namespace tuxape
