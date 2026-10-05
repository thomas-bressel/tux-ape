#pragma once

#include <array>
#include <cstdint>

namespace tuxape {

namespace z80_detail {

// Sign, zero and the two undocumented bits (3 and 5) of every byte value,
// with and without parity.
struct FlagTables {
    std::array<uint8_t, 256> sz53{};
    std::array<uint8_t, 256> sz53p{};

    constexpr FlagTables()
    {
        for (int v = 0; v < 256; ++v) {
            int bits = 0;
            for (int b = 0; b < 8; ++b)
                bits += (v >> b) & 1;
            const uint8_t base = static_cast<uint8_t>((v & 0xA8) | (v == 0 ? 0x40 : 0));
            sz53[v] = base;
            sz53p[v] = static_cast<uint8_t>(base | ((bits & 1) ? 0 : 0x04));
        }
    }
};

inline constexpr FlagTables flagTables{};

}  // namespace z80_detail

// Zilog Z80 CPU (NMOS), including the undocumented instructions and flags,
// the internal MEMPTR register and the flag latch that SCF/CCF expose.
//
// The core runs one instruction per step() but performs every bus access in
// the order, and with the internal delays, of the real chip's machine cycles.
// Time itself does not live here: each access is a call on the Bus, which
// advances the machine clock. That lets the host insert wait states (the CPC
// stretches every machine cycle to line up with the Gate Array) without the
// CPU knowing.
//
// Bus must provide:
//   uint8_t m1(uint16_t addr)               opcode fetch            4 T-states
//   uint8_t read(uint16_t addr)             memory read             3 T-states
//   void    write(uint16_t addr, uint8_t v) memory write            3 T-states
//   uint8_t in(uint16_t port)               I/O read                4 T-states
//   void    out(uint16_t port, uint8_t v)   I/O write               4 T-states
//   void    tick(int tstates)               T-states without bus access
//   bool    irq()                           level of the INT line
//   uint8_t irqAck()                        interrupt acknowledge   6 T-states,
//                                           returns the data bus value
template <class Bus>
class Z80 {
public:
    enum Flag : uint8_t {
        CF = 0x01,
        NF = 0x02,
        PF = 0x04,
        XF = 0x08,
        HF = 0x10,
        YF = 0x20,
        ZF = 0x40,
        SF = 0x80,
    };

    // Index into reg[]. The order is the 3-bit register field of the opcodes;
    // slot 6, which encodes "(HL)" there, holds F.
    enum Reg : int { B, C, D, E, H, L, F, A };

    uint8_t reg[8];
    uint16_t af2, bc2, de2, hl2;  // alternate register set
    uint16_t ix, iy, sp, pc;
    uint16_t wz;  // MEMPTR
    uint8_t i, r;
    uint8_t im;
    bool iff1, iff2;
    bool halted;

    explicit Z80(Bus& bus)
        : bus_(bus)
    {
        powerOn();
    }

    // State after power is applied.
    void powerOn()
    {
        for (uint8_t& v : reg)
            v = 0xFF;
        af2 = bc2 = de2 = hl2 = 0xFFFF;
        ix = iy = 0xFFFF;
        wz = 0;
        reset();
    }

    // Effect of the RESET line: the general registers keep their contents.
    void reset()
    {
        pc = 0;
        sp = 0xFFFF;
        reg[A] = reg[F] = 0xFF;
        i = r = 0;
        im = 0;
        iff1 = iff2 = false;
        halted = false;
        eiDelay_ = false;
        ldAIR_ = false;
        nmiPending_ = false;
        q_ = prevQ_ = 0;
        idx_ = None;
    }

    // NMI is edge triggered: call once per falling edge.
    void nmi() { nmiPending_ = true; }

    // True if the next step() will respond to an interrupt instead of
    // running the instruction at PC.
    bool interruptDue() const { return nmiPending_ || (!eiDelay_ && iff1 && bus_.irq()); }

    uint16_t af() const { return static_cast<uint16_t>(reg[A] << 8 | reg[F]); }
    uint16_t bc() const { return static_cast<uint16_t>(reg[B] << 8 | reg[C]); }
    uint16_t de() const { return static_cast<uint16_t>(reg[D] << 8 | reg[E]); }
    uint16_t hl() const { return static_cast<uint16_t>(reg[H] << 8 | reg[L]); }
    void setAf(uint16_t v) { reg[A] = v >> 8, reg[F] = v & 0xFF; }
    void setBc(uint16_t v) { reg[B] = v >> 8, reg[C] = v & 0xFF; }
    void setDe(uint16_t v) { reg[D] = v >> 8, reg[E] = v & 0xFF; }
    void setHl(uint16_t v) { reg[H] = v >> 8, reg[L] = v & 0xFF; }

