// Instruction durations on the CPC.
//
// The Gate Array stretches the Z80's machine cycles, so every instruction
// lasts a whole number of microseconds, and not always the number its
// T-state count suggests. These are the figures CPC programmers count in
// ("NOPs"); cycle-exact software depends on them.

#include <initializer_list>
#include <vector>

#include "check.h"
#include "core/cpc.h"

namespace {

using tuxape::Cpc;

struct Case {
    const char* name;
    std::vector<uint8_t> code;
    int microseconds;
    // Register setup before the instruction runs.
    uint8_t b = 2;
    uint8_t flags = 0x00;  // zero and carry clear
    uint16_t bc = 0;       // if non-zero, overrides BC for block instructions
};

int measure(const Case& c)
{
    Cpc cpc;
    auto& cpu = cpc.cpu();
    // All RAM: no ROM in the way of the test program.
    cpc.memory().setRomEnables(false, false);
    uint16_t addr = 0x4000;
    for (uint8_t byte : c.code)
        cpc.memory().write(addr++, byte);
    cpc.memory().write(addr, 0x00);  // a NOP to realign on
    // Somewhere harmless for instructions that read or jump through memory.
    cpu.pc = 0x4000;
    cpu.sp = 0x8000;
    cpu.setHl(0x9000);
    cpu.setDe(0xA000);
    cpu.ix = cpu.iy = 0x9000;
    cpu.setBc(c.bc ? c.bc : static_cast<uint16_t>(c.b << 8 | 0x7F));
    cpu.reg[cpu.F] = c.flags;
    // With A = FF, "OUT (n),A" and "IN A,(n)" address no device at all.
    cpu.reg[cpu.A] = 0xFF;
    // Return and vector targets all lead to NOPs.
    cpc.memory().write(0x8000, 0x00);
    cpc.memory().write(0x8001, 0x50);

    const uint64_t start = cpc.clock();
    cpu.step();
    // The instruction may end part-way through a microsecond; the NOP that
    // follows always ends on a boundary.
    const uint16_t next = cpu.pc;
    cpc.memory().write(next, 0x00);
    cpu.step();
    const uint64_t elapsed = cpc.clock() - start;
    CHECK_EQ(elapsed % 4, 0);
    return static_cast<int>(elapsed / 4) - 1;
}

const Case kCases[] = {
    {"NOP", {0x00}, 1},
    {"LD B,n", {0x06, 0x12}, 2},
    {"LD BC,nn", {0x01, 0x34, 0x12}, 3},
    {"LD (BC),A", {0x02}, 2, 0x90},
    {"LD A,(BC)", {0x0A}, 2, 0x90},
    {"INC BC", {0x03}, 2},
    {"INC B", {0x04}, 1},
    {"ADD HL,BC", {0x09}, 3},
    {"RLCA", {0x07}, 1},
    {"EX AF,AF'", {0x08}, 1},
    {"DJNZ taken", {0x10, 0x00}, 4, 2},
    {"DJNZ not taken", {0x10, 0x00}, 3, 1},
    {"JR e", {0x18, 0x00}, 3},
    {"JR NZ taken", {0x20, 0x00}, 3},
    {"JR Z not taken", {0x28, 0x00}, 2},
    {"LD (nn),HL", {0x22, 0x00, 0x90}, 5},
    {"LD HL,(nn)", {0x2A, 0x00, 0x90}, 5},
    {"LD (nn),A", {0x32, 0x00, 0x90}, 4},
    {"LD A,(nn)", {0x3A, 0x00, 0x90}, 4},
    {"INC (HL)", {0x34}, 3},
    {"LD (HL),n", {0x36, 0x12}, 3},
    {"SCF", {0x37}, 1},
    {"LD B,C", {0x41}, 1},
    {"LD B,(HL)", {0x46}, 2},
    {"LD (HL),B", {0x70}, 2},
    {"ADD A,B", {0x80}, 1},
    {"ADD A,(HL)", {0x86}, 2},
    {"RET NZ taken", {0xC0}, 4},
    {"RET Z not taken", {0xC8}, 2},
    {"POP BC", {0xC1}, 3},
    {"JP nn", {0xC3, 0x00, 0x50}, 3},
    {"JP Z not taken", {0xCA, 0x00, 0x50}, 3},
    {"CALL nn", {0xCD, 0x00, 0x50}, 5},
    {"CALL Z not taken", {0xCC, 0x00, 0x50}, 3},
    {"PUSH BC", {0xC5}, 4},
    {"ADD A,n", {0xC6, 0x12}, 2},
    {"RST 38h", {0xFF}, 4},
    {"RET", {0xC9}, 3},
    {"OUT (n),A", {0xD3, 0xFF}, 3},
    {"IN A,(n)", {0xDB, 0xFF}, 3},
    {"EXX", {0xD9}, 1},
    {"EX (SP),HL", {0xE3}, 6},
    {"JP (HL)", {0xE9}, 1},
    {"EX DE,HL", {0xEB}, 1},
    {"DI", {0xF3}, 1},
    {"LD SP,HL", {0xF9}, 2},

    {"RLC B", {0xCB, 0x00}, 2},
    {"RLC (HL)", {0xCB, 0x06}, 4},
    {"BIT 0,B", {0xCB, 0x40}, 2},
    {"BIT 0,(HL)", {0xCB, 0x46}, 3},
    {"SET 0,(HL)", {0xCB, 0xC6}, 4},

    {"IN B,(C)", {0xED, 0x40}, 4, 0xFF},
    {"OUT (C),B", {0xED, 0x41}, 4, 0xFF},
    {"SBC HL,BC", {0xED, 0x42}, 4},
    {"LD (nn),BC", {0xED, 0x43, 0x00, 0x90}, 6},
    {"LD BC,(nn)", {0xED, 0x4B, 0x00, 0x90}, 6},
    {"NEG", {0xED, 0x44}, 2},
    {"RETN", {0xED, 0x45}, 4},
    {"IM 1", {0xED, 0x56}, 2},
    {"LD I,A", {0xED, 0x47}, 3},
    {"LD A,I", {0xED, 0x57}, 3},
    {"RRD", {0xED, 0x67}, 5},
    {"LDI", {0xED, 0xA0}, 5, 0, 0, 0x0002},
    {"LDIR repeating", {0xED, 0xB0}, 6, 0, 0, 0x0002},
    {"LDIR last", {0xED, 0xB0}, 5, 0, 0, 0x0001},
    {"CPI", {0xED, 0xA1}, 4, 0, 0, 0x0002},
    {"CPIR repeating", {0xED, 0xB1}, 6, 0, 0, 0x0002},
    {"INI", {0xED, 0xA2}, 5, 0, 0, 0xFFFF},
    {"OUTI", {0xED, 0xA3}, 5, 0, 0, 0xFFFF},
    {"OTIR repeating", {0xED, 0xB3}, 6, 0, 0, 0xFFFF},

    {"LD IX,nn", {0xDD, 0x21, 0x34, 0x12}, 4},
    {"ADD IX,BC", {0xDD, 0x09}, 4},
    {"INC IX", {0xDD, 0x23}, 3},
    {"LD B,(IX+d)", {0xDD, 0x46, 0x01}, 5},
    {"LD (IX+d),B", {0xDD, 0x70, 0x01}, 5},
    {"LD (IX+d),n", {0xDD, 0x36, 0x01, 0x12}, 6},
    {"INC (IX+d)", {0xDD, 0x34, 0x01}, 6},
    {"ADD A,(IX+d)", {0xDD, 0x86, 0x01}, 5},
    {"EX (SP),IX", {0xDD, 0xE3}, 7},
    {"PUSH IX", {0xDD, 0xE5}, 5},
    {"POP IX", {0xDD, 0xE1}, 4},
    {"JP (IX)", {0xDD, 0xE9}, 2},
    {"RLC (IX+d)", {0xDD, 0xCB, 0x01, 0x06}, 7},
    {"BIT 0,(IX+d)", {0xDD, 0xCB, 0x01, 0x46}, 6},
    {"SET 0,(IX+d)", {0xDD, 0xCB, 0x01, 0xC6}, 7},
};

void testHaltAndInterrupt()
{
    Cpc cpc;
    auto& cpu = cpc.cpu();
    cpc.memory().setRomEnables(false, false);
    // The Gate Array counts HSYNCs to time its interrupt, so the CRTC needs
    // the firmware's usual settings.
    const uint8_t crtc[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7};
    for (uint8_t r = 0; r < sizeof crtc; ++r) {
        cpc.out(0xBC00, r);
        cpc.out(0xBD00, crtc[r]);
    }
    for (uint16_t a = 0; a < 0x100; ++a)
        cpc.memory().write(a, 0x00);
    cpu.pc = 0x4000;
    cpu.sp = 0x8000;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = true;
    cpc.memory().write(0x4000, 0x76);  // HALT
    cpu.step();
    // Idle until the Gate Array raises its interrupt.
    int guard = 0;
    while (!cpc.irq() && ++guard < 100000)
        cpu.step();
    CHECK(cpc.irq());
    const uint64_t start = cpc.clock();
    cpu.step();
    CHECK_EQ(cpu.pc, 0x0038);
    CHECK(!cpc.irq());
    cpu.step();  // NOP at 0038h, to land on a microsecond boundary
    // 13 T-states stretched by the same wait rule as everything else.
    CHECK_EQ((cpc.clock() - start) / 4 - 1, 5);
}

// A machine idling on NOPs, stopped at the microsecond where the Gate Array
// raises an interrupt request, interrupts enabled or not.
struct Idle {
    Cpc cpc;
    uint64_t request = 0;  // when the next request is raised, in T-states

