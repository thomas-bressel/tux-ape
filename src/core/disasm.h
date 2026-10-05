#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace tuxape {

// One Z80 instruction, as WinAPE's debugger writes it: mnemonic, a space,
// operands; numbers in hexadecimal after a '#'; the halves of IX and IY as
// HX, LX, HY and LY.
struct Instruction {
    // What the instruction does to the program's course, which is what
    // stepping over it needs to know.
    enum class Flow {
        Plain,
        Jump,    // JP, JR, DJNZ, with or without a condition
        Call,    // CALL and RST: comes back to the instruction after it
        Return,  // RET, RETI, RETN
        Repeat,  // LDIR and its like: the same instruction until done
        Halt,
    };
    std::string text;
    int length = 1;  // bytes, 1 to 4
    Flow flow = Flow::Plain;
};

// The instruction at `address`; `read` gives the byte at an address.
Instruction disassemble(uint16_t address, const std::function<uint8_t(uint16_t)>& read);

}  // namespace tuxape
