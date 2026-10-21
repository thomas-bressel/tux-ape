#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/assembler.h"

namespace tuxape {

// Symbol files: one symbol a line, its name then its value, as assemblers
// write them for debuggers. Those of WinAPE and TuxAPE (`name equ #1234`),
// rasm (`name #1234`, a bank after it), pasmo (`name EQU 01234H`),
// sjasmplus (`name: EQU 0x00001234`) and NoICE (`DEF name 1234`, in
// hexadecimal) are all read. A value is in hexadecimal after '#', '&', '$'
// or "0x", or with an 'H' after it; in binary after '%'; in decimal
// otherwise. What follows ';' is a comment; lines that are no symbol are
// passed over. Nothing if the text holds no symbol at all.
//
// Each symbol read has `file` for its file and `line` for its line in it.
std::optional<std::vector<AsmSymbol>> readSymbolFile(const std::string& text, const std::string& fileName = {});

// The symbols as TuxAPE writes them, as a source its assembler can read
// back: `name equ #1234`, a line each.
std::string symbolFileText(const std::vector<AsmSymbol>& symbols);

// The names of a program's addresses, as a debugger shows them: those of
// the symbols whose value is the address, 0 to &FFFF.
class SymbolNames {
public:
    void set(const std::vector<AsmSymbol>& symbols);
    bool empty() const { return byValue_.empty(); }

    // The names whose value is `address`, in the order the symbols came
    // in; none if no symbol has that value.
    const std::vector<std::string>& at(uint16_t address) const;
    // An instruction as the disassembler writes it, each of its addresses
    // (a number of four hexadecimal digits after '#') put as the first
    // name of that value: "CALL #BB5A" becomes "CALL TXT_OUTPUT". Numbers
    // of two digits, bytes and offsets, are left as they are.
    std::string named(const std::string& instruction) const;
    // The value of a name, in any case.
    std::optional<uint16_t> value(const std::string& name) const;

private:
    std::unordered_map<uint16_t, std::vector<std::string>> byValue_;
    std::unordered_map<std::string, uint16_t> byName_;  // in upper case
};

}  // namespace tuxape
