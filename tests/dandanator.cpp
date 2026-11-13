// The Dandanator mini cartridge: its commands (three &FD prefixes, then
// LD (IY+n),B / C / A), the configuration that waits for a RET, the zones
// that follow the ROM enable, the flash written with its JEDEC sequences,
// the buttons read at a reset; and in a machine, a program that changes
// the slot it runs from.

#include <cstdint>
#include <vector>

#include "check.h"
#include "core/cpc.h"
#include "core/dandanator.h"

namespace {

using namespace tuxape;

// A command as the Z80 gives it: the prefixes and the opcode fetched, then
// the byte the instruction writes.
void command(Dandanator& cart, uint8_t opcode, uint8_t value, int prefixes = 3)
{
    for (int i = 0; i < prefixes; ++i)
        cart.fetched(0xFD);
    cart.fetched(opcode);
    cart.written(0x8000, value);
}

void testReset()
{
    Dandanator cart;
    // Zone 0 on slot 0 at &0000, zone 1 hidden.
    CHECK_EQ(cart.shownSlot(0, true, true), 0);
    CHECK_EQ(cart.shownSlot(1, true, true), -1);
    CHECK_EQ(cart.shownSlot(2, true, true), -1);
    CHECK_EQ(cart.shownSlot(3, true, true), -1);
    const Dandanator::State s = cart.state();
    CHECK(s.shown[0] && !s.shown[1] && !s.commandsOff && !s.followRom && !s.flashWrites && s.serialOut);

    // The left button: slot 31. The middle one: the machine's own ROMs.
    cart.setBoot(Dandanator::Boot::Slot31);
    cart.reset();
    CHECK_EQ(cart.shownSlot(0, true, true), 31);
    cart.setBoot(Dandanator::Boot::Machine);
    cart.reset();
    CHECK_EQ(cart.shownSlot(0, true, true), -1);
}

void testCommands()
{
    Dandanator cart;
    // Two prefixes are not enough; three or more are.
    command(cart, 0x70, 0x05, 2);
    CHECK_EQ(cart.shownSlot(0, true, true), 0);
    command(cart, 0x70, 0x05, 3);
    CHECK_EQ(cart.shownSlot(0, true, true), 5);
    command(cart, 0x71, 0x1E, 5);
    CHECK_EQ(cart.shownSlot(1, true, true), 30);
    // Something else between the prefixes and the command: nothing.
    cart.fetched(0xFD);
    cart.fetched(0xFD);
    cart.fetched(0x00);
    cart.fetched(0xFD);
    cart.fetched(0x70);
    cart.written(0x8000, 0x07);
    CHECK_EQ(cart.shownSlot(0, true, true), 5);
    // Bit 5 hides a zone.
    command(cart, 0x70, 0x25);
    CHECK_EQ(cart.shownSlot(0, true, true), -1);
    command(cart, 0x70, 0x05);

    // The configuration at once: zone 0 at &8000, zone 1 at &C000.
    command(cart, 0x77, 0x8C);
    CHECK_EQ(cart.shownSlot(0, true, true), -1);
    CHECK_EQ(cart.shownSlot(2, true, true), 5);
    CHECK_EQ(cart.shownSlot(1, true, true), -1);
    CHECK_EQ(cart.shownSlot(3, true, true), 30);
    // Both hidden.
    command(cart, 0x77, 0x83);
    for (int quarter = 0; quarter < 4; ++quarter)
        CHECK_EQ(cart.shownSlot(quarter, true, true), -1);

    // The other configuration: serial, flash writes, USB line, follow slot.
    command(cart, 0x77, 0x1B);
    Dandanator::State s = cart.state();
    CHECK(s.serialIn && s.flashWrites && !s.serialOut);
    CHECK_EQ(s.followSlot, 31);

    // No more commands: those after are not taken, the zones stay.
    command(cart, 0x77, 0xA0);
    CHECK_EQ(cart.shownSlot(0, true, true), 5);
    CHECK(cart.state().commandsOff);
    command(cart, 0x70, 0x09);
    CHECK_EQ(cart.shownSlot(0, true, true), 5);
    // Until a reset.
    cart.reset();
    command(cart, 0x70, 0x09);
    CHECK_EQ(cart.shownSlot(0, true, true), 9);
}

void testRetWait()
{
    Dandanator cart;
    // Waits for a RET: nothing yet, then all at once.
    command(cart, 0x77, 0xC1);
    CHECK(cart.state().waitingRet);
    CHECK_EQ(cart.shownSlot(0, true, true), 0);
    cart.fetched(0x00);
    CHECK_EQ(cart.shownSlot(0, true, true), 0);
    CHECK(cart.fetched(0xC9));
    CHECK(!cart.state().waitingRet);
    CHECK_EQ(cart.shownSlot(0, true, true), -1);
    // A RET with nothing waiting changes nothing.
    CHECK(!cart.fetched(0xC9));

    // Any command between cancels it.
    command(cart, 0x77, 0xC0);
    command(cart, 0x70, 0x03);
    CHECK(!cart.state().waitingRet);
    cart.fetched(0xC9);
    CHECK_EQ(cart.shownSlot(0, true, true), 3);

    // Following the ROM enable, only on a RET: zone 0 takes slot 28 + n,
    // and is seen at &0000 only while the lower ROM is.
    command(cart, 0x77, 0x10);  // follow slot 30
    command(cart, 0x77, 0xD0);
    CHECK(!cart.state().followRom);
    cart.fetched(0xC9);
    CHECK(cart.state().followRom);
    CHECK_EQ(cart.shownSlot(0, true, true), 30);
    CHECK_EQ(cart.shownSlot(0, false, true), -1);
    // Zone 1 at &C000, with the upper ROM.
    command(cart, 0x71, 0x02);
    command(cart, 0x77, 0xD8);
    cart.fetched(0xC9);
    CHECK_EQ(cart.shownSlot(3, true, true), 2);
    CHECK_EQ(cart.shownSlot(3, true, false), -1);
    // Elsewhere, not.
    command(cart, 0x77, 0xD0);
    cart.fetched(0xC9);
    CHECK_EQ(cart.shownSlot(1, false, false), 2);
}

void testFlash()
{
    Dandanator cart;
    std::vector<uint8_t> image(Dandanator::kSlotSize * 2, 0x00);
    image[Dandanator::kSlotSize + 0x123] = 0xFF;
    cart.setFlash(image);
    CHECK_EQ(cart.flash().size(), Dandanator::kSize);
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize * 2], 0xFF);  // the rest erased
    CHECK(!cart.changed());

    // The chip sees 15 bits of address, A14 being the slot's bit 0: its
    // &5555 is &1555 in an odd slot, its &2AAA &2AAA in an even one. Zone 0
    // on slot 1 at &0000, zone 1 on slot 0 at &4000, the flash written to.
    command(cart, 0x70, 0x01);
    command(cart, 0x71, 0x00);
    command(cart, 0x77, 0x80);
    command(cart, 0x77, 0x02);
    const auto unlock = [&](uint8_t step) {
        cart.written(0x1555, 0xAA);
        cart.written(0x6AAA, 0x55);
        cart.written(0x1555, step);
    };
    const auto program = [&](uint16_t address, uint8_t value) {
        unlock(0xA0);
        cart.written(address, value);
    };
    program(0x0123, 0x3C);
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize + 0x123], 0x3C);
    CHECK(cart.changed());
    // Bits go from 1 to 0 only.
    program(0x0123, 0xC3);
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize + 0x123], 0x00);
    // A sequence broken: nothing written.
    cart.clearChanged();
    cart.written(0x1555, 0xAA);
    cart.written(0x6AAB, 0x55);
    cart.written(0x1555, 0xA0);
    cart.written(0x0200, 0x00);
    CHECK(!cart.changed());

    // A sector of 4K erased.
    unlock(0x80);
    cart.written(0x1555, 0xAA);
    cart.written(0x6AAA, 0x55);
    cart.written(0x1234, 0x30);  // the sector's address
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize + 0x1000], 0xFF);
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize + 0x1FFF], 0xFF);
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize + 0x0FFF], 0x00);
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize + 0x2000], 0x00);

    // Writes off: nothing.
    command(cart, 0x77, 0x00);
    cart.clearChanged();
    program(0x0300, 0x00);
    CHECK(!cart.changed());

    // The whole chip.
    command(cart, 0x77, 0x02);
    unlock(0x80);
    unlock(0x10);
    CHECK_EQ(cart.flash()[0], 0xFF);
    CHECK_EQ(cart.flash()[Dandanator::kSlotSize + 0x0FFF], 0xFF);
}

