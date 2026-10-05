#include "core/snapshot.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

#include "core/cpc.h"

namespace tuxape {

namespace {

constexpr size_t kHeaderSize = 0x100;
constexpr size_t kPageSize = 0x10000;
constexpr char kSignature[] = "MV - SNA";
// Pages the "MEMn" chunks can name: the base RAM and eight expansion pages,
// 576K. A plain dump after the header can be longer.
constexpr int kChunkPages = 9;
constexpr int kMaxPages = Memory::kRamPages;

// Offsets in the header.
enum : size_t {
    kVersion = 0x10,
    kRegisters = 0x11,   // F A C B E D L H R I IFF1 IFF2 IXl IXh IYl IYh SPl SPh PCl PCh IM F' A' C' B' E' D' L' H'
    kPen = 0x2E,
    kInks = 0x2F,        // 17 of them
    kRomAndMode = 0x40,
    kRamBank = 0x41,
    kCrtcSelected = 0x42,
    kCrtcRegisters = 0x43,  // 18
    kUpperRom = 0x55,
    kPpiA = 0x56,
    kPpiB = 0x57,
    kPpiC = 0x58,
    kPpiControl = 0x59,
    kPsgSelected = 0x5A,
    kPsgRegisters = 0x5B,   // 16
    kDumpSize = 0x6B,       // in K, 16 bits
    // Version 2.
    kMachine = 0x6D,
    // Version 3.
    kMotor = 0x9C,
    kCrtcType = 0xA4,
    kCrtcHcc = 0xA9,
    kCrtcVcc = 0xAB,
    kCrtcVlc = 0xAC,
    kCrtcVtac = 0xAD,
    kCrtcHsc = 0xAE,
    kCrtcVsc = 0xAF,
    kCrtcFlags = 0xB0,      // 16 bits: bit 0 VSYNC, bit 1 HSYNC, bit 7 in vertical adjustment
    kInterruptCounter = 0xB3,
    kInterruptRequest = 0xB4,
};

bool isSnapshot(std::span<const uint8_t> data)
{
    return data.size() >= kHeaderSize && std::memcmp(data.data(), kSignature, 8) == 0;
}

// "MEMn" chunks are run-length encoded: E5 count value, with E5 00 for a
// literal E5. A chunk of exactly 64K is stored as it is.
void unpack(std::span<const uint8_t> in, uint8_t* out)
{
    if (in.size() == kPageSize) {
        std::memcpy(out, in.data(), kPageSize);
        return;
    }
    size_t o = 0;
    for (size_t i = 0; i < in.size() && o < kPageSize; ++i) {
        if (in[i] != 0xE5 || i + 1 >= in.size()) {
            out[o++] = in[i];
        } else if (in[++i] == 0) {
            out[o++] = 0xE5;
        } else if (i + 1 < in.size()) {
            const size_t count = std::min<size_t>(in[i], kPageSize - o);
            std::memset(out + o, in[++i], count);
            o += count;
        }
    }
}

std::vector<uint8_t> pack(const uint8_t* in)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < kPageSize;) {
        size_t run = 1;
        while (i + run < kPageSize && run < 255 && in[i + run] == in[i])
            ++run;
        if (run >= 3 || in[i] == 0xE5) {
            if (in[i] == 0xE5 && run == 1) {
                out.insert(out.end(), {0xE5, 0x00});
            } else {
                // A run of one or two E5 could not be told from a literal.
                out.insert(out.end(), {0xE5, static_cast<uint8_t>(run), in[i]});
            }
            i += run;
        } else {
            out.push_back(in[i++]);
        }
    }
    return out;
}

}  // namespace

