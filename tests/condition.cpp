// Breakpoint conditions, and the hooks on memory and on input and output
// that memory and I/O breakpoints stand on. No ROM is needed.

#include <string>
#include <vector>

#include "check.h"
#include "core/condition.h"
#include "core/cpc.h"

namespace {

using namespace tuxape;

void testConditions()
{
    Cpc cpc;
    auto& z80 = cpc.cpu();
    z80.reg[z80.A] = 3;
    z80.reg[z80.F] = 0x41;
    z80.reg[z80.B] = 0x12;
    z80.reg[z80.C] = 0x34;
    z80.reg[z80.H] = 0x40;
    z80.reg[z80.L] = 0x00;
    z80.ix = 0x0507;
    z80.iy = 0xA1B2;
    z80.sp = 0xBFF0;
    z80.pc = 0x8000;
    z80.af2 = 0x1122;
    cpc.memory().write(0x4000, 0xFF);
    cpc.out(0x7F00, 0x8D);  // mode 1, both ROMs out
    cpc.out(0x7F00, 0x05);  // pen 5...
    cpc.out(0x7F00, 0x4B);  // ...in colour 11
    cpc.out(0xBC00, 6);
    cpc.out(0xBD00, 25);
    cpc.psg().setRegister(7, 0x38);

    ConditionContext context;
    context.address = 0xC123;
    context.value = 4;
    context.previous = 9;
    context.symbol = [](const std::string& name) -> std::optional<int32_t> {
        if (name == "START")
            return 0x8000;
        return std::nullopt;
    };
    const auto value = [&](const std::string& text) {
        const std::optional<int32_t> result = evaluateCondition(text, cpc, context);
        if (!result) {
            std::printf("\"%s\" is not understood\n", text.c_str());
            ++g_failures;
            return 0x7FFFFFFF;
        }
        return *result;
    };
    // The help's own examples.
    CHECK_EQ(value("HL = address"), 0);
    CHECK_EQ(value("peek(#4000) = #ff"), -1);
    CHECK_EQ(value("value = 4"), -1);
    CHECK_EQ(value("A = 3 and XH = 5"), -1);
    CHECK_EQ(value("A = 3 and XH = 6"), 0);
    CHECK_EQ(value("4 and 2"), 0);
    CHECK_EQ(value("not #fffffffe"), 1);
    // Registers, any case.
    CHECK_EQ(value("a"), 3);
    CHECK_EQ(value("AF"), 0x0341);
    CHECK_EQ(value("bc"), 0x1234);
    CHECK_EQ(value("b * 256 + c = BC"), -1);
    CHECK_EQ(value("ix"), 0x0507);
    CHECK_EQ(value("IXh + LX + hy + IYL"), 5 + 7 + 0xA1 + 0xB2);
    CHECK_EQ(value("sp - pc"), 0x3FF0);
    CHECK_EQ(value("af'"), 0x1122);
    CHECK_EQ(value("f and %01000000"), 0x40);
    // Numbers and operators, comparisons before `and` before `or`.
    CHECK_EQ(value("10 + &10 + #10 + %10"), 44);
    CHECK_EQ(value("2 + 3 * 4"), 14);
    CHECK_EQ(value("(2 + 3) * 4"), 20);
    CHECK_EQ(value("7 mod 4 + 9 / 2"), 7);
    CHECK_EQ(value("-1"), -1);
    CHECK_EQ(value("1 < 2 or 1 > 2"), -1);
    CHECK_EQ(value("1 <= 1 and 2 >= 3"), 0);
    CHECK_EQ(value("1 <> 1"), 0);
    CHECK_EQ(value("6 xor 3"), 5);
    CHECK_EQ(value("true = not false"), -1);
    // What the breakpoint saw, and the machine's state.
    CHECK_EQ(value("address"), 0xC123);
    CHECK_EQ(value("previous - value"), 5);
    CHECK_EQ(value("mode"), 1);
    CHECK_EQ(value("palette_select"), 5);
    CHECK_EQ(value("ga_palette()"), 11);
    CHECK_EQ(value("ga_palette(5)"), 11);
    CHECK_EQ(value("crtc_select"), 6);
    CHECK_EQ(value("crtc()"), 25);
    CHECK_EQ(value("crtc(6) = 25"), -1);
    CHECK_EQ(value("psg(7)"), 0x38);
    CHECK_EQ(value("ay(7)"), 0x38);
    CHECK_EQ(value("psg_select = ay_select"), -1);
    CHECK_EQ(value("lower_enabled or upper_enabled"), 0);
    CHECK_EQ(value("ram_bank"), 0xC0);
    CHECK_EQ(value("fdc_motor"), 0);
    CHECK_EQ(value("tape_motor"), 0);
    CHECK_EQ(value("byte(#12345678)"), 0x78);
    CHECK_EQ(value("hibyte(#12345678)"), 0x56);
    CHECK_EQ(value("word(#12345678)"), 0x5678);
    CHECK_EQ(value("hiword(#12345678)"), 0x1234);
    CHECK_EQ(value("start"), 0x8000);
    CHECK_EQ(value("pc = Start"), -1);
    CHECK_EQ(value("poke(#4100, 1, 2, 3)"), 0);
    CHECK_EQ(value("peek(#4100) + peek(#4101) * 10 + peek(#4102) * 100"), 321);
    // Not conditions.
    for (const char* text : {"", "1 +", "nowhere", "(1", "1)", "peek()", "1 / 0", "#", "12ab", "ga_palette(17)", "a b"})
        CHECK(!evaluateCondition(text, cpc, context).has_value());
}

// A program that reads and writes memory and ports, watched.
void testHooks()
{
    Cpc cpc;
    // 8000 LD A,(9000) / 8003 LD (9001),A / 8006 LD BC,7F8D / 8009 OUT (C),C
    // 800B LD B,F5 / 800D IN A,(C) / 800F LD HL,9002 / 8012 INC (HL) / 8013 JR 8013
    const uint8_t program[] = {0x3A, 0x00, 0x90, 0x32, 0x01, 0x90, 0x01, 0x8D, 0x7F, 0xED, 0x49,
                               0x06, 0xF5, 0xED, 0x78, 0x21, 0x02, 0x90, 0x34, 0x18, 0xFE};
    cpc.out(0x7F00, 0x8C);
    for (size_t i = 0; i < sizeof program; ++i)
        cpc.memory().write(static_cast<uint16_t>(0x8000 + i), program[i]);
    cpc.memory().write(0x9000, 0x5A);
    cpc.memory().write(0x9001, 0x11);
    cpc.memory().write(0x9002, 0x7F);
    cpc.cpu().pc = 0x8000;
    cpc.cpu().iff1 = cpc.cpu().iff2 = false;

    struct Access {
        unsigned address, value, previous;
        bool write;
    };
    std::vector<Access> memory, io;
    cpc.setMemoryHook([&](uint16_t addr, uint8_t value, uint8_t previous, bool write) {
        memory.push_back({addr, value, previous, write});
    });
    cpc.setIoHook([&](uint16_t port, uint8_t value, bool write) { io.push_back({port, value, 0, write}); });
    cpc.watchMemory(0x9000, true, false);
    cpc.watchMemory(0x9001, false, true);
    cpc.watchMemory(0x9002, true, true);
    cpc.watchMemory(0x8000, true, true);  // instructions fetched there do not count
    for (int n = 0; n < 9; ++n)
        cpc.stepInstruction();
    CHECK_EQ(cpc.cpu().pc, 0x8013);
    CHECK_EQ(memory.size(), 4);
    if (memory.size() == 4) {
        CHECK(memory[0].address == 0x9000 && memory[0].value == 0x5A && !memory[0].write);
        CHECK(memory[1].address == 0x9001 && memory[1].value == 0x5A && memory[1].previous == 0x11 && memory[1].write);
        CHECK(memory[2].address == 0x9002 && memory[2].value == 0x7F && !memory[2].write);
        CHECK(memory[3].address == 0x9002 && memory[3].value == 0x80 && memory[3].previous == 0x7F && memory[3].write);
    }
    CHECK_EQ(io.size(), 2);
    if (io.size() == 2) {
        CHECK(io[0].address == 0x7F8D && io[0].value == 0x8D && io[0].write);
        CHECK(io[1].address == 0xF58D && !io[1].write);
    }
    // Watches taken away, the program is left alone again.
    memory.clear();
    cpc.clearMemoryWatches();
    cpc.setIoHook(nullptr);
    cpc.cpu().pc = 0x8000;
    for (int n = 0; n < 9; ++n)
        cpc.stepInstruction();
    CHECK(memory.empty());
    CHECK_EQ(io.size(), 2);
}

}  // namespace

int main()
{
    testConditions();
    testHooks();
    return checkSummary("condition");
}
