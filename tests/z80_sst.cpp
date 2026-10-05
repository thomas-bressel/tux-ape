// Runs the SingleStepTests Z80 vectors (https://github.com/SingleStepTests/z80).
// Each case gives the full CPU state and memory before one instruction, and
// what they must be afterwards, along with the bus activity of every T-state.
//
// Checked here: all registers including MEMPTR and the internal latches, the
// memory contents, the number of T-states, and the order, address and value
// of every memory write and I/O access.
//
//   z80_sst <file.json | directory>...

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "core/z80.h"
#include "mini_json.h"

namespace {

struct BusEvent {
    char kind;  // 'w' memory write, 'i' port read, 'o' port write
    uint16_t addr;
    uint8_t value;
    bool operator==(const BusEvent&) const = default;
};

struct RecordingBus {
    uint8_t ram[0x10000] = {};
    uint64_t tstates = 0;
    std::vector<BusEvent> events;
    std::vector<BusEvent> portReads;  // values to supply, in order
    size_t nextPortRead = 0;

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
        events.push_back({'w', addr, v});
    }
    uint8_t in(uint16_t port)
    {
        tstates += 4;
        const uint8_t v = nextPortRead < portReads.size() ? portReads[nextPortRead++].value : 0xFF;
        events.push_back({'i', port, v});
        return v;
    }
    void out(uint16_t port, uint8_t v)
    {
        tstates += 4;
        events.push_back({'o', port, v});
    }
    void tick(int n) { tstates += n; }
    bool irq() const { return false; }
    uint8_t irqAck() { return 0xFF; }
};

using Cpu = tuxape::Z80<RecordingBus>;

uint16_t u16(const Json& state, const char* key)
{
    return static_cast<uint16_t>(state.at(key).number);
}

uint8_t u8(const Json& state, const char* key)
{
    return static_cast<uint8_t>(state.at(key).number);
}

void loadState(Cpu& cpu, RecordingBus& bus, const Json& s)
{
    cpu.pc = u16(s, "pc");
    cpu.sp = u16(s, "sp");
    cpu.reg[Cpu::A] = u8(s, "a");
    cpu.reg[Cpu::F] = u8(s, "f");
    cpu.reg[Cpu::B] = u8(s, "b");
    cpu.reg[Cpu::C] = u8(s, "c");
    cpu.reg[Cpu::D] = u8(s, "d");
    cpu.reg[Cpu::E] = u8(s, "e");
    cpu.reg[Cpu::H] = u8(s, "h");
    cpu.reg[Cpu::L] = u8(s, "l");
    cpu.i = u8(s, "i");
    cpu.r = u8(s, "r");
    cpu.wz = u16(s, "wz");
    cpu.ix = u16(s, "ix");
    cpu.iy = u16(s, "iy");
    cpu.af2 = u16(s, "af_");
    cpu.bc2 = u16(s, "bc_");
    cpu.de2 = u16(s, "de_");
    cpu.hl2 = u16(s, "hl_");
    cpu.im = u8(s, "im");
    cpu.iff1 = u8(s, "iff1");
    cpu.iff2 = u8(s, "iff2");
    cpu.halted = false;
    cpu.setLatches({u8(s, "ei") != 0, u8(s, "p") != 0, u8(s, "q")});
    for (const Json& cell : s.at("ram").items)
        bus.ram[static_cast<uint16_t>(cell[0].number)] = static_cast<uint8_t>(cell[1].number);
}

// Appends "name: got X, want Y" for every field that differs.
std::string compareState(const Cpu& cpu, const RecordingBus& bus, const Json& s)
{
    std::ostringstream diff;
    auto check = [&](const char* name, unsigned got, unsigned want) {
        if (got != want)
            diff << "  " << name << ": got " << std::hex << got << ", want " << want << std::dec << "\n";
    };
    check("pc", cpu.pc, u16(s, "pc"));
    check("sp", cpu.sp, u16(s, "sp"));
    check("a", cpu.reg[Cpu::A], u8(s, "a"));
    check("f", cpu.reg[Cpu::F], u8(s, "f"));
    check("b", cpu.reg[Cpu::B], u8(s, "b"));
    check("c", cpu.reg[Cpu::C], u8(s, "c"));
    check("d", cpu.reg[Cpu::D], u8(s, "d"));
    check("e", cpu.reg[Cpu::E], u8(s, "e"));
    check("h", cpu.reg[Cpu::H], u8(s, "h"));
    check("l", cpu.reg[Cpu::L], u8(s, "l"));
    check("i", cpu.i, u8(s, "i"));
    check("r", cpu.r, u8(s, "r"));
    check("wz", cpu.wz, u16(s, "wz"));
    check("ix", cpu.ix, u16(s, "ix"));
    check("iy", cpu.iy, u16(s, "iy"));
    check("af'", cpu.af2, u16(s, "af_"));
    check("bc'", cpu.bc2, u16(s, "bc_"));
    check("de'", cpu.de2, u16(s, "de_"));
    check("hl'", cpu.hl2, u16(s, "hl_"));
    check("im", cpu.im, u8(s, "im"));
    check("iff1", cpu.iff1, u8(s, "iff1"));
    check("iff2", cpu.iff2, u8(s, "iff2"));
    const auto latches = cpu.latches();
    check("ei", latches.eiDelay, u8(s, "ei"));
    check("p", latches.ldAIR, u8(s, "p"));
    check("q", latches.q, u8(s, "q"));
    for (const Json& cell : s.at("ram").items) {
        const uint16_t addr = static_cast<uint16_t>(cell[0].number);
        if (bus.ram[addr] != cell[1].number)
            diff << "  ram[" << std::hex << addr << "]: got " << unsigned(bus.ram[addr]) << ", want "
                 << cell[1].number << std::dec << "\n";
    }
    return diff.str();
}