    explicit Idle(bool enabled, tuxape::CrtcType type = tuxape::CrtcType::HD6845S)
    {
        auto& cpu = cpc.cpu();
        cpc.crtc().setType(type);
        cpc.memory().setRomEnables(false, false);
        const uint8_t crtc[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7};
        for (uint8_t r = 0; r < sizeof crtc; ++r) {
            cpc.out(0xBC00, r);
            cpc.out(0xBD00, crtc[r]);
        }
        for (uint32_t a = 0; a < 0x10000; ++a)
            cpc.memory().write(static_cast<uint16_t>(a), 0x00);
        cpu.pc = 0x4000;
        cpu.sp = 0x8000;
        cpu.setHl(0x9000);
        cpu.im = 1;
        cpu.iff1 = cpu.iff2 = true;
        // Take one interrupt to know where the requests fall: one every 52
        // lines from there on.
        int guard = 0;
        while (!cpc.irq() && ++guard < 100000)
            cpu.step();
        request = cpc.clock() + 52 * 64 * 4;
        cpu.step();
        cpu.pc = 0x4000;
        cpu.iff1 = cpu.iff2 = enabled;
    }

    // Idles until `tstates` before `moment`, then runs one instruction.
    void runAt(uint64_t moment, unsigned tstates, std::initializer_list<uint8_t> code)
    {
        auto& cpu = cpc.cpu();
        while (cpc.clock() + tstates < moment) {
            if (cpu.pc >= 0x7000)
                cpu.pc = 0x4000;
            cpu.step();
        }
        CHECK_EQ(cpc.clock() + tstates, moment);
        uint16_t addr = cpu.pc;
        for (uint8_t byte : code)
            cpc.memory().write(addr++, byte);
        cpu.step();
    }
};

// The Gate Array raises its request half a microsecond before the place
// where the CRTC moves on in this model, and the Z80 looks at the line as
// its last T-state begins: so an instruction can end one T-state short of
// that place and still be the one interrupted (Compendium 27.7.2, checked
// by the Shaker's "killer" tests on a real CRTC 0 machine).
void testInterruptAgainstInstructionEnd()
{
    struct {
        const char* name;
        std::initializer_list<uint8_t> code;
        unsigned before;  // T-states from its start to the request
        bool taken;       // interrupted right after it?
    } const cases[] = {
        {"NOP ending on the request", {0x00}, 4, true},
        {"ADD HL,DE ending 1 T before", {0x19}, 12, true},
        {"CP (HL) ending 1 T before", {0xBE}, 8, true},
        {"DEC HL ending 2 T before", {0x2B}, 8, false},
        {"LD A,I ending 3 T before", {0xED, 0x57}, 12, false},
        {"NOP ending 4 T before", {0x00}, 8, false},
    };
    for (const auto& c : cases) {
        Idle idle(true);
        idle.runAt(idle.request, c.before, c.code);
        const bool taken = idle.cpc.cpu().interruptDue();
        if (taken != c.taken) {
            std::printf("%s: interrupt %s\n", c.name, taken ? "taken at once" : "left for later");
            ++g_failures;
        }
    }
    // The ASICs raise it a quarter of a microsecond sooner: an instruction
    // ending two T-states short is interrupted as well. The Shaker's "INT
    // Z80A signal" runs through a field of DEC DE, which end there one time
    // in two, and notes where each interrupt stops it: &0758, &077A, &0779
    // on real machines of types 3 and 4, &0759, &0779, &077A on those of
    // types 0, 1 and 2; with NOP and CP (HL) all five give the same.
    for (const tuxape::CrtcType type : {tuxape::CrtcType::UM6845R, tuxape::CrtcType::MC6845, tuxape::CrtcType::AsicPlus,
                                        tuxape::CrtcType::PreAsic}) {
        const bool asic = type == tuxape::CrtcType::AsicPlus || type == tuxape::CrtcType::PreAsic;
        struct {
            std::initializer_list<uint8_t> code;
            unsigned before;
            bool taken;
        } const others[] = {
            {{0x00}, 4, true},    // NOP ending on the request
            {{0xBE}, 8, true},    // CP (HL) ending 1 T before
            {{0x1B}, 8, asic},    // DEC DE ending 2 T before
            {{0x00}, 8, false},   // NOP ending 4 T before
        };
        for (const auto& c : others) {
            Idle idle(true, type);
            idle.runAt(idle.request, c.before, c.code);
            CHECK_EQ(idle.cpc.cpu().interruptDue(), c.taken);
        }
    }
}

// A request left pending is acknowledged when interrupts come back on, and
// the acknowledge takes bit 5 off the Gate Array's line counter. When that
// falls together with the end of an HSYNC, which of the two comes first
// depends on where the interrupted instruction really ends (Compendium
// 27.7.1): the counter is left at 0 (next interrupt in 52 lines) or at 32
// (in 20 lines).
void testAcknowledgeAgainstHsync()
{
    struct {
        const char* name;
        std::initializer_list<uint8_t> code;
        int counter;
    } const cases[] = {
        {"CP (HL)", {0xBE}, 0},
        {"ADD A,E", {0x83}, 0},
        {"DEC HL", {0x2B}, 32},
        {"LD A,I", {0xED, 0x57}, 32},
    };
    for (const auto& c : cases) {
        Idle idle(false);
        auto& cpu = idle.cpc.cpu();
        // 32 lines after the request the counter goes from 31 to 32. An EI
        // and the instruction come in the microseconds just before, placed
        // so that an instruction of seven T-states ends 9 T-states short.
        const uint64_t count32 = idle.request + 32 * 64 * 4;
        const unsigned length = c.code.size() == 2 ? 12 : c.code.begin()[0] == 0x83 ? 4 : 8;
        idle.runAt(count32, 8 + length + 4, {0xFB});
        CHECK_EQ(idle.cpc.gateArray().interruptCounter(), 31);
        CHECK(!cpu.interruptDue());  // not straight after the EI
        idle.runAt(count32, 8 + length, c.code);
        CHECK(cpu.interruptDue());
        cpu.step();
        CHECK_EQ(cpu.pc, 0x0038);
        if (idle.cpc.gateArray().interruptCounter() != c.counter) {
            std::printf("%s: line counter at %d after the acknowledge, want %d\n", c.name,
                        idle.cpc.gateArray().interruptCounter(), c.counter);
            ++g_failures;
        }
    }
}

}  // namespace

int main()
{
    for (const Case& c : kCases) {
        const int got = measure(c);
        if (got != c.microseconds) {
            std::printf("%s: %d microseconds, want %d\n", c.name, got, c.microseconds);
            ++g_failures;
        }
    }
    testHaltAndInterrupt();
    testInterruptAgainstInstructionEnd();
    testAcknowledgeAgainstHsync();
    return checkSummary("cpc_timing");
}