    // Internal latches that outlive an instruction. They are part of the CPU
    // state: a snapshot that drops them is not exact.
    struct Latches {
        bool eiDelay;  // the previous instruction was EI
        bool ldAIR;    // the previous instruction was LD A,I or LD A,R
        uint8_t q;     // F as left by the previous instruction, 0 if untouched
    };
    Latches latches() const { return {eiDelay_, ldAIR_, q_}; }
    void setLatches(const Latches& l)
    {
        eiDelay_ = l.eiDelay;
        ldAIR_ = l.ldAIR;
        q_ = l.q;
    }

    // Runs one instruction, or one interrupt response, or one halted cycle.
    void step()
    {
        const bool canInterrupt = !eiDelay_;
        eiDelay_ = false;
        const bool clearParity = ldAIR_;
        ldAIR_ = false;
        prevQ_ = q_;
        q_ = 0;

        if (nmiPending_) {
            nmiPending_ = false;
            if (clearParity)
                reg[F] &= ~PF;
            halted = false;
            iff1 = false;
            bus_.m1(pc);
            bumpR();
            bus_.tick(1);
            push(pc);
            pc = wz = 0x0066;
            return;
        }

        if (canInterrupt && iff1 && bus_.irq()) {
            // On NMOS parts LD A,I and LD A,R report IFF2 wrongly when an
            // interrupt is accepted right after them.
            if (clearParity)
                reg[F] &= ~PF;
            halted = false;
            iff1 = iff2 = false;
            const uint8_t data = bus_.irqAck();
            bumpR();
            switch (im) {
            case 0:
                // The interrupting device supplies the instruction. In
                // practice this is a restart: the CPC's idle bus reads FF.
                idx_ = None;
                execute(data);
                break;
            case 1:
                bus_.tick(1);
                push(pc);
                pc = wz = 0x0038;
                break;
            default: {
                bus_.tick(1);
                push(pc);
                const uint16_t vector = static_cast<uint16_t>(i << 8 | data);
                const uint8_t lo = bus_.read(vector);
                const uint8_t hi = bus_.read(vector + 1);
                pc = wz = static_cast<uint16_t>(hi << 8 | lo);
                break;
            }
            }
            return;
        }

        if (halted) {
            bus_.m1(pc);
            bumpR();
            return;
        }

        idx_ = None;
        uint8_t op = fetch();
        while (op == 0xDD || op == 0xFD) {
            idx_ = op == 0xDD ? IX : IY;
            // A prefix behaves as an instruction that leaves the flags alone.
            prevQ_ = 0;
            op = fetch();
        }
        execute(op);
    }

private:
    enum Index : uint8_t { None, IX, IY };

    static constexpr const std::array<uint8_t, 256>& sz53 = z80_detail::flagTables.sz53;
    static constexpr const std::array<uint8_t, 256>& sz53p = z80_detail::flagTables.sz53p;

    Bus& bus_;
    Index idx_ = None;
    bool eiDelay_ = false;     // the previous instruction was EI
    bool ldAIR_ = false;       // the previous instruction was LD A,I or LD A,R
    bool nmiPending_ = false;
    uint8_t q_ = 0;            // F as left by this instruction, 0 if untouched
    uint8_t prevQ_ = 0;        // the same for the previous instruction

    // ---- fetch and stack helpers -------------------------------------------

    void bumpR() { r = static_cast<uint8_t>((r & 0x80) | ((r + 1) & 0x7F)); }

    uint8_t fetch()
    {
        const uint8_t v = bus_.m1(pc++);
        bumpR();
        return v;
    }

    uint8_t imm8() { return bus_.read(pc++); }

    uint16_t imm16()
    {
        const uint8_t lo = bus_.read(pc++);
        const uint8_t hi = bus_.read(pc++);
        return static_cast<uint16_t>(hi << 8 | lo);
    }

    void push(uint16_t v)
    {
        bus_.write(--sp, v >> 8);
        bus_.write(--sp, v & 0xFF);
    }

    uint16_t pop()
    {
        const uint8_t lo = bus_.read(sp++);
        const uint8_t hi = bus_.read(sp++);
        return static_cast<uint16_t>(hi << 8 | lo);
    }

    // ---- operands, honouring a DD/FD prefix --------------------------------

    void setF(uint8_t v) { reg[F] = q_ = v; }

    uint16_t hlx() const { return idx_ == None ? hl() : idx_ == IX ? ix : iy; }

