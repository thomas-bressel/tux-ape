// The Z80 disassembler: instructions written the way WinAPE's debugger
// writes them, and their lengths, checked against what the emulated
// processor does with the same bytes.

#include <initializer_list>
#include <string>
#include <vector>

#include "check.h"
#include "core/cpc.h"
#include "core/disasm.h"

namespace {

using namespace tuxape;
using Flow = Instruction::Flow;

Instruction at(uint16_t address, std::initializer_list<uint8_t> bytes)
{
    const std::vector<uint8_t> code(bytes);
    return disassemble(address, [&](uint16_t a) {
        const size_t index = static_cast<uint16_t>(a - address);
        return index < code.size() ? code[index] : uint8_t(0);
    });
}

void expect(std::initializer_list<uint8_t> bytes, const char* text, Flow flow = Flow::Plain, uint16_t address = 0x4000)
{
    const Instruction got = at(address, bytes);
    if (got.text != text || got.length != static_cast<int>(bytes.size()) || got.flow != flow) {
        std::printf("%02X...: \"%s\" of %d bytes, want \"%s\" of %zu\n", *bytes.begin(), got.text.c_str(), got.length,
                    text, bytes.size());
        ++g_failures;
    }
}

void testTexts()
{
    expect({0x00}, "NOP");
    expect({0x71}, "LD (HL),C");
    expect({0x18, 0x08}, "JR #0040", Flow::Jump, 0x0036);
    expect({0x18, 0xFE}, "JR #4000", Flow::Jump);
    expect({0x20, 0x80}, "JR NZ,#3F82", Flow::Jump);
    expect({0x10, 0x05}, "DJNZ #4007", Flow::Jump);
    expect({0xC3, 0x39, 0xB9}, "JP #B939", Flow::Jump);
    expect({0xCA, 0x00, 0x80}, "JP Z,#8000", Flow::Jump);
    expect({0xE9}, "JP (HL)", Flow::Jump);
    expect({0xCD, 0x5A, 0xBB}, "CALL #BB5A", Flow::Call);
    expect({0xDC, 0x34, 0x12}, "CALL C,#1234", Flow::Call);
    expect({0xFF}, "RST #38", Flow::Call);
    expect({0xC7}, "RST #00", Flow::Call);
    expect({0xC9}, "RET", Flow::Return);
    expect({0xF8}, "RET M", Flow::Return);
    expect({0x76}, "HALT", Flow::Halt);
    expect({0x01, 0x34, 0x12}, "LD BC,#1234");
    expect({0x3E, 0x07}, "LD A,#07");
    expect({0x36, 0xFF}, "LD (HL),#FF");
    expect({0x22, 0x00, 0xC0}, "LD (#C000),HL");
    expect({0x2A, 0x00, 0xC0}, "LD HL,(#C000)");
    expect({0x32, 0x01, 0x02}, "LD (#0201),A");
    expect({0x3A, 0x01, 0x02}, "LD A,(#0201)");
    expect({0x0A}, "LD A,(BC)");
    expect({0x12}, "LD (DE),A");
    expect({0x08}, "EX AF,AF'");
    expect({0xEB}, "EX DE,HL");
    expect({0xE3}, "EX (SP),HL");
    expect({0xD9}, "EXX");
    expect({0xF9}, "LD SP,HL");
    expect({0x09}, "ADD HL,BC");
    expect({0x34}, "INC (HL)");
    expect({0x3D}, "DEC A");
    expect({0x1B}, "DEC DE");
    expect({0x86}, "ADD A,(HL)");
    expect({0x97}, "SUB A");
    expect({0xBE}, "CP (HL)");
    expect({0xE6, 0x0F}, "AND #0F");
    expect({0xFE, 0x20}, "CP #20");
    expect({0xD3, 0xFF}, "OUT (#FF),A");
    expect({0xDB, 0x7F}, "IN A,(#7F)");
    expect({0xF5}, "PUSH AF");
    expect({0xE1}, "POP HL");
    expect({0x27}, "DAA");
    expect({0xF3}, "DI");
    // The CB page.
    expect({0xCB, 0x00}, "RLC B");
    expect({0xCB, 0x36}, "SLL (HL)");
    expect({0xCB, 0x7E}, "BIT 7,(HL)");
    expect({0xCB, 0x87}, "RES 0,A");
    expect({0xCB, 0xFD}, "SET 7,L");
    // The ED page.
    expect({0xED, 0x49}, "OUT (C),C");
    expect({0xED, 0x78}, "IN A,(C)");
    expect({0xED, 0x70}, "IN (C)");
    expect({0xED, 0x71}, "OUT (C),0");
    expect({0xED, 0x42}, "SBC HL,BC");
    expect({0xED, 0x7A}, "ADC HL,SP");
    expect({0xED, 0x43, 0x00, 0x90}, "LD (#9000),BC");
    expect({0xED, 0x7B, 0x00, 0x90}, "LD SP,(#9000)");
    expect({0xED, 0x44}, "NEG");
    expect({0xED, 0x4D}, "RETI", Flow::Return);
    expect({0xED, 0x45}, "RETN", Flow::Return);
    expect({0xED, 0x56}, "IM 1");
    expect({0xED, 0x5E}, "IM 2");
    expect({0xED, 0x47}, "LD I,A");
    expect({0xED, 0x5F}, "LD A,R");
    expect({0xED, 0x6F}, "RLD");
    expect({0xED, 0xA0}, "LDI");
    expect({0xED, 0xB0}, "LDIR", Flow::Repeat);
    expect({0xED, 0xB9}, "CPDR", Flow::Repeat);
    expect({0xED, 0xA3}, "OUTI");
    expect({0xED, 0xB3}, "OTIR", Flow::Repeat);
    expect({0xED, 0x00}, "NOP");
    expect({0xED, 0xFF}, "BRK");
    // The index registers, their halves, and the page that goes with them.
    expect({0xDD, 0x21, 0x34, 0x12}, "LD IX,#1234");
    expect({0xFD, 0x2A, 0x00, 0x80}, "LD IY,(#8000)");
    expect({0xDD, 0x7E, 0x05}, "LD A,(IX+#05)");
    expect({0xFD, 0x77, 0xFD}, "LD (IY-#03),A");
    expect({0xDD, 0x36, 0x02, 0x99}, "LD (IX+#02),#99");
    expect({0xDD, 0x66, 0x01}, "LD H,(IX+#01)");
    expect({0xDD, 0x75, 0x00}, "LD (IX+#00),L");
    expect({0xDD, 0x65}, "LD HX,LX");
    expect({0xFD, 0x7C}, "LD A,HY");
    expect({0xFD, 0x2E, 0x10}, "LD LY,#10");
    expect({0xDD, 0x84}, "ADD A,HX");
    expect({0xDD, 0x86, 0x7F}, "ADD A,(IX+#7F)");
    expect({0xDD, 0x34, 0x80}, "INC (IX-#80)");
    expect({0xDD, 0x24}, "INC HX");
    expect({0xDD, 0x09}, "ADD IX,BC");
    expect({0xFD, 0x29}, "ADD IY,IY");
    expect({0xDD, 0xE5}, "PUSH IX");
    expect({0xFD, 0xE9}, "JP (IY)", Flow::Jump);
    expect({0xDD, 0xE3}, "EX (SP),IX");
    expect({0xDD, 0xF9}, "LD SP,IX");
    expect({0xDD, 0xCB, 0x05, 0x46}, "BIT 0,(IX+#05)");
    expect({0xFD, 0xCB, 0xFF, 0xDE}, "SET 3,(IY-#01)");
    expect({0xDD, 0xCB, 0x02, 0x16}, "RL (IX+#02)");
    expect({0xDD, 0xCB, 0x02, 0x10}, "RL (IX+#02),B");
    expect({0xDD, 0xCB, 0x02, 0x81}, "RES 0,(IX+#02),C");
    // A prefix ahead of another prefix does nothing.
    expect({0xDD, 0x00}, "NOP");  // ahead of an instruction that has no use for it, it goes with it
    CHECK(at(0x4000, {0xDD, 0xFD, 0x21}).text == "NOP");
    CHECK_EQ(at(0x4000, {0xDD, 0xED, 0xB0}).length, 1);
    // An instruction that runs over the top of memory.
    CHECK(at(0xFFFF, {0xC3, 0x34, 0x12}).text == "JP #1234");
}

// Every instruction that goes on to the next one: the processor ends up as
// many bytes further as the disassembler says the instruction is long.
void testLengths()
{
    Cpc cpc;
    int checked = 0;
    auto check = [&](std::initializer_list<uint8_t> bytes) {
        const Instruction instruction = at(0x8000, bytes);
        if (instruction.flow != Flow::Plain)
            return;
        // RAM everywhere, registers that keep I/O and memory writes out
        // of harm's way, and no interrupts.
        cpc.reset();
        cpc.out(0x7F00, 0xC0);
        cpc.out(0x7F00, 0x8C);
        auto& cpu = cpc.cpu();
        cpu.reg[cpu.A] = cpu.reg[cpu.B] = cpu.reg[cpu.C] = 0xFF;
        cpu.reg[cpu.H] = cpu.reg[cpu.D] = 0x90;
        cpu.ix = cpu.iy = 0x9080;
        cpu.sp = 0xA000;
        cpu.pc = 0x8000;
        uint16_t address = 0x8000;
        for (const uint8_t byte : bytes)
            cpc.memory().write(address++, byte);
        cpc.stepInstruction();
        ++checked;
        if (cpu.pc != 0x8000 + instruction.length) {
            std::printf("%s: the processor went %d bytes on, the disassembler says %d\n", instruction.text.c_str(),
                        cpu.pc - 0x8000, instruction.length);
            ++g_failures;
        }
    };
    for (int op = 0; op < 256; ++op) {
        const uint8_t b = static_cast<uint8_t>(op);
        if (b != 0xCB && b != 0xDD && b != 0xED && b != 0xFD)
            check({b, 0x12, 0x34, 0x56});
        check({0xCB, b, 0x12, 0x34});
        check({0xED, b, 0x12, 0x34});
        if (b != 0xDD && b != 0xED && b != 0xFD) {
            check({0xDD, b, 0x12, 0x34});
            check({0xFD, b, 0x12, 0x34});
        }
        check({0xDD, 0xCB, 0x05, b});
        check({0xFD, 0xCB, 0xFB, b});
    }
    CHECK(checked > 1500);
}

// The debugger's handles on the machine: a single step, a stop on an
// address, and the break instruction.
void testStepping()
{
    Cpc cpc;
    cpc.out(0x7F00, 0x8C);
    const uint8_t code[] = {0x3E, 0x05,        // 8000 LD A,5
                            0x3D,              // 8002 DEC A
                            0x20, 0xFD,        // 8003 JR NZ,8002
                            0xED, 0xFF,        // 8005 BRK
                            0x01, 0x03, 0x00,  // 8007 LD BC,3
                            0xED, 0xB0,        // 800A LDIR
                            0x18, 0xFE};       // 800C JR 800C
    for (size_t i = 0; i < sizeof code; ++i)
        cpc.memory().write(static_cast<uint16_t>(0x8000 + i), code[i]);
    auto& cpu = cpc.cpu();
    cpu.pc = 0x8000;
    cpu.sp = 0xA000;
    cpc.stepInstruction();
    CHECK_EQ(cpu.pc, 0x8002);
    CHECK_EQ(cpu.reg[cpu.A], 5);
    cpc.stepInstruction();
    cpc.stepInstruction();
    CHECK_EQ(cpu.pc, 0x8002);

    // A watched address ends the run before its instruction.
    int stops = 0;
    cpc.setExecHook([&](uint16_t) {
        ++stops;
        cpc.stopRun();
    });
    cpc.watchAddress(0x8005);
    const uint64_t before = cpc.microseconds();
    cpc.run(1000);
    CHECK_EQ(stops, 1);
    CHECK_EQ(cpu.pc, 0x8005);
    CHECK_EQ(cpu.reg[cpu.A], 0);
    CHECK(cpc.microseconds() - before < 100);
    // The time left over is not made up for later.
    cpc.watchAddress(0x8005, false);
    cpc.watchAddress(0x800C);
    // The break instruction stops the run after itself, when asked to.
    cpc.setBreakInstructions(true);
    cpc.run(1000);
    CHECK_EQ(cpu.pc, 0x8007);
    CHECK(cpc.breakInstructionHit());
    CHECK(!cpc.breakInstructionHit());
    CHECK_EQ(stops, 1);
    // LDIR is one instruction for each byte it moves.
    cpc.stepInstruction();
    CHECK_EQ(cpu.pc, 0x800A);
    cpc.stepInstruction();
    CHECK_EQ(cpu.pc, 0x800A);
    CHECK_EQ(cpu.reg[cpu.C], 2);
    cpc.run(1000);
    CHECK_EQ(cpu.pc, 0x800C);
    CHECK_EQ(stops, 2);
    // Without the option, ED FF is passed like any instruction that does
    // nothing.
    cpc.setBreakInstructions(false);
    cpc.watchAddress(0x800C, false);
    cpu.pc = 0x8005;
    cpc.run(20);
    CHECK_EQ(cpu.pc, 0x800C);
    CHECK(!cpc.breakInstructionHit());
}

}  // namespace

int main()
{
    testTexts();
    testLengths();
    testStepping();
    return checkSummary("disasm");
}
