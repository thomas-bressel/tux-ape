// SNA snapshots: what is written can be read back, the header is laid out
// as the format says, and files from other programs are accepted.
//
//   snapshot [folder of .sna files to try]

#include <cstring>
#include <filesystem>
#include <vector>

#include "check.h"
#include "core/cpc.h"
#include "core/files.h"
#include "core/snapshot.h"

namespace {

using namespace tuxape;

// A machine in a state with something recognisable in every chip.
void dress(Cpc& cpc)
{
    cpc.memory().setRomEnables(false, false);
    for (int page = 0; page < Memory::kRamPages; ++page) {
        uint8_t* ram = cpc.memory().ramPage(page);
        if (!ram)
            continue;
        for (unsigned a = 0x100; a < 0x10000; a += 251)
            ram[a] = static_cast<uint8_t>(a * 7 + page);
        // Every shape the run-length coding has to tell apart.
        const uint8_t tricky[] = {0x11, 0xE5, 0x22, 0xE5, 0xE5, 0x33, 0xE5, 0xE5, 0xE5, 0x44};
        std::memcpy(ram + 0x8000 + page, tricky, sizeof tricky);
        std::memset(ram + 0x9000, 0xE5, 700);
        std::memset(ram + 0xA000, 0x5A, 300);
    }
    // Gate Array: inks, border, mode 1 with both ROMs off.
    for (uint8_t pen = 0; pen < 16; ++pen) {
        cpc.out(0x7F00, pen);
        cpc.out(0x7F00, static_cast<uint8_t>(0x40 | (pen + 3)));
    }
    cpc.out(0x7F00, 0x10);
    cpc.out(0x7F00, 0x40 | 0x15);
    cpc.out(0x7F00, 0x05);  // pen 5 left selected
    cpc.out(0x7F00, 0x80 | 0x0D);
    cpc.out(0x7F00, 0xC2);  // RAM configuration 2
    cpc.out(0xDF00, 7);
    const uint8_t crtc[] = {63, 40, 46, 0x8E, 38, 0, 25, 30, 0, 7, 0x10, 0x05, 0x30, 0x40, 0x01, 0x23};
    for (uint8_t r = 0; r < sizeof crtc; ++r) {
        cpc.out(0xBC00, r);
        cpc.out(0xBD00, crtc[r]);
    }
    cpc.out(0xBC00, 12);
    for (uint8_t r = 0; r < 14; ++r) {
        cpc.psg().selectRegister(r);
        cpc.psg().write(static_cast<uint8_t>(0x11 * r + 1));
    }
    cpc.psg().selectRegister(9);
    cpc.ppi().write(3, 0x82);
    cpc.ppi().write(0, 0x5A);
    cpc.ppi().write(2, 0x0B);

    // Let it run on the NOPs RAM is full of, so that the counters are
    // somewhere in the middle of a frame.
    auto& cpu = cpc.cpu();
    cpu.pc = 0x0000;
    cpc.run(123457);
    cpu.setAf(0x1234);
    cpu.setBc(0x5678);
    cpu.setDe(0x9ABC);
    cpu.setHl(0xDEF0);
    cpu.af2 = 0x0FED;
    cpu.bc2 = 0xCBA9;
    cpu.de2 = 0x8765;
    cpu.hl2 = 0x4321;
    cpu.ix = 0x1357;
    cpu.iy = 0x2468;
    cpu.sp = 0xBFF0;
    cpu.pc = 0x4321;
    cpu.i = 0x3C;
    cpu.r = 0xA5;
    cpu.im = 1;
    cpu.iff1 = true;
    cpu.iff2 = false;
}

void roundTrip(RamExpansion expansion)
{
    Cpc first;
    first.memory().setRam(expansion, false);
    dress(first);
    const std::vector<uint8_t> saved = saveSnapshot(first, SnapshotMachine::Cpc6128);

    SnapshotInfo info;
    CHECK(snapshotInfo(saved, info));
    CHECK_EQ(info.version, 3);
    CHECK_EQ(info.ramKb, first.memory().ramSizeKb());
    CHECK(info.machine == SnapshotMachine::Cpc6128);

    // A stock machine; RAM is added to it as the snapshot needs, and never
    // taken away, so the smallest case starts small.
    Cpc second;
    if (expansion == RamExpansion::None)
        second.memory().setRam(RamExpansion::None, false);
    std::string error;
    CHECK(loadSnapshot(second, saved, &error));
    CHECK(error.empty());
    CHECK(second.memory().ramSizeKb() >= first.memory().ramSizeKb());

    for (int page = 0; page < Memory::kRamPages; ++page) {
        const uint8_t* a = first.memory().ramPage(page);
        const uint8_t* b = second.memory().ramPage(page);
        if (a && page < 9) {
            CHECK(b != nullptr);
            if (b && std::memcmp(a, b, 0x10000) != 0) {
                std::printf("RAM page %d differs after a round trip\n", page);
                ++g_failures;
            }
        }
    }
    const auto& a = first.cpu();
    const auto& b = second.cpu();
    CHECK_EQ(b.af(), 0x1234);
    CHECK_EQ(b.bc(), 0x5678);
    CHECK_EQ(b.de(), 0x9ABC);
    CHECK_EQ(b.hl(), 0xDEF0);
    CHECK_EQ(b.af2, 0x0FED);
    CHECK_EQ(b.bc2, 0xCBA9);
    CHECK_EQ(b.de2, 0x8765);
    CHECK_EQ(b.hl2, 0x4321);
    CHECK_EQ(b.ix, 0x1357);
    CHECK_EQ(b.iy, 0x2468);
    CHECK_EQ(b.sp, 0xBFF0);
    CHECK_EQ(b.pc, 0x4321);
    CHECK_EQ(b.i, 0x3C);
    CHECK_EQ(b.r, 0xA5);
    CHECK_EQ(b.im, 1);
    CHECK(b.iff1 && !b.iff2);
    CHECK_EQ(a.pc, b.pc);

    for (int pen = 0; pen < 17; ++pen)
        CHECK_EQ(second.gateArray().ink(pen), first.gateArray().ink(pen));
    CHECK_EQ(second.gateArray().ink(16), 0x15);
    CHECK_EQ(second.gateArray().selectedPen(), 5);
    CHECK_EQ(second.gateArray().requestedMode(), 1);
    CHECK(!second.memory().lowerRomEnabled() && !second.memory().upperRomEnabled());
    CHECK_EQ(second.gateArray().interruptCounter(), first.gateArray().interruptCounter());
    CHECK_EQ(second.memory().selectedUpperRom(), 7);
    // A 64K machine has no banking register to keep the value in.
    CHECK_EQ(second.memory().ramBank() & 0x3F, expansion == RamExpansion::None ? 0x00 : 0x02);
    // RAM configuration 2 puts the expansion's four blocks everywhere.
    CHECK_EQ(second.memory().read(0x8001), first.memory().read(0x8001));

    for (int r = 0; r < 16; ++r)
        CHECK_EQ(second.crtc().reg(r), first.crtc().reg(r));
    CHECK_EQ(second.crtc().selected(), 12);
    CHECK_EQ(second.crtc().hcc(), first.crtc().hcc());
    CHECK_EQ(second.crtc().vcc(), first.crtc().vcc());
    CHECK_EQ(second.crtc().vlc(), first.crtc().vlc());
    CHECK_EQ(second.crtc().vsync(), first.crtc().vsync());
    CHECK_EQ(second.crtc().hsync(), first.crtc().hsync());

    for (int r = 0; r < 14; ++r)
        CHECK_EQ(second.psg().reg(r), first.psg().reg(r));
    CHECK_EQ(second.psg().selected(), 9);
    CHECK_EQ(second.ppi().control(), 0x82);
    CHECK_EQ(second.ppi().latch(0), 0x5A);
    CHECK_EQ(second.ppi().latch(2), 0x0B);

    // And what was read writes out the same.
    const std::vector<uint8_t> again = saveSnapshot(second, SnapshotMachine::Cpc6128);
    CHECK_EQ(again.size(), saved.size());
    for (size_t i = 0; i < saved.size() && i < again.size(); ++i) {
        if (saved[i] != again[i]) {
            std::printf("saved again, byte %zx is %02X, was %02X (%dK machine)\n", i, again[i], saved[i],
                        first.memory().ramSizeKb());
            ++g_failures;
            break;
        }
    }
}

void layout()
{
    Cpc cpc;
    dress(cpc);
    const std::vector<uint8_t> s = saveSnapshot(cpc, SnapshotMachine::Cpc664);
    CHECK_EQ(s.size(), 0x100 + 128 * 1024);
    CHECK(std::memcmp(s.data(), "MV - SNA", 8) == 0);
    CHECK_EQ(s[0x10], 3);
    CHECK_EQ(s[0x11], 0x34);  // F
    CHECK_EQ(s[0x12], 0x12);  // A
    CHECK_EQ(s[0x13], 0x78);  // C
    CHECK_EQ(s[0x14], 0x56);  // B
    CHECK_EQ(s[0x17], 0xF0);  // L
    CHECK_EQ(s[0x18], 0xDE);  // H
    CHECK_EQ(s[0x19], 0xA5);  // R
    CHECK_EQ(s[0x1A], 0x3C);  // I
    CHECK_EQ(s[0x1B], 1);     // IFF1
    CHECK_EQ(s[0x1C], 0);     // IFF2
    CHECK_EQ(s[0x1D] | s[0x1E] << 8, 0x1357);  // IX
    CHECK_EQ(s[0x21] | s[0x22] << 8, 0xBFF0);  // SP
    CHECK_EQ(s[0x23] | s[0x24] << 8, 0x4321);  // PC
    CHECK_EQ(s[0x25], 1);     // IM
    CHECK_EQ(s[0x26], 0xED);  // F'
    CHECK_EQ(s[0x27], 0x0F);  // A'
    CHECK_EQ(s[0x2E], 5);     // selected pen
    CHECK_EQ(s[0x2F], 3);     // ink 0
    CHECK_EQ(s[0x3F], 0x15);  // border
    CHECK_EQ(s[0x40], 0x0D);
    CHECK_EQ(s[0x41], 0x02);
    CHECK_EQ(s[0x42], 12);
    CHECK_EQ(s[0x43], 63);    // R0
    CHECK_EQ(s[0x4A], 30);    // R7
    CHECK_EQ(s[0x55], 7);
    CHECK_EQ(s[0x56], 0x5A);
    CHECK_EQ(s[0x58], 0x0B);
    CHECK_EQ(s[0x59], 0x82);
    CHECK_EQ(s[0x5A], 9);
    CHECK_EQ(s[0x5B], 1);     // PSG register 0
    CHECK_EQ(s[0x6B] | s[0x6C] << 8, 128);
    CHECK_EQ(s[0x6D], 1);     // CPC 664
    CHECK_EQ(s[0xA4], 0);     // CRTC type
    CHECK_EQ(s[0x100 + 0x8001], 0xE5);
}

void olderVersions()
{
    // Version 1: registers and 64K, nothing more.
    std::vector<uint8_t> s(0x100 + 0x10000, 0);
    std::memcpy(s.data(), "MV - SNA", 8);
    s[0x10] = 1;
    s[0x12] = 0x77;           // A
    s[0x23] = 0x00;
    s[0x24] = 0x80;           // PC = 8000
    s[0x40] = 0x8C | 0x01;    // both ROMs off, mode 1, as the port would be written
    s[0x43 + 1] = 40;
    s[0x43 + 6] = 25;
    s[0x59] = 0x82;
    s[0x6B] = 64;
    s[0x100 + 0x8000] = 0xC9;
    Cpc cpc;
    CHECK(loadSnapshot(cpc, s));
    CHECK_EQ(cpc.cpu().pc, 0x8000);
    CHECK_EQ(cpc.cpu().reg[cpc.cpu().A], 0x77);
    CHECK_EQ(cpc.memory().read(0x8000), 0xC9);
    CHECK_EQ(cpc.gateArray().requestedMode(), 1);
    CHECK_EQ(cpc.crtc().reg(1), 40);

    // Cut short: what is there is used.
    s.resize(0x100 + 0x9000);
    Cpc other;
    CHECK(loadSnapshot(other, s));
    CHECK_EQ(other.memory().read(0x8000), 0xC9);
}

void halted()
{
    // A machine stopped on a HALT with interrupts off must come back
    // stopped (the acid tests' "snaphalt").
    Cpc cpc;
    cpc.memory().setRomEnables(false, false);
    cpc.memory().write(0x4000, 0xF3);  // DI
    cpc.memory().write(0x4001, 0x76);  // HALT
    cpc.memory().write(0x4002, 0x3C);  // INC A: must never run
    cpc.cpu().pc = 0x4000;
    cpc.cpu().reg[cpc.cpu().A] = 0;
    cpc.run(1000);
    CHECK(cpc.cpu().halted);
    const std::vector<uint8_t> s = saveSnapshot(cpc);
    CHECK_EQ(s[0x23] | s[0x24] << 8, 0x4001);

    Cpc other;
    CHECK(loadSnapshot(other, s));
    other.run(1000);
    CHECK(other.cpu().halted);
    CHECK_EQ(other.cpu().reg[other.cpu().A], 0);
}

void bigDump()
{
    // 4M after the header, as JavaCPC writes it.
    std::vector<uint8_t> s(0x100 + 65 * 0x10000, 0);
    std::memcpy(s.data(), "MV - SNA", 8);
    s[0x10] = 1;
    s[0x6B] = static_cast<uint8_t>(4160 & 0xFF);
    s[0x6C] = static_cast<uint8_t>(4160 >> 8);
    for (int page = 0; page < 65; ++page)
        s[0x100 + static_cast<size_t>(page) * 0x10000 + 0x1234] = static_cast<uint8_t>(page + 1);
    Cpc cpc;
    CHECK(loadSnapshot(cpc, s));
    CHECK_EQ(cpc.memory().ramSizeKb(), 4160);
    for (int page : {0, 1, 8, 9, 40, 64}) {
        CHECK(cpc.memory().ramPage(page) != nullptr);
        if (cpc.memory().ramPage(page))
            CHECK_EQ(cpc.memory().ramPage(page)[0x1234], page + 1);
    }
}

void refusals()
{
    Cpc cpc;
    cpc.cpu().pc = 0x1234;
    std::string error;
    std::vector<uint8_t> junk(0x200, 0x55);
    CHECK(!loadSnapshot(cpc, junk, &error));
    CHECK(!error.empty());
    CHECK_EQ(cpc.cpu().pc, 0x1234);

    std::vector<uint8_t> s(0x100, 0);
    std::memcpy(s.data(), "MV - SNA", 8);
    s[0x10] = 9;
    CHECK(!loadSnapshot(cpc, s, &error));
    s[0x10] = 3;  // a header and no memory at all
    CHECK(!loadSnapshot(cpc, s, &error));
    CHECK_EQ(cpc.cpu().pc, 0x1234);
    SnapshotInfo info;
    CHECK(!snapshotInfo(junk, info));
}

// Snapshots made by other emulators, if a folder of them is given.
void foreign(const std::filesystem::path& folder)
{
    std::error_code ec;
    int tried = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.path().extension() != ".sna")
            continue;
        const auto data = readFile(entry.path());
        SnapshotInfo info;
        if (!data || !snapshotInfo(*data, info)) {
            std::printf("%s: not read as a snapshot\n", entry.path().filename().string().c_str());
            ++g_failures;
            continue;
        }
        Cpc cpc;
        std::string error;
        const bool ok = loadSnapshot(cpc, *data, &error);
        std::printf("  %-28s version %d, %4dK: %s\n", entry.path().filename().string().c_str(), info.version,
                    info.ramKb, ok ? "loaded" : error.c_str());
        if (!ok)
            ++g_failures;
        else
            cpc.run(20000);  // and it runs on without tripping anything
        ++tried;
    }
    std::printf("%d snapshot(s) from other programs tried\n", tried);
}

}  // namespace

int main(int argc, char* argv[])
{
    roundTrip(RamExpansion::None);
    roundTrip(RamExpansion::Internal);
    roundTrip(RamExpansion::Dk256);
    layout();
    olderVersions();
    halted();
    bigDump();
    refusals();
    if (argc > 1)
        foreign(argv[1]);
    return checkSummary("snapshot");
}
