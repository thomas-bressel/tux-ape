// The assembler: every instruction the disassembler can write goes back to
// the bytes it came from, and the examples of WinAPE's help give what the
// help says they give.

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "core/assembler.h"
#include "core/disasm.h"

namespace {

using namespace tuxape;

// The bytes an assembly put into memory, as one run from its lowest address.
std::vector<uint8_t> bytesOf(const AsmResult& result)
{
    std::vector<uint8_t> out;
    if (result.memory.empty())
        return out;
    const unsigned first = result.memory.front().address;
    for (const AsmBlock& block : result.memory) {
        out.resize(block.address - first, 0);
        out.insert(out.end(), block.data.begin(), block.data.end());
    }
    return out;
}

std::string hexOf(const std::vector<uint8_t>& bytes)
{
    std::string text;
    char two[4];
    for (const uint8_t byte : bytes) {
        std::snprintf(two, sizeof two, "%02X", byte);
        text += (text.empty() ? "" : " ") + std::string(two);
    }
    return text;
}

std::string errorsOf(const AsmResult& result)
{
    std::string text;
    for (const AsmError& error : result.errors)
        text += std::to_string(error.line) + ": " + error.message + "; ";
    return text;
}

// `source` must assemble into `want`, bytes written in hexadecimal.
void expect(const std::string& source, const std::string& want)
{
    const AsmResult result = assemble(source);
    const std::string got = hexOf(bytesOf(result));
    if (!result.ok() || got != want) {
        std::printf("\"%s\": got [%s] %s, want [%s]\n", source.c_str(), got.c_str(), errorsOf(result).c_str(),
                    want.c_str());
        ++g_failures;
    }
}

// `source` must be refused with `message` on `line`.
void expectError(const std::string& source, const std::string& message, int line = 1)
{
    const AsmResult result = assemble(source);
    if (result.errors.empty() || result.errors[0].message != message || result.errors[0].line != line) {
        std::printf("\"%s\": got \"%s\", want \"%d: %s\"\n", source.c_str(), errorsOf(result).c_str(), line,
                    message.c_str());
        ++g_failures;
    }
}

int32_t symbol(const AsmResult& result, const std::string& name)
{
    for (const AsmSymbol& s : result.symbols)
        if (s.name == name)
            return s.value;
    std::printf("no symbol %s\n", name.c_str());
    ++g_failures;
    return -1;
}

Instruction at(const std::vector<uint8_t>& code)
{
    return disassemble(0x4000, [&](uint16_t a) {
        const size_t index = static_cast<uint16_t>(a - 0x4000);
        return index < code.size() ? code[index] : uint8_t(0);
    });
}

// What the disassembler writes for some bytes must assemble back into
// them, or, for the bytes that have a shorter or more usual spelling,
// into bytes that read the same.
void roundTrip(const std::vector<uint8_t>& code, int& count)
{
    const Instruction shown = at(code);
    const AsmResult result = assemble("org #4000\n" + shown.text);
    const std::vector<uint8_t> got = bytesOf(result);
    const std::vector<uint8_t> want(code.begin(), code.begin() + shown.length);
    ++count;
    if (result.ok() && (got == want || at(got).text == shown.text))
        return;
    std::printf("[%s] \"%s\": got [%s] %s\n", hexOf(want).c_str(), shown.text.c_str(), hexOf(got).c_str(),
                errorsOf(result).c_str());
    ++g_failures;
}

void testRoundTrip()
{
    int count = 0;
    for (int op = 0; op < 256; ++op) {
        const uint8_t b = static_cast<uint8_t>(op);
        if (b != 0xCB && b != 0xED && b != 0xDD && b != 0xFD)
            roundTrip({b, 0x34, 0x12}, count);
        roundTrip({0xCB, b}, count);
        if (b != 0xFF)  // ED FF is the debugger's own breakpoint
            roundTrip({0xED, b, 0x34, 0x12}, count);
        for (const uint8_t prefix : {uint8_t(0xDD), uint8_t(0xFD)}) {
            if (b != 0xCB && b != 0xED && b != 0xDD && b != 0xFD) {
                roundTrip({prefix, b, 0x34, 0x12}, count);
                roundTrip({prefix, b, 0xF0, 0x12}, count);  // a negative displacement
            }
            roundTrip({prefix, 0xCB, 0x05, b}, count);
            roundTrip({prefix, 0xCB, 0x85, b}, count);
        }
    }
    CHECK(count > 2000);

    // Instructions that are what they look like must give exactly these.
    expect("nop", "00");
    expect("ld a,b", "78");
    expect("LD (HL),C", "71");
    expect("ld hl,#1234", "21 34 12");
    expect("ld (ix+5),#12", "DD 36 05 12");
    expect("ld b,(iy - 5)", "FD 46 FB");
    expect("ld b,(iy + -5)", "FD 46 FB");
    expect("ld a,(ix)", "DD 7E 00");
    expect("ld hx,lx", "DD 65");
    expect("ld IXh,5", "DD 26 05");
    expect("ld a,ly", "FD 7D");
    expect("ld bc,(#1234)", "ED 4B 34 12");
    expect("ld (&1234),sp", "ED 73 34 12");
    expect("ld sp,ix", "DD F9");
    expect("adc h", "8C");
    expect("adc a,h", "8C");
    expect("sub a,5", "D6 05");
    expect("add hl,de", "19");
    expect("add ix,ix", "DD 29");
    expect("sbc hl,sp", "ED 72");
    expect("ex af,af'", "08");
    expect("ex hl,de", "EB");
    expect("ex (sp),iy", "FD E3");
    expect("jp (ix)", "DD E9");
    expect("jp nc,#4000", "D2 00 40");
    expect("org #100:jr $", "18 FE");
    expect("org #100:djnz $+2", "10 00");
    expect("org #100:jr c,#181", "38 7F");
    expect("org #100:jr z,#82", "28 80");
    expect("rst 3", "DF");
    expect("rst #18", "DF");
    expect("rst 8", "CF");
    expect("rst 1,#1234", "CF 34 12");
    expect("im 2", "ED 5E");
    expect("in a,(#fe)", "DB FE");
    expect("in d,(c)", "ED 50");
    expect("out (c),e", "ED 59");
    expect("out (#7f),a", "D3 7F");
    expect("bit 7,(ix-1)", "DD CB FF 7E");
    expect("set 0,(hl)", "CB C6");
    expect("sll b", "CB 30");
    expect("push af:pop iy", "F5 FD E1");
    expect("ret pe", "E8");
    expect("call m,5", "FC 05 00");
    expect("brk", "F7");

    expectError("ld hx,h", "Invalid Operand");
    expectError("ld hx,ly", "Invalid Operand");
    expectError("ld (hl),(hl)", "Invalid Operand");
    expectError("ld a,300", "Value Out of Range");
    expectError("ld (ix+128),a", "Value Out of Range");
    expectError("org #100:jr #182", "Relative Jump Out of Range");
    expectError("bit 8,a", "Value Out of Range");
    expectError("rst 9", "Value Out of Range");
    expectError("push sp", "Invalid Operand");
    expectError("frobnicate a", "Unknown Instruction: A");
    expectError("nop\nld a,", "Bad Expression", 2);
    expectError("nop\n\nld a,nowhere", "Undefined Symbol: nowhere", 3);
}

// The help's own examples.
void testSyntax()
{
    {
        const AsmResult result = assemble(" org #4000\n"
                                          " ABC\n"
                                          " def:ghi ld a,b\n"
                                          " .jkl ld c,d:mno\n"
                                          " pqr: ld b,a\n"
                                          " _stu:\n"
                                          " jp pqr\n");
        CHECK(result.ok());
        CHECK(hexOf(bytesOf(result)) == "78 4A 47 C3 02 40");
        CHECK_EQ(symbol(result, "ABC"), 0x4000);
        CHECK_EQ(symbol(result, "def"), 0x4000);
        CHECK_EQ(symbol(result, "ghi"), 0x4000);
        CHECK_EQ(symbol(result, "jkl"), 0x4001);
        CHECK_EQ(symbol(result, "mno"), 0x4002);
        CHECK_EQ(symbol(result, "pqr"), 0x4002);
        CHECK_EQ(symbol(result, "_stu"), 0x4003);
        const std::vector<std::string> want = {
            "000001  0000  (4000)         org #4000", "000002  4000                 ABC",
            "000003  4000                 def",       "000003  4000  78            ghi ld a,b",
            "000004  4001  4A             .jkl ld c,d", "000004  4002                mno",
            "000005  4002                 pqr",       "000005  4002  47             ld b,a",
            "000006  4003                 _stu",      "000007  4003  C3 02 40       jp pqr"};
        CHECK(result.output == want);
        if (result.output != want)
            for (const std::string& line : result.output)
                std::printf("  [%s]\n", line.c_str());
        CHECK_EQ(result.bytes, 6);
        CHECK_EQ(result.lines, 7);
    }
    {
        // No precedence between operators: from left to right.
        const AsmResult result = assemble("x equ 3\ny equ 2 * 4 + x\nz equ x + 2 * 4\n"
                                          ".int_vector equ #38\n.print equ 6 * 3 + z\n");
        CHECK(result.ok());
        CHECK_EQ(symbol(result, "y"), 11);
        CHECK_EQ(symbol(result, "z"), 20);
        CHECK_EQ(symbol(result, "int_vector"), 0x38);
        CHECK_EQ(symbol(result, "print"), 38);
    }
    expect("defb \"Hello\",32,\"t\" - 32,'here',10", "48 65 6C 6C 6F 20 54 68 65 72 65 0A");
    expect("defs 3,#ff,4,0,5,#80,2", "FF FF FF 00 00 00 00 80 80 80 80 80 00 00");
    expect("defw #1234, #abcd, 1, 2000", "34 12 CD AB 01 00 D0 07");
    expect("str \"ABC\", \"DEF\",1", "41 42 C3 44 45 C6 01");
    expect("org #4000:nop:align 4,#ff:nop", "00 FF FF FF 00");
    expect("org #4000:align 256:nop", "00");

    // Numbers and operators.
    expect("db #ff,&7F,%1010,10,0x20,0FFh,101b,$10,\"A\",'B'", "FF 7F 0A 0A 20 FF 05 10 41 42");
    expect("db 7 mod 4,6 and 3,4 or 1,6 xor 3,1 shl 4,#80 shr 7,-1,~0 and #ff", "03 02 05 05 10 01 FF FF");
    expect("db (1+2)*3,[2+2]*2,2+2*2,10/3,1 << 3,16 >> 2", "09 08 08 03 08 04");
    expect("db 3=3,3<>3,2<3,3<=2,\"de\"=\"DE\",\"de\"=\"hl\",not 0 and 1", "FF 00 FF 00 FF 00 01");
    expect("org #1234:dw $,$+1:db $ and #ff", "34 12 35 12 38");
    expectError("db 1/0", "Division by Zero");
    expectError("db 1 +", "Bad Expression");
    expectError("db 12q", "Bad Expression");

    // Comments, strings with colons and semicolons in them, AF'.
    expect("ld a,';' ; a comment: with a colon\nld b,\":\":ld c,\"'\"", "3E 3B 06 3A 0E 27");
    expect("ex af,af':ld a,'x':ex af,af' ; back", "08 3E 78 08");

    // Symbols: any case, set once, or more than once with LET.
    expect("Start: ld hl,START:jp start", "21 00 00 C3 00 00");
    expectError("a1 equ 1\na1 equ 2", "Duplicate Definition: a1", 2);
    expectError("here\n nop\nhere", "Duplicate Definition: here", 3);
    expect("let n = 1:db n:let n = n + 1:db n:n = n * 5:db n", "01 02 0A");
    // Values that are only known further down.
    expect("ld a,later:jp end1:later equ 7:end1", "3E 07 C3 05 00");
    expect("org first:nop:first equ second + 1:second equ #7fff:dw $", "00 01 80");
    expect("db size:data:ds size:size equ last - data:last equ data + 3", "03 00 00 00");
    expectError("loop equ loop + 1", "Undefined Symbol: loop");
    // A dot makes a label of a reserved word.
    expect(".print:.ld nop:jp print:jp ld", "00 C3 00 00 C3 00 00");
}

void testDirectives()
{
    expect("if 1:db 1:else:db 2:endif", "01");
    expect("if 0:db 1:else:db 2:endif", "02");
    expect("x equ 2\nif x=1\ndb 1\nelseif x=2\ndb 2\nelseif x=2\ndb 3\nelse\ndb 4\nendif", "02");
    expect("ifnot 0:db 1:endif:ifnot 5:db 2:endif", "01");
    expect("a2 equ 1:ifdef a2:db 1:endif:ifndef a2:db 2:endif:ifdef b2:db 3:endif:ifndef b2:db 4:endif", "01 04");
    expect("if 0:if 1:db 1:else:db 2:endif:db 3:else:db 4:endif", "04");
    expect("if 0:garbage ((:macro x:endif:db 1", "01");
    expectError("if 1:nop", "Missing ENDIF");
    expectError("endif", "ENDIF without IF");
    expectError("nop:else", "ELSE without IF");

    expect("repeat 3:db 1:rend:db 2", "01 01 01 02");
    expect("repeat 2:repeat 2:db 5:rend:db 6:rend", "05 05 06 05 05 06");
    expect("let i = 0:while i < 4:db i:let i = i + 1:wend", "00 01 02 03");
    expect("org #100:repeat 2:jr @skip:nop:@skip:rend", "18 01 00 18 01 00");
    expectError("repeat 2:nop", "Missing REND");
    expectError("nop:wend", "WEND without WHILE");
    expectError("while 1:wend", "WHILE Never Ends");

    expect("db 1:end:db 2", "01");
    expectError("db 1\nstop\ndb 2", "Assembly Stopped", 2);
    expect("db 1:nocode:db 2:here:code:db here", "01 00 02");
    expect("limit #4002:org #4000:db 1,2,3", "01 02 03");
    expectError("limit #4002\norg #4000\ndb 1,2,3,4", "Code Beyond the Limit", 3);
    expect("charset \"AB\",0:db \"ABC\",'A':charset:db \"A\"", "00 01 43 00 41");
    expect("charset 'a','z','A':db \"az!\":charset '!',0:db \"!\"", "41 5A 21 00");
    expect("db 1,2:dw checksum():checksum reset 5:db 1:db checksum()", "01 02 03 00 01 06");
    expect("dump:pause:title \"x\":list:nolist:db 1", "01");
    expectError("relocate_start", "Relocation Is Not Supported");
    expectError("equ 5", "Syntax Error");
    expectError("123", "Syntax Error");

    {
        // Code for one address stored at another.
        const AsmResult result = assemble("org #c000,#4000:here:jp here");
        CHECK(result.ok());
        CHECK_EQ(result.memory.size(), 1);
        CHECK_EQ(result.memory.at(0).address, 0x4000);
        CHECK(hexOf(result.memory.at(0).data) == "C3 00 C0");
        CHECK_EQ(result.memory.at(0).bank, -1);
    }
    {
        const AsmResult result = assemble("org #4000:db 1:org #8000:db 2,3:write direct -1,-1,#c4:org #4000:db 4");
        CHECK(result.ok());
        CHECK_EQ(result.memory.size(), 3);
        CHECK_EQ(result.memory.at(1).address, 0x8000);
        CHECK_EQ(result.memory.at(1).data.size(), 2);
        CHECK_EQ(result.memory.at(2).bank, 0xC4);
        CHECK_EQ(result.memory.at(2).address, 0x4000);
    }
    {
        const AsmResult result = assemble("org #4000\nrun start, stop\nnop\n.start ld hl,stop\n.stop nop\n");
        CHECK(result.ok());
        CHECK(result.run && *result.run == 0x4001);
        CHECK(result.breakpoint && *result.breakpoint == 0x4004);
    }
    {
        // Output to files, on the host or on the disc, then back to memory.
        const AsmResult result = assemble("org #8000\nwrite \"out.bin\"\ndb 1,2\nwrite direct \"b:prog.bin\",#8003\n"
                                          "db 3\nclose\ndb 4\nsave \"mem.bin\",#8000,#10,#c000,#20,#8000\n"
                                          "save direct \"m2\",0,1\n");
        CHECK(result.ok());
        CHECK_EQ(result.files.size(), 2);
        CHECK(result.files.at(0).name == "out.bin" && !result.files.at(0).direct);
        CHECK(hexOf(result.files.at(0).data) == "01 02");
        CHECK_EQ(result.files.at(0).loadAddress, 0x8000);
        CHECK_EQ(result.files.at(0).execAddress, -1);
        CHECK(result.files.at(1).name == "b:prog.bin" && result.files.at(1).direct);
        CHECK_EQ(result.files.at(1).loadAddress, 0x8002);
        CHECK_EQ(result.files.at(1).execAddress, 0x8003);
        CHECK(hexOf(bytesOf(result)) == "04");
        CHECK_EQ(result.memory.at(0).address, 0x8003);
        CHECK_EQ(result.saves.size(), 2);
        CHECK(result.saves.at(0).name == "mem.bin" && !result.saves.at(0).direct);
        CHECK_EQ(result.saves.at(0).regions.size(), 2);
        CHECK_EQ(result.saves.at(0).regions.at(1).first, 0xC000);
        CHECK_EQ(result.saves.at(0).regions.at(1).second, 0x20);
        CHECK_EQ(result.saves.at(0).execAddress, 0x8000);
        CHECK(result.saves.at(1).direct);
        CHECK_EQ(result.saves.at(1).execAddress, -1);
    }
    {
        const AsmResult result = assemble("start equ 500\ncount equ 10\nnolist\n"
                                          "print \"Start: $start, Count: &count\"\nprint count * 2\nprint \"a\",1,\"b\"\n");
        CHECK(result.ok());
        const std::vector<std::string> want = {"000001  0000  (01F4)        start equ 500",
                                               "000002  0000  (000A)        count equ 10", "Start: 01F4, Count: 10",
                                               "20", "a1b"};
        CHECK(result.output == want);
    }
    {
        // A long statement shows its first bytes, four to a row.
        const AsmResult result = assemble("org #4000\ndb 1,2,3,4,5,6");
        CHECK_EQ(result.output.size(), 3);
        CHECK(result.output.at(1) == "000002  4000  01 02 03 04   db 1,2,3,4,5,6");
        CHECK(result.output.at(2) == "000002  4004  05 06");
    }
}

void testMacros()
{
    {
        // Parameters are put in as text wherever they stand as a word.
        const AsmResult result = assemble("macro test a, b, c\n"
                                          "  print a\n"
                                          "  print \"hello a bc\"\n"
                                          "  print 'c'\n"
                                          "  db a, b, c\n"
                                          "  db a + b, c + 'a'\n"
                                          "  LD H,A\n"
                                          "mend\n"
                                          "nolist\n"
                                          "test 1, '2', 3\n");
        CHECK(result.ok());
        CHECK(hexOf(bytesOf(result)) == "01 32 03 33 34 26 01");
        CHECK(result.output.size() >= 3 && result.output[result.output.size() - 3] == "1");
        CHECK(result.output.size() >= 3 && result.output[result.output.size() - 2] == "hello 1 bc");
        CHECK(result.output.size() >= 3 && result.output.back() == "3");
    }
    {
        const AsmResult result = assemble("macro test a, b, c\n  db a, b, c\n  db a + b, c + 'a'\n  LD H,A\nmend\n"
                                          "\n\n\n\ntest 1, '2', 3\n");
        CHECK(result.output.size() == 9);
        CHECK(result.output.size() == 9 && result.output[6] == "000010  0000  01 32 03        db 1, '2', 3");
        CHECK(result.output.size() == 9 && result.output[7] == "000010  0003  33 34           db 1 + '2', 3 + '1'");
        CHECK(result.output.size() == 9 && result.output[8] == "000010  0005  26 01           LD H,1");
    }
    // A macro may take an instruction's name; '!' gives the instruction.
    expect("macro ld dest,src\n"
           "  if \"dest\" = \"de\"\n"
           "    if \"src\" = \"bc\"\n"
           "      !ld d,b\n"
           "      !ld e,c\n"
           "    elseif \"src\" = \"hl\"\n"
           "      !ld d,h\n"
           "      !ld e,l\n"
           "    else\n"
           "      !ld dest,src\n"
           "    endif\n"
           "  else\n"
           "    !ld dest,src\n"
           "  endif\n"
           "mend\n"
           "ld de,bc\nld DE,HL\nld de,5\nld a,(hl)\n",
           "50 59 54 5D 11 05 00 7E");
    // Local labels belong to one use of the macro.
    expectError("macro decnz_bad\n or a\n jr z,silly1\n dec a\n .silly1\nmend\ndecnz_bad\ndecnz_bad\n",
                "Duplicate Definition: silly1", 8);
    expect("macro decnz_a\n or a\n jr z,@leave\n dec a\n @leave\nmend\ndecnz_a\ndecnz_a\n", "B7 28 01 3D B7 28 01 3D");
    {
        const AsmResult result = assemble("macro curly param\n print \"H{param}o\"\nmend\nnolist\ncurly ell\n");
        CHECK(result.ok());
        CHECK(!result.output.empty() && result.output.back() == "Hello");
    }
    expect("macro swap reg1, reg2\n ld a,reg1\n ld reg1,reg2\n ld reg2,a\nmend\n"
           "macro swap_bcde\n swap b,c\n swap d,e\nmend\nswap_bcde\n",
           "78 41 4F 7A 53 5F");
    expect("macro fill n,v\n repeat n\n db v\n rend\nendm\nlab fill 3,\"x\"\ndw lab", "78 78 78 00 00");
    expectError("macro pair a\n db a\nmend\npair\n", "Bad Expression", 4);
    expectError("macro forever\n forever\nmend\nforever\n", "Nesting Too Deep", 4);
    expectError("macro open\n nop\n", "Missing MEND");
    expectError("nop\nmend", "MEND without MACRO", 2);
}

void testFiles()
{
    std::map<std::string, std::string> sources = {
        {"defs.asm", "five equ 5\nmacro put v\n db v\nmend\n"},
        {"sub/inner.asm", "db 9\nread \"deeper.asm\"\ndb oops\n"},
        {"deeper.asm", "db 8\n"},
        {"loop.asm", "read \"loop.asm\"\n"},
        {"ending.asm", "db 1\nend\ndb 2\n"},
    };
    AsmHost host;
    host.source = [&](std::string& name, const std::string&) -> std::optional<std::string> {
        const auto it = sources.find(name);
        if (it == sources.end())
            return std::nullopt;
        name = "/lib/" + name;
        return it->second;
    };
    host.binary = [&](const std::string& name, const std::string&) -> std::optional<std::vector<uint8_t>> {
        if (name != "data.bin")
            return std::nullopt;
        return std::vector<uint8_t>{10, 11, 12, 13, 14, 15};
    };
    host.memory = [](uint16_t address) { return static_cast<uint8_t>(address >> 8 ^ address); };
    {
        const AsmResult result = assemble("read \"defs.asm\"\nput five\nread 'sub/inner.asm'\ndb 7\n", "main.asm", host);
        CHECK_EQ(result.errors.size(), 1);
        CHECK(result.errors.at(0).file == "/lib/sub/inner.asm");
        CHECK_EQ(result.errors.at(0).line, 3);
        CHECK(result.errors.at(0).message == "Undefined Symbol: oops");
        CHECK(hexOf(bytesOf(result)) == "05 09 08 00 07");
        CHECK_EQ(result.lines, 4 + 4 + 3 + 1);
        CHECK(symbol(result, "five") == 5);
        for (const AsmSymbol& s : result.symbols)
            CHECK(s.name != "five" || (s.file == "/lib/defs.asm" && s.line == 1 && s.used));
    }
    {
        const AsmResult result = assemble("nop\nread \"missing.asm\"\n", "main.asm", host);
        CHECK_EQ(result.errors.size(), 1);
        CHECK(result.errors.at(0).file == "main.asm" && result.errors.at(0).line == 2);
        CHECK(result.errors.at(0).message == "File Not Found: missing.asm");
    }
    CHECK(assemble("read \"loop.asm\"", "main.asm", host).errors.at(0).message == "Nesting Too Deep");
    // END in a file read in ends everything.
    CHECK(hexOf(bytesOf(assemble("read \"ending.asm\"\ndb 3\n", "main.asm", host))) == "01");

    const auto built = [&](const std::string& source) { return hexOf(bytesOf(assemble(source, "main.asm", host))); };
    CHECK(built("incbin \"data.bin\"") == "0A 0B 0C 0D 0E 0F");
    CHECK(built("incbin \"data.bin\",2") == "0C 0D 0E 0F");
    CHECK(built("incbin \"data.bin\",1,2:db #ff") == "0B 0C FF");
    CHECK(built("incbin \"data.bin\",1,2,0:after:db after") == "0B 0C 02");
    CHECK(assemble("incbin \"data.bin\",4,3", "main.asm", host).errors.at(0).message == "Value Out of Range");
    CHECK(assemble("incbin \"none.bin\"", "main.asm", host).errors.at(0).message == "File Not Found: none.bin");

    // memory() reads a word of the machine's memory, or of what has just
    // been assembled; checksum(start,count) adds bytes up.
    CHECK(built("dw memory(#1234)") == "26 27");
    CHECK(built("org #1234:dw #beef:dw memory(#1234)") == "EF BE EF BE");
    CHECK(built("dw checksum(#1000,3)") == "33 00");
    CHECK(assemble("dw checksum(\"crc-16\")", "main.asm", host).errors.at(0).message
          == "CRC Checksums Are Not Supported");
}

}  // namespace

int main()
{
    testRoundTrip();
    testSyntax();
    testDirectives();
    testMacros();
    testFiles();
    return checkSummary("assembler");
}