    void setHlx(uint16_t v)
    {
        if (idx_ == None)
            setHl(v);
        else if (idx_ == IX)
            ix = v;
        else
            iy = v;
    }

    // Register named by a 3-bit opcode field; H and L become the halves of
    // the index register when prefixed.
    uint8_t regX(int n) const
    {
        if (idx_ != None && (n == H || n == L)) {
            const uint16_t v = idx_ == IX ? ix : iy;
            return n == H ? v >> 8 : v & 0xFF;
        }
        return reg[n];
    }

    void setRegX(int n, uint8_t v)
    {
        if (idx_ != None && (n == H || n == L)) {
            uint16_t& x = idx_ == IX ? ix : iy;
            x = n == H ? static_cast<uint16_t>((x & 0x00FF) | v << 8)
                       : static_cast<uint16_t>((x & 0xFF00) | v);
            return;
        }
        reg[n] = v;
    }

    // Address of the "(HL)" operand: HL itself, or IX/IY plus the
    // displacement byte that follows the opcode.
    uint16_t memOperand()
    {
        if (idx_ == None)
            return hl();
        const int8_t d = static_cast<int8_t>(imm8());
        bus_.tick(5);
        wz = static_cast<uint16_t>((idx_ == IX ? ix : iy) + d);
        return wz;
    }

    uint16_t rp(int p) const
    {
        switch (p) {
        case 0: return bc();
        case 1: return de();
        case 2: return hlx();
        default: return sp;
        }
    }

    void setRp(int p, uint16_t v)
    {
        switch (p) {
        case 0: setBc(v); break;
        case 1: setDe(v); break;
        case 2: setHlx(v); break;
        default: sp = v; break;
        }
    }

    bool cond(int cc) const
    {
        const uint8_t f = reg[F];
        switch (cc) {
        case 0: return !(f & ZF);
        case 1: return f & ZF;
        case 2: return !(f & CF);
        case 3: return f & CF;
        case 4: return !(f & PF);
        case 5: return f & PF;
        case 6: return !(f & SF);
        default: return f & SF;
        }
    }

    // ---- arithmetic --------------------------------------------------------

    void add8(uint8_t v, unsigned carry)
    {
        const unsigned a = reg[A];
        const unsigned res = a + v + carry;
        reg[A] = static_cast<uint8_t>(res);
        setF(static_cast<uint8_t>(sz53[res & 0xFF] | ((res >> 8) & CF) | ((a ^ v ^ res) & HF)
                                  | (((a ^ res) & (v ^ res) & 0x80) >> 5)));
    }

    // Returns A - v - carry and sets the flags; the caller decides whether to
    // keep the result (SUB/SBC) or drop it (CP).
    uint8_t sub8(uint8_t v, unsigned carry)
    {
        const unsigned a = reg[A];
        const unsigned res = a - v - carry;
        setF(static_cast<uint8_t>(sz53[res & 0xFF] | ((res >> 8) & CF) | NF | ((a ^ v ^ res) & HF)
                                  | (((a ^ v) & (a ^ res) & 0x80) >> 5)));
        return static_cast<uint8_t>(res);
    }

    void alu(int op, uint8_t v)
    {
        switch (op) {
        case 0: add8(v, 0); break;
        case 1: add8(v, reg[F] & CF); break;
        case 2: reg[A] = sub8(v, 0); break;
        case 3: reg[A] = sub8(v, reg[F] & CF); break;
        case 4: reg[A] &= v; setF(sz53p[reg[A]] | HF); break;
        case 5: reg[A] ^= v; setF(sz53p[reg[A]]); break;
        case 6: reg[A] |= v; setF(sz53p[reg[A]]); break;
        default:
            // CP takes the undocumented bits from the operand, not the result.
            sub8(v, 0);
            setF(static_cast<uint8_t>((reg[F] & ~(XF | YF)) | (v & (XF | YF))));
            break;
        }
    }

    uint8_t inc8(uint8_t v)
    {
        ++v;
        setF(static_cast<uint8_t>((reg[F] & CF) | sz53[v] | ((v & 0x0F) == 0 ? HF : 0)
                                  | (v == 0x80 ? PF : 0)));
        return v;
    }

    uint8_t dec8(uint8_t v)
    {
        --v;
        setF(static_cast<uint8_t>((reg[F] & CF) | NF | sz53[v] | ((v & 0x0F) == 0x0F ? HF : 0)
                                  | (v == 0x7F ? PF : 0)));
        return v;
    }

