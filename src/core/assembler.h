#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tuxape {

// A Z80 assembler with the syntax of Maxam 1.5 and the extensions of
// WinAPE's: several instructions on a line between colons, labels with or
// without a leading dot, numbers in hexadecimal after '#' or '&' and in
// binary after '%', expressions worked out from left to right with no
// precedence between operators, macros, repeat and while loops,
// conditional assembly, and other source or binary files read in.
//
// Nothing is written anywhere: what the program asked for comes back in
// the result, for the caller to put into the machine's memory or on disc.

struct AsmError {
    std::string file;
    int line = 0;  // from 1
    std::string message;
};

struct AsmSymbol {
    std::string name;  // as first written
    int32_t value = 0;
    bool used = false;  // some expression read it
    std::string file;   // where it was defined
    int line = 0;
};

// Bytes for the machine's memory.
struct AsmBlock {
    uint16_t address = 0;
    std::vector<uint8_t> data;
    int bank = -1;  // the RAM bank `write direct` named (#C0 to #FF), or -1
};

// The output of a `write "name"`: a file on the host, or with `direct` on
// the disc in the drive.
struct AsmFile {
    std::string name;
    bool direct = false;
    uint16_t loadAddress = 0;
    int execAddress = -1;
    std::vector<uint8_t> data;
};

// A `save`: regions of the machine's memory to write to a file once the
// assembled code is in place.
struct AsmSave {
    std::string name;
    bool direct = false;
    std::vector<std::pair<uint16_t, int>> regions;  // address, size
    int execAddress = -1;
};

struct AsmResult {
    std::vector<AsmError> errors;
    std::vector<AsmSymbol> symbols;   // in alphabetical order
    std::vector<std::string> output;  // the listing, and what `print` wrote
    std::vector<AsmBlock> memory;
    std::vector<AsmFile> files;
    std::vector<AsmSave> saves;
    std::optional<uint16_t> run;         // the `run` directive's address
    std::optional<uint16_t> breakpoint;  // and its breakpoint
    int bytes = 0;                       // generated in all
    int lines = 0;                       // read, included files too
    bool ok() const { return errors.empty(); }
};

// What the assembler needs from outside. Each of them may be left empty.
struct AsmHost {
    // The text of a source file `read` names, and the bytes of a file
    // `incbin` names; `from` is the file the directive is in. `source` may
    // put in `name` the path it found the file at, which errors then give.
    std::function<std::optional<std::string>(std::string& name, const std::string& from)> source;
    std::function<std::optional<std::vector<uint8_t>>(const std::string& name, const std::string& from)> binary;
    // The machine's memory, for memory() and checksum(start,count).
    std::function<uint8_t(uint16_t)> memory;
};

AsmResult assemble(const std::string& source, const std::string& fileName = {}, const AsmHost& host = {});

}  // namespace tuxape