// In a machine: a program in slot 0 puts zone 0 on slot 1 with a command
// that also writes to the RAM, and goes on from slot 1, where it reads a
// byte of that slot.
void testInMachine()
{
    const std::vector<uint8_t> code = {
        0xF3,                          // DI
        0x06, 0x01,                    // LD B,1
        0xFD, 0x21, 0x00, 0x80,        // LD IY,&8000
        0xFD, 0xFD, 0xFD, 0x70, 0x00,  // LD (IY+0),B: zone 0 on slot 1
        0x3A, 0x00, 0x01,              // LD A,(&0100)
        0x32, 0x01, 0x80,              // LD (&8001),A
        0x18, 0xFE,                    // JR $
    };
    std::vector<uint8_t> image(Dandanator::kSlotSize * 2, 0x00);
    for (int slot = 0; slot < 2; ++slot)
        std::copy(code.begin(), code.end(), image.begin() + slot * Dandanator::kSlotSize);
    image[0x100] = 0xA5;
    image[Dandanator::kSlotSize + 0x100] = 0x5A;

    Cpc cpc;
    cpc.fitDandanator(true);
    cpc.dandanator()->setFlash(image);
    cpc.coldReset();
    CHECK_EQ(cpc.memory().read(0x0100), 0xA5);
    for (int i = 0; i < 12; ++i)
        cpc.stepInstruction();
    CHECK_EQ(cpc.dandanator()->state().slot[0], 1);
    CHECK_EQ(cpc.memory().read(0x0100), 0x5A);
    CHECK_EQ(cpc.memory().readRam(0x8000), 0x01);  // the command's write, in the RAM
    CHECK_EQ(cpc.memory().readRam(0x8001), 0x5A);
    // A write where the cartridge is shown goes to the RAM below it.
    cpc.memory().write(0x0100, 0x77);
    CHECK_EQ(cpc.memory().read(0x0100), 0x5A);
    CHECK_EQ(cpc.memory().readRam(0x0100), 0x77);

    // A reset: slot 0 again. Taken out: the machine's own memory.
    cpc.coldReset();
    CHECK_EQ(cpc.memory().read(0x0100), 0xA5);
    cpc.fitDandanator(false);
    CHECK(cpc.dandanator() == nullptr);
    CHECK(cpc.memory().read(0x0100) != 0xA5);
}

}  // namespace

int main()
{
    testReset();
    testCommands();
    testRetWait();
    testFlash();
    testInMachine();
    return checkSummary("dandanator");
}