bool snapshotInfo(std::span<const uint8_t> data, SnapshotInfo& info)
{
    if (!isSnapshot(data))
        return false;
    info.version = data[kVersion];
    info.ramKb = data[kDumpSize] | data[kDumpSize + 1] << 8;
    info.machine = info.version >= 2 && data[kMachine] <= 6 ? static_cast<SnapshotMachine>(data[kMachine])
                                                            : SnapshotMachine::Unknown;
    // With the memory in chunks the header may say 0.
    size_t pos = kHeaderSize + static_cast<size_t>(info.ramKb) * 1024;
    while (info.version >= 3 && pos + 8 <= data.size()) {
        const size_t length = data[pos + 4] | data[pos + 5] << 8 | data[pos + 6] << 16
                              | static_cast<size_t>(data[pos + 7]) << 24;
        if (std::memcmp(&data[pos], "MEM", 3) == 0 && data[pos + 3] >= '0' && data[pos + 3] <= '8')
            info.ramKb = std::max(info.ramKb, (data[pos + 3] - '0' + 1) * 64);
        pos += 8 + length;
    }
    return true;
}

bool loadSnapshot(Cpc& cpc, std::span<const uint8_t> data, std::string* error)
{
    auto fail = [&](const char* message) {
        if (error)
            *error = message;
        return false;
    };
    if (!isSnapshot(data))
        return fail("this is not a snapshot file");
    const int version = data[kVersion];
    if (version < 1 || version > 3)
        return fail("this snapshot is of a version that is not known");

    // Gather the memory first, so that a damaged file changes nothing.
    std::array<std::unique_ptr<uint8_t[]>, kMaxPages> pages;
    auto page = [&](int n) {
        if (!pages[n]) {
            pages[n] = std::make_unique<uint8_t[]>(kPageSize);
            std::memset(pages[n].get(), 0, kPageSize);
        }
        return pages[n].get();
    };
    const size_t dumpSize = static_cast<size_t>(data[kDumpSize] | data[kDumpSize + 1] << 8) * 1024;
    size_t pos = kHeaderSize;
    for (int n = 0; n < kMaxPages && static_cast<size_t>(n) * kPageSize < dumpSize; ++n) {
        // Short files exist; what is missing reads as 0.
        const size_t from = kHeaderSize + static_cast<size_t>(n) * kPageSize;
        if (from < data.size())
            std::memcpy(page(n), &data[from], std::min(kPageSize, data.size() - from));
    }
    pos += dumpSize;
    while (version >= 3 && pos + 8 <= data.size()) {
        const size_t length = data[pos + 4] | data[pos + 5] << 8 | data[pos + 6] << 16
                              | static_cast<size_t>(data[pos + 7]) << 24;
        const size_t available = std::min(length, data.size() - pos - 8);
        if (std::memcmp(&data[pos], "MEM", 3) == 0 && data[pos + 3] >= '0' && data[pos + 3] <= '8')
            unpack(data.subspan(pos + 8, available), page(data[pos + 3] - '0'));
        pos += 8 + length;
    }
    if (!pages[0])
        return fail("this snapshot holds no memory");
    int pageCount = 0;
    for (int n = 0; n < kMaxPages; ++n)
        if (pages[n])
            pageCount = n + 1;

    // Enough RAM to hold it.
    Memory& memory = cpc.memory();
    if (!memory.ramPage(pageCount - 1)) {
        // 128K, then a 256K expansion, then that with a 256K Silicon Disc
        // (WinAPE's "512K"), then the 4M board.
        const RamExpansion needed = pageCount <= 2   ? RamExpansion::Internal
                                    : pageCount <= 9 ? RamExpansion::Dk256
                                                     : RamExpansion::Yarek4M;
        memory.setRam(needed, memory.siliconDisc() || (pageCount > 5 && pageCount <= 9));
    }

    cpc.reset();
    for (int n = 0; n < kMaxPages; ++n)
        if (pages[n] && memory.ramPage(n))
            std::memcpy(memory.ramPage(n), pages[n].get(), kPageSize);

    auto word = [&](size_t at) { return static_cast<uint16_t>(data[at] | data[at + 1] << 8); };
    auto& cpu = cpc.cpu();
    const uint8_t* r = &data[kRegisters];
    cpu.reg[cpu.F] = r[0];
    cpu.reg[cpu.A] = r[1];
    cpu.reg[cpu.C] = r[2];
    cpu.reg[cpu.B] = r[3];
    cpu.reg[cpu.E] = r[4];
    cpu.reg[cpu.D] = r[5];
    cpu.reg[cpu.L] = r[6];
    cpu.reg[cpu.H] = r[7];
    cpu.r = r[8];
    cpu.i = r[9];
    cpu.iff1 = r[10] & 1;
    cpu.iff2 = r[11] & 1;
    cpu.ix = word(kRegisters + 12);
    cpu.iy = word(kRegisters + 14);
    cpu.sp = word(kRegisters + 16);
    cpu.pc = word(kRegisters + 18);
    cpu.im = r[20] > 2 ? 0 : r[20];
    cpu.af2 = static_cast<uint16_t>(r[22] << 8 | r[21]);
    cpu.bc2 = static_cast<uint16_t>(r[24] << 8 | r[23]);
    cpu.de2 = static_cast<uint16_t>(r[26] << 8 | r[25]);
    cpu.hl2 = static_cast<uint16_t>(r[28] << 8 | r[27]);
    // A halted CPU is saved stopped on its HALT, and finds it again.
    cpu.halted = false;

    Crtc& crtc = cpc.crtc();
    if (version >= 3 && data[kCrtcType] <= 4)
        crtc.setType(static_cast<CrtcType>(data[kCrtcType]));
    for (uint8_t n = 0; n < 18; ++n) {
        crtc.select(n);
        crtc.write(data[kCrtcRegisters + n]);
    }
    crtc.select(data[kCrtcSelected]);
    if (version >= 3) {
        const uint8_t flags = data[kCrtcFlags];
        crtc.restoreCounters(data[kCrtcHcc], data[kCrtcVcc], data[kCrtcVlc], data[kCrtcHsc], data[kCrtcVsc],
                             flags & 0x02, flags & 0x01);
    } else {
        crtc.restoreCounters(0, 0, 0, 0, 0, false, false);
    }

    const uint8_t pen = data[kPen] & 0x10 ? 16 : data[kPen] & 0x0F;
    cpc.gateArray().restore(pen, &data[kInks], data[kRomAndMode],
                            version >= 3 ? data[kInterruptCounter] : 0,
                            version >= 3 && (data[kInterruptRequest] & 1));
    memory.setRomEnables(cpc.gateArray().lowerRomEnabled(), cpc.gateArray().upperRomEnabled());
    memory.selectUpperRom(data[kUpperRom]);
    memory.selectRamBank(static_cast<uint8_t>(0xC0 | (data[kRamBank] & 0x3F)), 0x7F);

    // The control word first: setting it clears the output latches.
    Ppi& ppi = cpc.ppi();
    ppi.write(3, static_cast<uint8_t>(data[kPpiControl] | 0x80));
    ppi.write(0, data[kPpiA]);
    ppi.write(1, data[kPpiB]);
    ppi.write(2, data[kPpiC]);

    Psg& psg = cpc.psg();
    for (uint8_t n = 0; n < 16; ++n) {
        psg.selectRegister(n);
        psg.write(data[kPsgRegisters + n]);
    }
    psg.selectRegister(data[kPsgSelected]);

    if (version >= 3)
        cpc.fdc().writeMotor(data[kMotor] & 1, cpc.microseconds());
    return true;
}

