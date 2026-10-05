#include "core/disasm.h"

namespace tuxape {

namespace {

const char* const kReg[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
const char* const kPair[4] = {"BC", "DE", "HL", "SP"};
const char* const kPushPair[4] = {"BC", "DE", "HL", "AF"};
const char* const kCondition[8] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
const char* const kAlu[8] = {"ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "};
const char* const kRotate[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};
const char* const kBlock[4][4] = {{"LDI", "CPI", "INI", "OUTI"},
                                  {"LDD", "CPD", "IND", "OUTD"},
                                  {"LDIR", "CPIR", "INIR", "OTIR"},
                                  {"LDDR", "CPDR", "INDR", "OTDR"}};

std::string hex(unsigned value, int digits)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string text = "#";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        text += kDigits[value >> shift & 0xF];
    return text;
}

// Reads the instruction's bytes one after the other.
class Reader {
public:
    Reader(uint16_t address, const std::function<uint8_t(uint16_t)>& read)
        : start_(address)
        , read_(read)
    {
    }
    uint8_t byte() { return read_(static_cast<uint16_t>(start_ + length_++)); }
    std::string byteText() { return hex(byte(), 2); }
    std::string wordText()
    {
        const unsigned low = byte();
        return hex(low | byte() << 8, 4);
    }
    // Where a relative jump goes.
    std::string target()
    {
        const int8_t offset = static_cast<int8_t>(byte());
        return hex(static_cast<uint16_t>(start_ + length_ + offset), 4);
    }
    int length() const { return length_; }

private:
    uint16_t start_;
    const std::function<uint8_t(uint16_t)>& read_;
    int length_ = 0;
};

// The index register an instruction uses in HL's place, if any.
struct Index {
    const char* name = nullptr;  // "IX" or "IY"
    const char* high = "H";
    const char* low = "L";
    std::string memory;          // "(IX+#12)", once its offset has been read

