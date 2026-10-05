// Interrupt behaviour of the Z80 core: response in the three modes, NMI,
// HALT, the one-instruction delay after EI, and the NMOS quirk of LD A,I.

#include <cstdio>
#include <initializer_list>

#include "core/z80.h"
#include "test_bus.h"

namespace {

using Cpu = tuxape::Z80<TestBus>;

int failures = 0;

#define CHECK_EQ(got, want)                                                                         \
    do {                                                                                            \
        const long long g = (got), w = (want);                                                      \
        if (g != w) {                                                                               \
            std::printf("%s:%d: %s = %llx, want %llx\n", __FILE__, __LINE__, #got, g, w);           \
            ++failures;                                                                             \
        }                                                                                           \
    } while (0)

void load(TestBus& bus, uint16_t addr, std::initializer_list<uint8_t> bytes)
{
    for (uint8_t b : bytes)
        bus.ram[addr++] = b;
}

// Runs one step and returns how many T-states it took.
uint64_t timedStep(Cpu& cpu, TestBus& bus)
{
    const uint64_t before = bus.tstates;
    cpu.step();
    return bus.tstates - before;
}

void testMode1()
{
    TestBus bus;
    Cpu cpu(bus);
    cpu.pc = 0x1234;
    cpu.sp = 0x8000;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = true;
    bus.irqLine = true;

    CHECK_EQ(timedStep(cpu, bus), 13);
    CHECK_EQ(cpu.pc, 0x0038);
    CHECK_EQ(cpu.sp, 0x7FFE);
    CHECK_EQ(bus.ram[0x7FFE], 0x34);
    CHECK_EQ(bus.ram[0x7FFF], 0x12);
    CHECK_EQ(cpu.iff1, false);
    CHECK_EQ(cpu.iff2, false);
    CHECK_EQ(cpu.r, 1);
    CHECK_EQ(cpu.wz, 0x0038);
}

void testMode2()
{
    TestBus bus;
    Cpu cpu(bus);
    cpu.pc = 0x1234;
    cpu.sp = 0x8000;
    cpu.im = 2;
    cpu.i = 0x40;
    cpu.iff1 = cpu.iff2 = true;
    bus.irqLine = true;
    bus.irqData = 0x10;
    load(bus, 0x4010, {0x78, 0x56});

    CHECK_EQ(timedStep(cpu, bus), 19);
    CHECK_EQ(cpu.pc, 0x5678);
    CHECK_EQ(cpu.sp, 0x7FFE);
}

void testMode0Restart()
{
    TestBus bus;
    Cpu cpu(bus);
    cpu.pc = 0x1234;
    cpu.sp = 0x8000;
    cpu.im = 0;
    cpu.iff1 = cpu.iff2 = true;
    bus.irqLine = true;
    bus.irqData = 0xEF;  // RST 28h

    CHECK_EQ(timedStep(cpu, bus), 13);
    CHECK_EQ(cpu.pc, 0x0028);
    CHECK_EQ(cpu.sp, 0x7FFE);
}

void testMaskedWhenDisabled()
{
    TestBus bus;
    Cpu cpu(bus);
    cpu.pc = 0x1000;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = false;
    bus.irqLine = true;

    CHECK_EQ(timedStep(cpu, bus), 4);  // the NOP at 0x1000 runs instead
    CHECK_EQ(cpu.pc, 0x1001);
}

void testEiDelaysByOneInstruction()
{
    TestBus bus;
    Cpu cpu(bus);
    load(bus, 0x1000, {0xFB, 0x00, 0x00});  // EI; NOP; NOP
    cpu.pc = 0x1000;
    cpu.sp = 0x8000;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = false;
    bus.irqLine = true;

    cpu.step();  // EI
    CHECK_EQ(cpu.pc, 0x1001);
    cpu.step();  // the NOP after EI still runs
    CHECK_EQ(cpu.pc, 0x1002);
    cpu.step();  // now the interrupt is taken
    CHECK_EQ(cpu.pc, 0x0038);
    CHECK_EQ(bus.ram[0x7FFE], 0x02);
    CHECK_EQ(bus.ram[0x7FFF], 0x10);
}

void testNmi()
{
    TestBus bus;
    Cpu cpu(bus);
    cpu.pc = 0x1234;
    cpu.sp = 0x8000;
    cpu.iff1 = cpu.iff2 = true;
    cpu.nmi();

    CHECK_EQ(timedStep(cpu, bus), 11);
    CHECK_EQ(cpu.pc, 0x0066);
    CHECK_EQ(cpu.sp, 0x7FFE);
    CHECK_EQ(cpu.iff1, false);
    CHECK_EQ(cpu.iff2, true);  // kept so RETN can restore it

    load(bus, 0x0066, {0xED, 0x45});  // RETN
    cpu.step();
    CHECK_EQ(cpu.pc, 0x1234);
    CHECK_EQ(cpu.iff1, true);
}

void testHalt()
{
    TestBus bus;
    Cpu cpu(bus);
    load(bus, 0x1000, {0x76, 0x3C});  // HALT; INC A
    cpu.pc = 0x1000;
    cpu.sp = 0x8000;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = true;

    cpu.step();
    CHECK_EQ(cpu.halted, true);
    const uint8_t r = cpu.r;
    CHECK_EQ(timedStep(cpu, bus), 4);  // idles one fetch at a time
    CHECK_EQ(cpu.halted, true);
    CHECK_EQ(cpu.pc, 0x1001);
    CHECK_EQ(cpu.r, r + 1);

    bus.irqLine = true;
    cpu.step();
    CHECK_EQ(cpu.halted, false);
    CHECK_EQ(cpu.pc, 0x0038);
    // Execution resumes after the HALT.
    CHECK_EQ(bus.ram[0x7FFE], 0x01);
    CHECK_EQ(bus.ram[0x7FFF], 0x10);
}

void testLdAIParityLostOnInterrupt()
{
    for (bool interrupt : {false, true}) {
        TestBus bus;
        Cpu cpu(bus);
        load(bus, 0x1000, {0xED, 0x57, 0x00});  // LD A,I; NOP
        cpu.pc = 0x1000;
        cpu.sp = 0x8000;
        cpu.im = 1;
        cpu.iff1 = cpu.iff2 = true;

        cpu.step();
        CHECK_EQ(cpu.reg[Cpu::F] & Cpu::PF, Cpu::PF);  // P/V mirrors IFF2
        bus.irqLine = interrupt;
        cpu.step();
        CHECK_EQ(cpu.reg[Cpu::F] & Cpu::PF, interrupt ? 0 : Cpu::PF);
    }
}

void testPrefixNotInterruptible()
{
    TestBus bus;
    Cpu cpu(bus);
    load(bus, 0x1000, {0xDD, 0x21, 0x34, 0x12});  // LD IX,1234h
    cpu.pc = 0x1000;
    cpu.sp = 0x8000;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = false;

    cpu.step();
    CHECK_EQ(cpu.ix, 0x1234);
    CHECK_EQ(cpu.pc, 0x1004);
}

}  // namespace

int main()
{
    testMode1();
    testMode2();
    testMode0Restart();
    testMaskedWhenDisabled();
    testEiDelaysByOneInstruction();
    testNmi();
    testHalt();
    testLdAIParityLostOnInterrupt();
    testPrefixNotInterruptible();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all interrupt checks passed\n");
    return 0;
}