// Memory writes and port accesses the test expects, in order.
std::vector<BusEvent> expectedEvents(const Json& test)
{
    // Port accesses are listed separately and carry no timestamp, so merge
    // them by walking the per-T-state trace: an access shows up there as the
    // first T-state with the IORQ flag after a gap.
    std::vector<BusEvent> ports;
    if (const Json* list = test.find("ports"))
        for (const Json& p : list->items)
            ports.push_back({p[2].string == "r" ? 'i' : 'o', static_cast<uint16_t>(p[0].number),
                             static_cast<uint8_t>(p[1].number)});

    std::vector<BusEvent> events;
    size_t nextPort = 0;
    bool inIo = false;
    for (const Json& cycle : test.at("cycles").items) {
        const std::string& pins = cycle[2].string;
        const bool io = pins[3] == 'i';
        if (io && !inIo && nextPort < ports.size())
            events.push_back(ports[nextPort++]);
        inIo = io;
        if (pins[1] == 'w' && pins[2] == 'm' && !cycle[1].isNull())
            events.push_back({'w', static_cast<uint16_t>(cycle[0].number), static_cast<uint8_t>(cycle[1].number)});
    }
    return events;
}

std::string describe(const std::vector<BusEvent>& events)
{
    std::ostringstream out;
    for (const BusEvent& e : events)
        out << ' ' << e.kind << ':' << std::hex << e.addr << '=' << unsigned(e.value) << std::dec;
    return out.str();
}

struct Totals {
    long cases = 0;
    long failed = 0;
    int filesFailed = 0;
};

void runFile(const std::filesystem::path& path, Totals& totals)
{
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    const Json tests = Json::parse(text);

    static RecordingBus bus;
    long failed = 0;
    for (const Json& test : tests.items) {
        const Json& initial = test.at("initial");
        bus.tstates = 0;
        bus.events.clear();
        bus.portReads.clear();
        bus.nextPortRead = 0;
        if (const Json* list = test.find("ports"))
            for (const Json& p : list->items)
                if (p[2].string == "r")
                    bus.portReads.push_back({'i', static_cast<uint16_t>(p[0].number),
                                             static_cast<uint8_t>(p[1].number)});

        Cpu cpu(bus);
        loadState(cpu, bus, initial);
        cpu.step();

        std::string problems = compareState(cpu, bus, test.at("final"));
        const size_t wantT = test.at("cycles").size();
        if (bus.tstates != wantT)
            problems += "  T-states: got " + std::to_string(bus.tstates) + ", want " + std::to_string(wantT) + "\n";
        const auto want = expectedEvents(test);
        if (bus.events != want)
            problems += "  bus: got" + describe(bus.events) + "\n       want" + describe(want) + "\n";

        if (!problems.empty() && failed++ < 2)
            std::printf("FAIL %s (%s)\n%s", test.at("name").string.c_str(), path.filename().c_str(),
                        problems.c_str());

        // Leave memory clean for the next case.
        for (const Json& cell : initial.at("ram").items)
            bus.ram[static_cast<uint16_t>(cell[0].number)] = 0;
        for (const Json& cell : test.at("final").at("ram").items)
            bus.ram[static_cast<uint16_t>(cell[0].number)] = 0;
        for (const BusEvent& e : bus.events)
            if (e.kind == 'w')
                bus.ram[e.addr] = 0;
    }
    totals.cases += static_cast<long>(tests.size());
    totals.failed += failed;
    if (failed) {
        ++totals.filesFailed;
        std::printf("%s: %ld of %zu failed\n", path.filename().c_str(), failed, tests.size());
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    std::vector<std::filesystem::path> files;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path arg = argv[i];
        if (std::filesystem::is_directory(arg)) {
            for (const auto& entry : std::filesystem::directory_iterator(arg))
                if (entry.path().extension() == ".json")
                    files.push_back(entry.path());
        } else {
            files.push_back(arg);
        }
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: %s <file.json | directory>...\n", argv[0]);
        return 2;
    }
    std::sort(files.begin(), files.end());

    Totals totals;
    for (const auto& path : files)
        runFile(path, totals);
    std::printf("%ld cases in %zu files: %ld failed (%d files)\n", totals.cases, files.size(), totals.failed,
                totals.filesFailed);
    return totals.failed ? 1 : 0;
}