    const char* pair(int p) const { return p == 2 && name ? name : kPair[p]; }
    const char* pushPair(int p) const { return p == 2 && name ? name : kPushPair[p]; }
    // A register by its three bits. `indexed` says the instruction also
    // names (IX+d): H and L are then the real ones.
    std::string reg(int r, bool indexed = false) const
    {
        if (r == 6)
            return name ? memory : "(HL)";
        if (name && !indexed && r == 4)
            return high;
        if (name && !indexed && r == 5)
            return low;
        return kReg[r];
    }
    void readOffset(Reader& in)
    {
        if (!name)
            return;
        const int8_t offset = static_cast<int8_t>(in.byte());
        memory = std::string("(") + name + (offset < 0 ? "-" : "+") + hex(static_cast<unsigned>(offset < 0 ? -offset : offset), 2) + ")";
    }
};

Instruction bits(Reader& in, Index& index)
{
    Instruction out;
    // With an index register the offset comes before the operation.
    index.readOffset(in);
    const uint8_t op = in.byte();
    const int x = op >> 6, y = op >> 3 & 7, z = op & 7;
    const std::string operand = index.name ? index.memory : kReg[z];
    if (x == 0) {
        out.text = std::string(kRotate[y]) + " " + operand;
    } else {
        static const char* const kNames[4] = {"", "BIT", "RES", "SET"};
        out.text = std::string(kNames[x]) + " " + static_cast<char>('0' + y) + "," + operand;
    }
    // The undocumented forms that also copy the result into a register.
    if (index.name && z != 6 && x != 1)
        out.text += std::string(",") + kReg[z];
    return out;
}

Instruction extended(Reader& in)
{
    Instruction out;
    const uint8_t op = in.byte();
    const int x = op >> 6, y = op >> 3 & 7, z = op & 7, p = y >> 1, q = y & 1;
    if (op == 0xFF) {
        out.text = "BRK";  // WinAPE's breakpoint instruction
    } else if (x == 1) {
        switch (z) {
        case 0: out.text = y == 6 ? "IN (C)" : std::string("IN ") + kReg[y] + ",(C)"; break;
        case 1: out.text = y == 6 ? "OUT (C),0" : std::string("OUT (C),") + kReg[y]; break;
        case 2: out.text = std::string(q ? "ADC HL," : "SBC HL,") + kPair[p]; break;
        case 3:
            out.text = q ? std::string("LD ") + kPair[p] + ",(" + in.wordText() + ")"
                         : "LD (" + in.wordText() + ")," + kPair[p];
            break;
        case 4: out.text = "NEG"; break;
        case 5:
            out.text = y == 1 ? "RETI" : "RETN";
            out.flow = Instruction::Flow::Return;
            break;
        case 6: {
            static const char* const kModes[8] = {"0", "0", "1", "2", "0", "0", "1", "2"};
            out.text = std::string("IM ") + kModes[y];
            break;
        }
        case 7: {
            static const char* const kNames[8] = {"LD I,A", "LD R,A", "LD A,I", "LD A,R", "RRD", "RLD", "NOP", "NOP"};
            out.text = kNames[y];
            break;
        }
        }
    } else if (x == 2 && z <= 3 && y >= 4) {
        out.text = kBlock[y - 4][z];
        if (y >= 6)
            out.flow = Instruction::Flow::Repeat;
    } else {
        out.text = "NOP";  // the rest of the page does nothing
    }
    return out;
}

Instruction plain(Reader& in, Index& index, uint8_t op)
{
    using Flow = Instruction::Flow;
    Instruction out;
    const int x = op >> 6, y = op >> 3 & 7, z = op & 7, p = y >> 1, q = y & 1;
    switch (x) {
    case 0:
        switch (z) {
        case 0:
            if (y == 0) {
                out.text = "NOP";
            } else if (y == 1) {
                out.text = "EX AF,AF'";
            } else {
                out.text = y == 2 ? "DJNZ " : y == 3 ? "JR " : std::string("JR ") + kCondition[y - 4] + ",";
                out.text += in.target();
                out.flow = Flow::Jump;
            }
            break;
        case 1:
            out.text = q ? std::string("ADD ") + index.pair(2) + "," + index.pair(p)
                         : std::string("LD ") + index.pair(p) + "," + in.wordText();
            break;
        case 2: {
            static const char* const kNames[8] = {"LD (BC),A", "LD A,(BC)", "LD (DE),A", "LD A,(DE)", "", "", "", ""};
            if (p < 2)
                out.text = kNames[y];
            else if (y == 4)
                out.text = "LD (" + in.wordText() + ")," + index.pair(2);
            else if (y == 5)
                out.text = std::string("LD ") + index.pair(2) + ",(" + in.wordText() + ")";
            else if (y == 6)
                out.text = "LD (" + in.wordText() + "),A";
            else
                out.text = "LD A,(" + in.wordText() + ")";
            break;
        }
        case 3: out.text = std::string(q ? "DEC " : "INC ") + index.pair(p); break;
        case 4:
        case 5:
            if (y == 6)
                index.readOffset(in);
            out.text = std::string(z == 4 ? "INC " : "DEC ") + index.reg(y);
            break;
        case 6:
            if (y == 6)
                index.readOffset(in);
            out.text = "LD " + index.reg(y) + "," + in.byteText();
            break;
        case 7: {
            static const char* const kNames[8] = {"RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"};
            out.text = kNames[y];
            break;
        }
        }
        break;
    case 1:
        if (op == 0x76) {
            out.text = "HALT";
            out.flow = Flow::Halt;
        } else {
            // With (IX+d) on one side, H and L on the other are themselves.
            const bool indexed = y == 6 || z == 6;
            if (indexed)
                index.readOffset(in);
            out.text = "LD " + index.reg(y, indexed) + "," + index.reg(z, indexed);
        }
        break;
    case 2:
        if (z == 6)
            index.readOffset(in);
        out.text = kAlu[y] + index.reg(z);
        break;
    case 3:
        switch (z) {
        case 0:
            out.text = std::string("RET ") + kCondition[y];
            out.flow = Flow::Return;
            break;
        case 1:
            if (!q) {
                out.text = std::string("POP ") + index.pushPair(p);
            } else if (p == 0) {
                out.text = "RET";
                out.flow = Flow::Return;
            } else if (p == 1) {
                out.text = "EXX";
            } else if (p == 2) {
                out.text = std::string("JP (") + index.pair(2) + ")";
                out.flow = Flow::Jump;
            } else {
                out.text = std::string("LD SP,") + index.pair(2);
            }
            break;
        case 2:
            out.text = std::string("JP ") + kCondition[y] + "," + in.wordText();
            out.flow = Flow::Jump;
            break;
        case 3:
            switch (y) {
            case 0:
                out.text = "JP " + in.wordText();
                out.flow = Flow::Jump;
                break;
            case 2: out.text = "OUT (" + in.byteText() + "),A"; break;
            case 3: out.text = "IN A,(" + in.byteText() + ")"; break;
            case 4: out.text = std::string("EX (SP),") + index.pair(2); break;
            case 5: out.text = "EX DE,HL"; break;
            case 6: out.text = "DI"; break;
            case 7: out.text = "EI"; break;
            }
            break;
        case 4:
            out.text = std::string("CALL ") + kCondition[y] + "," + in.wordText();
            out.flow = Flow::Call;
            break;
        case 5:
            if (!q) {
                out.text = std::string("PUSH ") + index.pushPair(p);
            } else {
                out.text = "CALL " + in.wordText();
                out.flow = Flow::Call;
            }
            break;
        case 6: out.text = kAlu[y] + in.byteText(); break;
        case 7:
            out.text = "RST " + hex(static_cast<unsigned>(y * 8), 2);
            out.flow = Flow::Call;
            break;
        }
        break;
    }
    return out;
}

}  // namespace

Instruction disassemble(uint16_t address, const std::function<uint8_t(uint16_t)>& read)
{
    Reader in(address, read);
    Index index;
    uint8_t op = in.byte();
    // An index prefix; before another prefix it does nothing, and stands
    // as an instruction of its own.
    if (op == 0xDD || op == 0xFD) {
        const uint8_t next = read(static_cast<uint16_t>(address + 1));
        if (next == 0xDD || next == 0xFD || next == 0xED) {
            Instruction out;
            out.text = "NOP";
            return out;
        }
        index.name = op == 0xDD ? "IX" : "IY";
        index.high = op == 0xDD ? "HX" : "HY";
        index.low = op == 0xDD ? "LX" : "LY";
        op = in.byte();
    }
    Instruction out = op == 0xCB ? bits(in, index) : op == 0xED ? extended(in) : plain(in, index, op);
    out.length = in.length();
    return out;
}

}  // namespace tuxape