    uint16_t add16(uint16_t a, uint16_t v)
    {
        const unsigned res = a + v;
        wz = a + 1;
        setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF)) | ((res >> 16) & CF)
                                  | (((a ^ v ^ res) >> 8) & HF) | ((res >> 8) & (XF | YF))));
        return static_cast<uint16_t>(res);
    }

    void adc16(uint16_t v)
    {
        const unsigned a = hl();
        const unsigned res = a + v + (reg[F] & CF);
        wz = static_cast<uint16_t>(a + 1);
        setF(static_cast<uint8_t>(((res >> 16) & CF) | ((res >> 8) & (SF | XF | YF))
                                  | ((res & 0xFFFF) ? 0 : ZF) | (((a ^ v ^ res) >> 8) & HF)
                                  | (((a ^ res) & (v ^ res) & 0x8000) >> 13)));
        setHl(static_cast<uint16_t>(res));
    }

    void sbc16(uint16_t v)
    {
        const unsigned a = hl();
        const unsigned res = a - v - (reg[F] & CF);
        wz = static_cast<uint16_t>(a + 1);
        setF(static_cast<uint8_t>(((res >> 16) & CF) | NF | ((res >> 8) & (SF | XF | YF))
                                  | ((res & 0xFFFF) ? 0 : ZF) | (((a ^ v ^ res) >> 8) & HF)
                                  | (((a ^ v) & (a ^ res) & 0x8000) >> 13)));
        setHl(static_cast<uint16_t>(res));
    }

    void daa()
    {
        const uint8_t a = reg[A];
        uint8_t fix = 0;
        uint8_t carry = reg[F] & CF;
        if ((reg[F] & HF) || (a & 0x0F) > 9)
            fix = 0x06;
        if (carry || a > 0x99) {
            fix |= 0x60;
            carry = CF;
        }
        const uint8_t res = (reg[F] & NF) ? a - fix : a + fix;
        reg[A] = res;
        setF(static_cast<uint8_t>((reg[F] & NF) | carry | sz53p[res] | ((a ^ res) & HF)));
    }

    // Rotates, shifts and bit set/reset of the CB page (not BIT).
    uint8_t cbOp(uint8_t op, uint8_t v)
    {
        const int n = (op >> 3) & 7;
        switch (op >> 6) {
        case 2: return v & ~(1 << n);
        case 3: return v | (1 << n);
        }
        uint8_t carry;
        switch (n) {
        case 0: carry = v >> 7; v = static_cast<uint8_t>(v << 1 | carry); break;                    // RLC
        case 1: carry = v & 1; v = static_cast<uint8_t>(v >> 1 | carry << 7); break;                // RRC
        case 2: carry = v >> 7; v = static_cast<uint8_t>(v << 1 | (reg[F] & CF)); break;            // RL
        case 3: carry = v & 1; v = static_cast<uint8_t>(v >> 1 | (reg[F] & CF) << 7); break;        // RR
        case 4: carry = v >> 7; v = static_cast<uint8_t>(v << 1); break;                            // SLA
        case 5: carry = v & 1; v = static_cast<uint8_t>((v >> 1) | (v & 0x80)); break;              // SRA
        case 6: carry = v >> 7; v = static_cast<uint8_t>(v << 1 | 1); break;                        // SLL
        default: carry = v & 1; v = v >> 1; break;                                                  // SRL
        }
        setF(sz53p[v] | carry);
        return v;
    }

    // BIT n: the undocumented bits come from `xy`, which is the tested
    // register, or the high byte of the address for memory operands.
    void bitTest(uint8_t v, int n, uint8_t xy)
    {
        const uint8_t t = v & (1 << n);
        setF(static_cast<uint8_t>((reg[F] & CF) | HF | (t & SF) | (t ? 0 : ZF | PF) | (xy & (XF | YF))));
    }

    // ---- instruction pages -------------------------------------------------

    void execute(uint8_t op)
    {
        const int y = (op >> 3) & 7;
        const int z = op & 7;
        const int p = y >> 1;

        if ((op & 0xC0) == 0x40) {
            if (op == 0x76) {  // HALT
                halted = true;
                return;
            }
            // LD r,r'. When one side is memory the other is always a plain
            // register, even under a DD/FD prefix.
            if (z == 6)
                reg[y] = bus_.read(memOperand());
            else if (y == 6)
                bus_.write(memOperand(), reg[z]);
            else
                setRegX(y, regX(z));
            return;
        }
        if ((op & 0xC0) == 0x80) {
            alu(y, z == 6 ? bus_.read(memOperand()) : regX(z));
            return;
        }

        switch (op) {
        case 0x00:  // NOP
            break;

        case 0x01: case 0x11: case 0x21: case 0x31:  // LD rp,nn
            setRp(p, imm16());
            break;

        case 0x02:  // LD (BC),A
            bus_.write(bc(), reg[A]);
            wz = static_cast<uint16_t>(reg[A] << 8 | ((bc() + 1) & 0xFF));
            break;
        case 0x12:  // LD (DE),A
            bus_.write(de(), reg[A]);
            wz = static_cast<uint16_t>(reg[A] << 8 | ((de() + 1) & 0xFF));
            break;
        case 0x0A:  // LD A,(BC)
            reg[A] = bus_.read(bc());
            wz = bc() + 1;
            break;
        case 0x1A:  // LD A,(DE)
            reg[A] = bus_.read(de());
            wz = de() + 1;
            break;

        case 0x03: case 0x13: case 0x23: case 0x33:  // INC rp
            bus_.tick(2);
            setRp(p, rp(p) + 1);
            break;
        case 0x0B: case 0x1B: case 0x2B: case 0x3B:  // DEC rp
            bus_.tick(2);
            setRp(p, rp(p) - 1);
            break;

        case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C:  // INC r
            setRegX(y, inc8(regX(y)));
            break;
        case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D:  // DEC r
            setRegX(y, dec8(regX(y)));
            break;
        case 0x34: {  // INC (HL)
            const uint16_t addr = memOperand();
            const uint8_t v = bus_.read(addr);
            bus_.tick(1);
            bus_.write(addr, inc8(v));
            break;
        }
        case 0x35: {  // DEC (HL)
            const uint16_t addr = memOperand();
            const uint8_t v = bus_.read(addr);
            bus_.tick(1);
            bus_.write(addr, dec8(v));
            break;
        }

        case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x3E:  // LD r,n
            setRegX(y, imm8());
            break;
        case 0x36:  // LD (HL),n
            if (idx_ == None) {
                const uint8_t v = imm8();
                bus_.write(hl(), v);
            } else {
                const int8_t d = static_cast<int8_t>(imm8());
                const uint8_t v = imm8();
                bus_.tick(2);
                wz = static_cast<uint16_t>((idx_ == IX ? ix : iy) + d);
                bus_.write(wz, v);
            }
            break;

        case 0x07: {  // RLCA
            const uint8_t a = static_cast<uint8_t>(reg[A] << 1 | reg[A] >> 7);
            reg[A] = a;
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF)) | (a & (XF | YF | CF))));
            break;
        }
        case 0x0F: {  // RRCA
            const uint8_t carry = reg[A] & 1;
            const uint8_t a = static_cast<uint8_t>(reg[A] >> 1 | carry << 7);
            reg[A] = a;
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF)) | (a & (XF | YF)) | carry));
            break;
        }
        case 0x17: {  // RLA
            const uint8_t carry = reg[A] >> 7;
            const uint8_t a = static_cast<uint8_t>(reg[A] << 1 | (reg[F] & CF));
            reg[A] = a;
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF)) | (a & (XF | YF)) | carry));
            break;
        }
        case 0x1F: {  // RRA
            const uint8_t carry = reg[A] & 1;
            const uint8_t a = static_cast<uint8_t>(reg[A] >> 1 | (reg[F] & CF) << 7);
            reg[A] = a;
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF)) | (a & (XF | YF)) | carry));
            break;
        }

        case 0x08: {  // EX AF,AF'
            const uint16_t t = af();
            setAf(af2);
            af2 = t;
            break;
        }

        case 0x09: case 0x19: case 0x29: case 0x39:  // ADD HL,rp
            bus_.tick(7);
            setHlx(add16(hlx(), rp(p)));
            break;

        case 0x10: {  // DJNZ e
            bus_.tick(1);
            const int8_t d = static_cast<int8_t>(imm8());
            if (--reg[B]) {
                bus_.tick(5);
                pc = wz = static_cast<uint16_t>(pc + d);
            }
            break;
        }
        case 0x18: {  // JR e
            const int8_t d = static_cast<int8_t>(imm8());
            bus_.tick(5);
            pc = wz = static_cast<uint16_t>(pc + d);
            break;
        }
        case 0x20: case 0x28: case 0x30: case 0x38: {  // JR cc,e
            const int8_t d = static_cast<int8_t>(imm8());
            if (cond(y - 4)) {
                bus_.tick(5);
                pc = wz = static_cast<uint16_t>(pc + d);
            }
            break;
        }

        case 0x22: {  // LD (nn),HL
            const uint16_t addr = imm16();
            const uint16_t v = hlx();
            bus_.write(addr, v & 0xFF);
            bus_.write(addr + 1, v >> 8);
            wz = addr + 1;
            break;
        }
        case 0x2A: {  // LD HL,(nn)
            const uint16_t addr = imm16();
            const uint8_t lo = bus_.read(addr);
            const uint8_t hi = bus_.read(addr + 1);
            setHlx(static_cast<uint16_t>(hi << 8 | lo));
            wz = addr + 1;
            break;
        }
        case 0x32: {  // LD (nn),A
            const uint16_t addr = imm16();
            bus_.write(addr, reg[A]);
            wz = static_cast<uint16_t>(reg[A] << 8 | ((addr + 1) & 0xFF));
            break;
        }
        case 0x3A: {  // LD A,(nn)
            const uint16_t addr = imm16();
            reg[A] = bus_.read(addr);
            wz = addr + 1;
            break;
        }

        case 0x27:  // DAA
            daa();
            break;
        case 0x2F:  // CPL
            reg[A] = ~reg[A];
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF | CF)) | HF | NF | (reg[A] & (XF | YF))));
            break;
        case 0x37:  // SCF
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF)) | CF
                                      | (((prevQ_ ^ reg[F]) | reg[A]) & (XF | YF))));
            break;
        case 0x3F:  // CCF
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | PF)) | ((reg[F] & CF) ? HF : CF)
                                      | (((prevQ_ ^ reg[F]) | reg[A]) & (XF | YF))));
            break;

        case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:  // RET cc
            bus_.tick(1);
            if (cond(y))
                pc = wz = pop();
            break;
        case 0xC9:  // RET
            pc = wz = pop();
            break;

        case 0xC1: case 0xD1: case 0xE1:  // POP rp
            setRp(p, pop());
            break;
        case 0xF1:  // POP AF
            setAf(pop());
            break;
        case 0xC5: case 0xD5: case 0xE5:  // PUSH rp
            bus_.tick(1);
            push(rp(p));
            break;
        case 0xF5:  // PUSH AF
            bus_.tick(1);
            push(af());
            break;

        case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA:  // JP cc,nn
            wz = imm16();
            if (cond(y))
                pc = wz;
            break;
        case 0xC3:  // JP nn
            pc = wz = imm16();
            break;

        case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:  // CALL cc,nn
            wz = imm16();
            if (cond(y)) {
                bus_.tick(1);
                push(pc);
                pc = wz;
            }
            break;
        case 0xCD:  // CALL nn
            wz = imm16();
            bus_.tick(1);
            push(pc);
            pc = wz;
            break;

        case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:  // ALU A,n
            alu(y, imm8());
            break;

        case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:  // RST
            bus_.tick(1);
            push(pc);
            pc = wz = op & 0x38;
            break;

        case 0xCB:
            executeCB();
            break;

        case 0xD3: {  // OUT (n),A
            const uint8_t n = imm8();
            bus_.out(static_cast<uint16_t>(reg[A] << 8 | n), reg[A]);
            wz = static_cast<uint16_t>(reg[A] << 8 | ((n + 1) & 0xFF));
            break;
        }
        case 0xDB: {  // IN A,(n)
            const uint16_t port = static_cast<uint16_t>(reg[A] << 8 | imm8());
            wz = port + 1;
            reg[A] = bus_.in(port);
            break;
        }

        case 0xD9: {  // EXX
            uint16_t t = bc(); setBc(bc2); bc2 = t;
            t = de(); setDe(de2); de2 = t;
            t = hl(); setHl(hl2); hl2 = t;
            break;
        }
        case 0xE3: {  // EX (SP),HL
            const uint8_t lo = bus_.read(sp);
            const uint8_t hi = bus_.read(sp + 1);
            bus_.tick(1);
            const uint16_t v = hlx();
            bus_.write(sp + 1, v >> 8);
            bus_.write(sp, v & 0xFF);
            bus_.tick(2);
            wz = static_cast<uint16_t>(hi << 8 | lo);
            setHlx(wz);
            break;
        }
        case 0xE9:  // JP (HL)
            pc = hlx();
            break;
        case 0xEB: {  // EX DE,HL
            const uint16_t t = de();
            setDe(hl());
            setHl(t);
            break;
        }
        case 0xF9:  // LD SP,HL
            bus_.tick(2);
            sp = hlx();
            break;

        case 0xED:
            idx_ = None;
            executeED();
            break;

        case 0xF3:  // DI
            iff1 = iff2 = false;
            break;
        case 0xFB:  // EI
            iff1 = iff2 = true;
            eiDelay_ = true;
            break;

        default:
            // 0xDD and 0xFD are consumed as prefixes before getting here.
            break;
        }
    }

    void executeCB()
    {
        if (idx_ == None) {
            const uint8_t op = fetch();
            const int n = (op >> 3) & 7;
            const int z = op & 7;
            const bool isBit = (op >> 6) == 1;
            if (z == 6) {
                const uint16_t addr = hl();
                const uint8_t v = bus_.read(addr);
                bus_.tick(1);
                if (isBit)
                    bitTest(v, n, wz >> 8);
                else
                    bus_.write(addr, cbOp(op, v));
            } else if (isBit) {
                bitTest(reg[z], n, reg[z]);
            } else {
                reg[z] = cbOp(op, reg[z]);
            }
            return;
        }

        // DD CB d op: the displacement comes before the opcode, and neither
        // is fetched with an M1 cycle.
        const int8_t d = static_cast<int8_t>(imm8());
        const uint16_t addr = wz = static_cast<uint16_t>((idx_ == IX ? ix : iy) + d);
        const uint8_t op = bus_.read(pc++);
        bus_.tick(2);
        const uint8_t v = bus_.read(addr);
        bus_.tick(1);
        if ((op >> 6) == 1) {
            bitTest(v, (op >> 3) & 7, addr >> 8);
            return;
        }
        const uint8_t res = cbOp(op, v);
        bus_.write(addr, res);
        // Undocumented: the result is also copied to the register the opcode
        // names, when it names one.
        if ((op & 7) != 6)
            reg[op & 7] = res;
    }

    void executeED()
    {
        const uint8_t op = fetch();
        const int y = (op >> 3) & 7;
        const int z = op & 7;

        if ((op & 0xC0) == 0x40) {
            switch (z) {
            case 0: {  // IN r,(C)
                const uint8_t v = bus_.in(bc());
                wz = bc() + 1;
                if (y != 6)
                    reg[y] = v;
                setF(static_cast<uint8_t>((reg[F] & CF) | sz53p[v]));
                break;
            }
            case 1:  // OUT (C),r — the undocumented ED 71 outputs 0 on NMOS parts
                bus_.out(bc(), y == 6 ? 0 : reg[y]);
                wz = bc() + 1;
                break;
            case 2:  // SBC HL,rp / ADC HL,rp
                bus_.tick(7);
                if (y & 1)
                    adc16(rp(y >> 1));
                else
                    sbc16(rp(y >> 1));
                break;
            case 3: {  // LD (nn),rp / LD rp,(nn)
                const uint16_t addr = imm16();
                if (y & 1) {
                    const uint8_t lo = bus_.read(addr);
                    const uint8_t hi = bus_.read(addr + 1);
                    setRp(y >> 1, static_cast<uint16_t>(hi << 8 | lo));
                } else {
                    const uint16_t v = rp(y >> 1);
                    bus_.write(addr, v & 0xFF);
                    bus_.write(addr + 1, v >> 8);
                }
                wz = addr + 1;
                break;
            }
            case 4: {  // NEG
                const uint8_t v = reg[A];
                reg[A] = 0;
                reg[A] = sub8(v, 0);
                break;
            }
            case 5:  // RETN / RETI
                pc = wz = pop();
                iff1 = iff2;
                break;
            case 6: {  // IM 0/1/2
                static constexpr uint8_t modes[4] = {0, 0, 1, 2};
                im = modes[y & 3];
                break;
            }
            default:
                switch (y) {
                case 0:  // LD I,A
                    bus_.tick(1);
                    i = reg[A];
                    break;
                case 1:  // LD R,A
                    bus_.tick(1);
                    r = reg[A];
                    break;
                case 2:  // LD A,I
                    bus_.tick(1);
                    reg[A] = i;
                    setF(static_cast<uint8_t>((reg[F] & CF) | sz53[reg[A]] | (iff2 ? PF : 0)));
                    ldAIR_ = true;
                    break;
                case 3:  // LD A,R
                    bus_.tick(1);
                    reg[A] = r;
                    setF(static_cast<uint8_t>((reg[F] & CF) | sz53[reg[A]] | (iff2 ? PF : 0)));
                    ldAIR_ = true;
                    break;
                case 4: {  // RRD
                    const uint16_t addr = hl();
                    const uint8_t v = bus_.read(addr);
                    bus_.tick(4);
                    bus_.write(addr, static_cast<uint8_t>(reg[A] << 4 | v >> 4));
                    reg[A] = static_cast<uint8_t>((reg[A] & 0xF0) | (v & 0x0F));
                    setF(static_cast<uint8_t>((reg[F] & CF) | sz53p[reg[A]]));
                    wz = addr + 1;
                    break;
                }
                case 5: {  // RLD
                    const uint16_t addr = hl();
                    const uint8_t v = bus_.read(addr);
                    bus_.tick(4);
                    bus_.write(addr, static_cast<uint8_t>(v << 4 | (reg[A] & 0x0F)));
                    reg[A] = static_cast<uint8_t>((reg[A] & 0xF0) | v >> 4);
                    setF(static_cast<uint8_t>((reg[F] & CF) | sz53p[reg[A]]));
                    wz = addr + 1;
                    break;
                }
                default:  // ED 77, ED 7F: no operation
                    break;
                }
                break;
            }
            return;
        }

        if ((op & 0xE4) == 0xA0) {
            executeBlock(op);
            return;
        }
        // Every other ED opcode does nothing for the length of two fetches.
    }

    // LDI, CPI, INI, OUTI and their decrementing and repeating forms.
    void executeBlock(uint8_t op)
    {
        const int dir = (op & 0x08) ? -1 : 1;
        const bool repeat = op & 0x10;
        const uint16_t addr = hl();

        switch (op & 3) {
        case 0: {  // LDI / LDD / LDIR / LDDR
            const uint8_t v = bus_.read(addr);
            bus_.write(de(), v);
            bus_.tick(2);
            setHl(addr + dir);
            setDe(de() + dir);
            setBc(bc() - 1);
            const uint8_t n = v + reg[A];
            setF(static_cast<uint8_t>((reg[F] & (SF | ZF | CF)) | (bc() ? PF : 0) | (n & XF)
                                      | ((n << 4) & YF)));
            if (repeat && bc() != 0)
                repeatBlock();
            break;
        }
        case 1: {  // CPI / CPD / CPIR / CPDR
            const uint8_t v = bus_.read(addr);
            bus_.tick(5);
            const uint8_t res = reg[A] - v;
            const uint8_t half = (reg[A] ^ v ^ res) & HF;
            const uint8_t n = res - (half ? 1 : 0);
            setHl(addr + dir);
            setBc(bc() - 1);
            wz += dir;
            setF(static_cast<uint8_t>((reg[F] & CF) | NF | (sz53[res] & ~(XF | YF)) | half
                                      | (bc() ? PF : 0) | (n & XF) | ((n << 4) & YF)));
            if (repeat && bc() != 0 && res != 0)
                repeatBlock();
            break;
        }
        case 2: {  // INI / IND / INIR / INDR
            bus_.tick(1);
            const uint8_t v = bus_.in(bc());
            wz = bc() + dir;
            --reg[B];
            bus_.write(addr, v);
            setHl(addr + dir);
            blockIoFlags(v, v + static_cast<uint8_t>(reg[C] + dir));
            if (repeat && reg[B] != 0) {
                repeatBlock();
                blockIoRepeatFlags(v);
            }
            break;
        }
        default: {  // OUTI / OUTD / OTIR / OTDR
            bus_.tick(1);
            const uint8_t v = bus_.read(addr);
            --reg[B];
            bus_.out(bc(), v);
            setHl(addr + dir);
            wz = bc() + dir;
            blockIoFlags(v, v + reg[L]);
            if (repeat && reg[B] != 0) {
                repeatBlock();
                blockIoRepeatFlags(v);
            }
            break;
        }
        }
    }

    // A repeating block instruction re-executes itself; meanwhile the
    // undocumented bits show the high byte of its own address.
    void repeatBlock()
    {
        bus_.tick(5);
        pc -= 2;
        wz = pc + 1;
        setF(static_cast<uint8_t>((reg[F] & ~(XF | YF)) | ((pc >> 8) & (XF | YF))));
    }

    void blockIoFlags(uint8_t v, unsigned k)
    {
        setF(static_cast<uint8_t>(sz53[reg[B]] | ((v >> 6) & NF) | (k > 0xFF ? HF | CF : 0)
                                  | (sz53p[(k & 7) ^ reg[B]] & PF)));
    }

    // Extra flag changes while INIR/OTIR and friends are still repeating.
    void blockIoRepeatFlags(uint8_t v)
    {
        uint8_t f = reg[F];
        if (f & CF) {
            f &= ~HF;
            if (v & 0x80) {
                f ^= (sz53p[(reg[B] - 1) & 7] ^ PF) & PF;
                if ((reg[B] & 0x0F) == 0x00)
                    f |= HF;
            } else {
                f ^= (sz53p[(reg[B] + 1) & 7] ^ PF) & PF;
                if ((reg[B] & 0x0F) == 0x0F)
                    f |= HF;
            }
        } else {
            f ^= (sz53p[reg[B] & 7] ^ PF) & PF;
        }
        setF(f);
    }
};

}  // namespace tuxape
