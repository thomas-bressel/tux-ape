// Symbol files as the assemblers of the CPC write them, read; TuxAPE's own
// written and read back; and the names of addresses as the debugger shows
// them, put in place of the addresses of instructions.

#include <string>

#include "check.h"
#include "core/symbols.h"

int main()
{
    using namespace tuxape;

    // ---- Every kind of file ----
    const std::string text =
        "; symbols of a program\n"
        "start equ #8000\n"            // WinAPE, TuxAPE
        "loop: EQU 0x00008003\n"       // sjasmplus
        "SCREEN EQU 0C000H\n"          // pasmo
        "TXT_OUTPUT #BB5A B0\n"        // rasm, a bank after it
        "DEF table 9000\n"             // NoICE, in hexadecimal
        "count = 10\n"                 // decimal
        "mask %1111\n"                 // binary
        ".local &4000 ; a comment\n"
        "\n"
        "Symbol table:\n"              // what is no symbol is passed over
        "1234 nothing\n";
    const auto symbols = readSymbolFile(text, "prog.sym");
    CHECK(symbols.has_value());
    if (symbols) {
        CHECK_EQ(symbols->size(), 8);
        const auto value = [&](const char* name) {
            for (const AsmSymbol& symbol : *symbols)
                if (symbol.name == name)
                    return symbol.value;
            return -1;
        };
        CHECK_EQ(value("start"), 0x8000);
        CHECK_EQ(value("loop"), 0x8003);
        CHECK_EQ(value("SCREEN"), 0xC000);
        CHECK_EQ(value("TXT_OUTPUT"), 0xBB5A);
        CHECK_EQ(value("table"), 0x9000);
        CHECK_EQ(value("count"), 10);
        CHECK_EQ(value("mask"), 15);
        CHECK_EQ(value(".local"), 0x4000);
        CHECK(symbols->front().file == "prog.sym" && symbols->front().line == 2);
    }
    // A text with no symbol at all is no symbol file.
    CHECK(!readSymbolFile("hello\n\n; nothing here\n").has_value());

    // ---- TuxAPE's own, read back ----
    {
        std::vector<AsmSymbol> mine(2);
        mine[0].name = "start";
        mine[0].value = 0x8000;
        mine[1].name = "data";
        mine[1].value = 0x12;
        const std::string written = symbolFileText(mine);
        CHECK(written == "start equ #8000\ndata equ #0012\n");
        const auto back = readSymbolFile(written);
        CHECK(back && back->size() == 2 && (*back)[1].name == "data" && (*back)[1].value == 0x12);
    }

    // ---- The names of addresses ----
    if (symbols) {
        SymbolNames names;
        CHECK(names.empty() && names.named("CALL #BB5A") == "CALL #BB5A");
        names.set(*symbols);
        CHECK(!names.empty());
        CHECK(names.at(0x8000) == std::vector<std::string>{"start"});
        CHECK(names.at(0x1234).empty());
        CHECK(names.named("CALL #BB5A") == "CALL TXT_OUTPUT");
        CHECK(names.named("LD HL,#C000") == "LD HL,SCREEN");
        CHECK(names.named("LD (#9000),A") == "LD (table),A");
        CHECK(names.named("JR NZ,#8003") == "JR NZ,loop");
        // Bytes and offsets stay numbers, and so do addresses no symbol has.
        CHECK(names.named("LD A,#0A") == "LD A,#0A");
        CHECK(names.named("LD (IX+#0A),A") == "LD (IX+#0A),A");
        CHECK(names.named("JP #1234") == "JP #1234");
        CHECK(names.value("txt_output") == 0xBB5A && !names.value("nowhere"));
        // Two names of one value: the first is the one put in.
        std::vector<AsmSymbol> two(2);
        two[0].name = "here";
        two[1].name = "there";
        two[0].value = two[1].value = 0x4000;
        names.set(two);
        CHECK(names.at(0x4000).size() == 2 && names.named("JP #4000") == "JP here");
    }
    return checkSummary("symbols");
}