std::vector<uint8_t> saveSnapshot(Cpc& cpc, SnapshotMachine machine)
{
    std::vector<uint8_t> out(kHeaderSize, 0);
    std::memcpy(out.data(), kSignature, 8);
    out[kVersion] = 3;

    auto word = [&](size_t at, uint16_t value) {
        out[at] = static_cast<uint8_t>(value);
        out[at + 1] = static_cast<uint8_t>(value >> 8);
    };
    const auto& cpu = cpc.cpu();
    uint8_t* r = &out[kRegisters];
    r[0] = cpu.reg[cpu.F];
    r[1] = cpu.reg[cpu.A];
    r[2] = cpu.reg[cpu.C];
    r[3] = cpu.reg[cpu.B];
    r[4] = cpu.reg[cpu.E];
    r[5] = cpu.reg[cpu.D];
    r[6] = cpu.reg[cpu.L];
    r[7] = cpu.reg[cpu.H];
    r[8] = cpu.r;
    r[9] = cpu.i;
    r[10] = cpu.iff1;
    r[11] = cpu.iff2;
    word(kRegisters + 12, cpu.ix);
    word(kRegisters + 14, cpu.iy);
    word(kRegisters + 16, cpu.sp);
    // The format has no place for "halted": the convention is to leave PC
    // on the HALT, which is then simply executed again.
    word(kRegisters + 18, static_cast<uint16_t>(cpu.halted ? cpu.pc - 1 : cpu.pc));
    r[20] = cpu.im;
    word(kRegisters + 21, cpu.af2);  // F' then A'
    word(kRegisters + 23, cpu.bc2);
    word(kRegisters + 25, cpu.de2);
    word(kRegisters + 27, cpu.hl2);

    const GateArray& ga = cpc.gateArray();
    out[kPen] = ga.selectedPen() == 16 ? 0x10 : ga.selectedPen();
    for (int i = 0; i < 17; ++i)
        out[kInks + i] = ga.ink(i);
    out[kRomAndMode] = ga.romAndMode();
    const Memory& memory = cpc.memory();
    out[kRamBank] = memory.ramBank() & 0x3F;
    out[kUpperRom] = memory.selectedUpperRom();

    const Crtc& crtc = cpc.crtc();
    out[kCrtcSelected] = crtc.selected();
    for (int n = 0; n < 18; ++n)
        out[kCrtcRegisters + n] = crtc.reg(n);
    out[kCrtcType] = static_cast<uint8_t>(crtc.type());
    out[kCrtcHcc] = crtc.hcc();
    out[kCrtcVcc] = crtc.vcc();
    out[kCrtcVlc] = crtc.vlc();
    out[kCrtcVtac] = crtc.vtac();
    out[kCrtcHsc] = crtc.hsc();
    out[kCrtcVsc] = crtc.vsc();
    out[kCrtcFlags] = static_cast<uint8_t>((crtc.vsync() ? 0x01 : 0) | (crtc.hsync() ? 0x02 : 0)
                                           | (crtc.inVerticalAdjust() ? 0x80 : 0));
    out[kInterruptCounter] = ga.interruptCounter();
    out[kInterruptRequest] = ga.interruptRequested();

    const Ppi& ppi = cpc.ppi();
    out[kPpiA] = ppi.latch(0);
    out[kPpiB] = ppi.latch(1);
    out[kPpiC] = ppi.latch(2);
    out[kPpiControl] = ppi.control();
    const Psg& psg = cpc.psg();
    out[kPsgSelected] = psg.selected();
    for (int n = 0; n < 16; ++n)
        out[kPsgRegisters + n] = psg.reg(n);

    out[kMachine] = static_cast<uint8_t>(machine);
    out[kMotor] = cpc.fdc().motor();

    // 64K and 128K go after the header, where every program expects them.
    // More than that only fits in chunks.
    int pageCount = 1;
    for (int n = 1; n < kChunkPages; ++n)
        if (memory.ramPage(n))
            pageCount = n + 1;
    if (pageCount <= 2) {
        word(kDumpSize, static_cast<uint16_t>(pageCount * 64));
        for (int n = 0; n < pageCount; ++n)
            out.insert(out.end(), memory.ramPage(n), memory.ramPage(n) + kPageSize);
    } else {
        for (int n = 0; n < pageCount; ++n) {
            if (!memory.ramPage(n))
                continue;
            const std::vector<uint8_t> packed = pack(memory.ramPage(n));
            const uint32_t length = static_cast<uint32_t>(packed.size());
            out.insert(out.end(), {'M', 'E', 'M', static_cast<uint8_t>('0' + n), static_cast<uint8_t>(length),
                                   static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length >> 16),
                                   static_cast<uint8_t>(length >> 24)});
            out.insert(out.end(), packed.begin(), packed.end());
        }
    }
    return out;
}

}  // namespace tuxape
