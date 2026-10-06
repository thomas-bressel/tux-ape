#include "core/assembler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace tuxape {

namespace {

constexpr int kMaxSymbolPasses = 4;
constexpr int kMaxNesting = 32;      // macro calls and files read, one in another
constexpr int kMaxLoop = 0x10000;    // times round a repeat or a while
constexpr size_t kListedBytes = 16;  // bytes of one statement the listing shows

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool isWordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '@'; }
bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string upper(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string trim(const std::string& text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && isSpace(text[begin]))
        ++begin;
    while (end > begin && isSpace(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

void skipSpaces(const std::string& text, size_t& i)
{
    while (i < text.size() && isSpace(text[i]))
        ++i;
}

// Whether a string starts at text[i]: the apostrophe of AF' is not one.
bool opensString(const std::string& text, size_t i)
{
    if (text[i] == '"')
        return true;
    if (text[i] != '\'')
        return false;
    const bool afterAf = i >= 2 && std::toupper(static_cast<unsigned char>(text[i - 2])) == 'A'
        && std::toupper(static_cast<unsigned char>(text[i - 1])) == 'F' && (i == 2 || !isWordChar(text[i - 3]));
    return !afterAf;
}

// The position after the string that starts at text[i].
size_t stringEnd(const std::string& text, size_t i)
{
    const size_t close = text.find(text[i], i + 1);
    return close == std::string::npos ? text.size() : close + 1;
}

// The bracket that closes the one at text[open], or npos.
size_t closing(const std::string& text, size_t open)
{
    int depth = 0;
    for (size_t i = open; i < text.size();) {
        if (opensString(text, i)) {
            i = stringEnd(text, i);
            continue;
        }
        if (text[i] == '(' || text[i] == '[')
            ++depth;
        else if ((text[i] == ')' || text[i] == ']') && --depth == 0)
            return i;
        ++i;
    }
    return std::string::npos;
}

// The items of a list between commas, brackets and strings kept whole.
std::vector<std::string> splitList(const std::string& text)
{
    std::vector<std::string> items;
    if (trim(text).empty())
        return items;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < text.size();) {
        if (opensString(text, i)) {
            i = stringEnd(text, i);
            continue;
        }
        if (text[i] == '(' || text[i] == '[')
            ++depth;
        else if (text[i] == ')' || text[i] == ']')
            --depth;
        else if (text[i] == ',' && depth <= 0) {
            items.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
        ++i;
    }
    items.push_back(trim(text.substr(start)));
    return items;
}

// Whether the whole of `text` is one string between quotes.
bool isString(const std::string& text)
{
    return text.size() >= 2 && (text[0] == '"' || text[0] == '\'') && text.find(text[0], 1) == text.size() - 1;
}

std::string unquote(const std::string& text)
{
    const std::string t = trim(text);
    return isString(t) ? t.substr(1, t.size() - 2) : t;
}

std::string hex4(unsigned value)
{
    char text[8];
    std::snprintf(text, sizeof text, "%04X", value & 0xFFFF);
    return text;
}

// One instruction or directive: what stands between two colons.
struct Statement {
    int file = 0;
    int line = 0;
    std::string text;    // without its comment
    std::string listed;  // as the listing shows it
};

void split(const std::string& source, int file, std::vector<Statement>& out, int& lineCount)
{
    size_t pos = 0;
    int number = 0;
    const auto add = [&](const std::string& text, std::string listed) {
        while (!listed.empty() && isSpace(listed.back()))
            listed.pop_back();
        if (!trim(listed).empty())
            out.push_back({file, number, text, listed});
    };
    while (pos < source.size()) {
        size_t end = source.find('\n', pos);
        if (end == std::string::npos)
            end = source.size();
        std::string line = source.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        ++number;
        size_t start = 0, comment = line.size();
        for (size_t i = 0; i < line.size();) {
            if (opensString(line, i)) {
                i = stringEnd(line, i);
                continue;
            }
            if (line[i] == ';') {
                comment = i;
                break;
            }
            if (line[i] == ':') {
                add(line.substr(start, i - start), line.substr(start, i - start));
                start = i + 1;
            }
            ++i;
        }
        add(line.substr(start, comment - start), line.substr(start));
    }
    lineCount += number;
}

const std::set<std::string>& mnemonics()
{
    static const std::set<std::string> kWords = {
        "ADC",  "ADD",  "AND",  "BIT",  "CALL", "CCF",  "CP",   "CPD",  "CPDR", "CPI",  "CPIR", "CPL",  "DAA", "DEC",
        "DI",   "DJNZ", "EI",   "EX",   "EXX",  "HALT", "IM",   "IN",   "INC",  "IND",  "INDR", "INI",  "INIR", "JP",
        "JR",   "LD",   "LDD",  "LDDR", "LDI",  "LDIR", "NEG",  "NOP",  "OR",   "OTDR", "OTIR", "OUT",  "OUTD", "OUTI",
        "POP",  "PUSH", "RES",  "RET",  "RETI", "RETN", "RL",   "RLA",  "RLC",  "RLCA", "RLD",  "RR",   "RRA",  "RRC",
        "RRCA", "RRD",  "RST",  "SBC",  "SCF",  "SET",  "SLA",  "SLL",  "SRA",  "SRL",  "SUB",  "XOR"};
    return kWords;
}

const std::set<std::string>& directives()
{
    static const std::set<std::string> kWords = {
        "ALIGN",  "BRK",     "BYTE",   "CHARSET", "CHECKSUM",     "CLOSE",          "CODE",           "DB",
        "DEFB",   "DEFL",    "DEFM",   "DEFS",    "DEFW",         "DM",             "DS",             "DUMP",
        "DW",     "ELSE",    "ELSEIF", "END",     "ENDIF",        "ENDM",           "EQU",            "IF",
        "IFDEF",  "IFNDEF",  "IFNOT",  "INCBIN",  "LET",          "LIMIT",          "LIST",           "MACRO",
        "MEND",   "NOCODE",  "NOLIST", "ORG",     "PAUSE",        "PRINT",          "READ",           "REND",
        "REPEAT", "RMEM",    "RUN",    "SAVE",    "STOP",         "STR",            "TEXT",           "TITLE",
        "WORD",   "WEND",    "WHILE",  "WRITE",   "RELOCATE_END", "RELOCATE_START", "RELOCATE_TABLE"};
    return kWords;
}

struct Value {
    int32_t number = 0;
    bool known = true;     // false while a symbol it comes from has no value yet
    bool literal = false;  // a string between quotes, kept in `text`
    std::string text;
};

struct Symbol {
    std::string name;
    int32_t value = 0;
    bool known = false;
    bool variable = false;  // from `let`: may be given another value
    bool used = false;
    int pass = 0;  // the pass that last defined it
    int file = 0;
    int line = 0;
};

struct Macro {
    std::vector<std::string> parameters;  // upper case
    std::vector<Statement> body;
};

// A statement's parts.
struct Head {
    std::string label;      // as written, without its dot
    std::string word;       // the instruction or directive, upper case
    std::string rest;       // what follows it
    bool standard = false;  // a '!' before the word: never a macro
    bool bad = false;
};

struct Condition {
    bool outer = true;   // the code around the IF is being assembled
    bool active = false;
    bool taken = false;  // one of its branches has been assembled
    bool hadElse = false;
};

struct Operand {
    enum Kind {
        Reg8,   // B C D E H L A
        Half,   // HX LX HY LY
        Pair,   // BC DE HL SP
        Af,
        AfAlt,
        Index,  // IX IY
        RegI,
        RegR,
        IndBc,
        IndDe,
        IndHl,
        IndSp,
        IndC,
        IndIndex,    // (IX+d)
        IndAddress,  // (nn)
        Immediate,
    };
    Kind kind = Immediate;
    int code = 0;        // the register's number in an opcode
    uint8_t prefix = 0;  // #DD or #FD where IX or IY comes in
    std::string expr;    // the value, address or displacement, still as text
    std::string text;    // the whole operand in upper case
};

struct Image {
    std::array<uint8_t, 0x10000> data{};
    std::vector<bool> written = std::vector<bool>(0x10000, false);
};

class Assembler {
public:
    explicit Assembler(const AsmHost& host)
        : host_(host)
    {
    }
    AsmResult run(const std::string& source, const std::string& fileName);

private:
    void beginPass();
    void execute(const std::vector<Statement>& code, size_t begin, size_t end);
    Head parseHead(const std::string& text) const;
    bool conditional(const Statement& st, const Head& head);
    bool skipping() const { return !conditions_.empty() && !conditions_.back().active; }
    size_t defineMacro(const std::vector<Statement>& code, size_t at, size_t end, const Head& head);
    size_t loop(const std::vector<Statement>& code, size_t at, size_t end, const Head& head);
    void perform(const Statement& st, const Head& head);
    void callMacro(const Macro& macro, const Head& head, const Statement& st);
    void block(const std::vector<Statement>& code, size_t begin, size_t end);
    bool directive(const std::string& word, const std::string& rest);
    bool instruction(const std::string& word, const std::string& rest);
    void list(const Statement& st);
    void error(const std::string& message);

    // Symbols and expressions.
    std::string symbolKey(const std::string& name, int scope) const;
    void define(const std::string& name, const Value& value, bool variable);
    void label(const Head& head);
    Value symbolValue(const std::string& name);
    Value evaluate(const std::string& text);
    Value expression(const std::string& s, size_t& i);
    Value term(const std::string& s, size_t& i);
    Value number(const std::string& digits, int base);
    Value function(const std::string& name, const std::string& s, size_t& i);
    Value apply(const std::string& op, const Value& left, const Value& right);
    int32_t fixed(const Value& value);
    uint8_t byteOf(const std::string& expr);
    uint16_t wordOf(const std::string& expr);
    uint8_t peek(uint16_t address) const;

    // Output.
    void emit(uint8_t value);
    void emitWord(uint16_t value);
    void emitString(const std::string& text, bool markLast);

    // Z80 instructions.
    Operand operand(const std::string& raw) const;
    static bool reg8(const Operand& op, int& code);
    static int condition(const Operand& op);
    uint8_t displacementOf(const Operand& op);
    void emitReg(uint8_t opcode, const Operand& op);
    bool z80(const std::string& m, std::vector<Operand>& o);
    bool arithmetic(int alu, std::vector<Operand>& o);
    bool load(const Operand& a, const Operand& b);
    bool shift(int index, std::vector<Operand>& o);
    bool bitwise(uint8_t base, std::vector<Operand>& o);

    const std::vector<Statement>* source(const std::string& name);
    const std::vector<uint8_t>* binary(const std::string& name);

    const AsmHost& host_;
    AsmResult result_;
    std::vector<std::string> files_;
    std::map<std::string, std::vector<Statement>> sources_;
    std::map<std::string, std::vector<uint8_t>> binaries_;
    std::map<std::string, Symbol> symbols_;
    std::map<std::string, Macro> macros_;
    std::map<int, std::unique_ptr<Image>> images_;

    int pass_ = 0;
    bool final_ = false;
    bool unresolved_ = false;  // something was laid out from a value not yet known
    bool changed_ = false;     // a symbol took another value than in the pass before

    // The state of a pass.
    std::vector<Condition> conditions_;
    std::vector<int> scopes_;  // where local (@) labels belong, innermost last
    int scopeCount_ = 0;
    int nesting_ = 0;
    bool ended_ = false;
    int codeAddress_ = 0;
    int outAddress_ = 0;
    int limit_ = -1;
    bool code_ = true;
    bool list_ = true;
    bool toFile_ = false;
    int bank_ = -1;
    uint32_t checksum_ = 0;
    std::array<uint8_t, 256> charset_{};

    // The statement in hand.
    int file_ = 0;
    int line_ = 0;
    int statementAddress_ = 0;
    bool failed_ = false;
    std::vector<uint8_t> listBytes_;
    std::optional<int32_t> listValue_;
};

AsmResult Assembler::run(const std::string& source, const std::string& fileName)
{
    files_.push_back(fileName);
    std::vector<Statement> main;
    split(source, 0, main, result_.lines);
    // Passes that only gather the symbols, until their values hold still,
    // then the one that writes the code.
    for (int symbolPasses = 0;;) {
        ++pass_;
        beginPass();
        execute(main, 0, main.size());
        if (!conditions_.empty())
            error("Missing ENDIF");
        if (final_)
            break;
        ++symbolPasses;
        final_ = !(unresolved_ || (symbolPasses > 1 && changed_)) || symbolPasses >= kMaxSymbolPasses;
    }
    for (const auto& [key, symbol] : symbols_) {
        if (key[0] == '@')
            continue;  // local labels: one per use of a macro
        result_.symbols.push_back({symbol.name, symbol.value, symbol.used,
                                   files_[static_cast<size_t>(symbol.file)], symbol.line});
    }
    for (const auto& [bank, image] : images_) {
        for (int a = 0; a < 0x10000;) {
            if (!image->written[static_cast<size_t>(a)]) {
                ++a;
                continue;
            }
            AsmBlock out;
            out.address = static_cast<uint16_t>(a);
            out.bank = bank;
            for (; a < 0x10000 && image->written[static_cast<size_t>(a)]; ++a)
                out.data.push_back(image->data[static_cast<size_t>(a)]);
            result_.memory.push_back(std::move(out));
        }
    }
    return std::move(result_);
}

void Assembler::beginPass()
{
    unresolved_ = false;
    changed_ = false;
    conditions_.clear();
    scopes_.clear();
    macros_.clear();
    scopeCount_ = 0;
    nesting_ = 0;
    ended_ = false;
    codeAddress_ = 0;
    outAddress_ = 0;
    limit_ = -1;
    code_ = true;
    list_ = true;
    toFile_ = false;
    bank_ = -1;
    checksum_ = 0;
    for (size_t c = 0; c < charset_.size(); ++c)
        charset_[c] = static_cast<uint8_t>(c);
}

void Assembler::error(const std::string& message)
{
    // Only the last pass knows every symbol; one message a statement.
    if (!final_ || failed_)
        return;
    failed_ = true;
    result_.errors.push_back({files_[static_cast<size_t>(file_)], line_, message});
}

void Assembler::execute(const std::vector<Statement>& code, size_t begin, size_t end)
{
    for (size_t i = begin; i < end && !ended_; ++i) {
        const Statement& st = code[i];
        file_ = st.file;
        line_ = st.line;
        failed_ = false;
        listBytes_.clear();
        listValue_.reset();
        statementAddress_ = codeAddress_;
        const Head head = parseHead(st.text);
        if (conditional(st, head) || skipping())
            continue;
        if (head.word == "MACRO")
            i = defineMacro(code, i, end, head);
        else if (head.word == "REPEAT" || head.word == "WHILE")
            i = loop(code, i, end, head);
        else
            perform(st, head);
    }
}

Head Assembler::parseHead(const std::string& text) const
{
    Head head;
    const std::string s = trim(text);
    if (s.empty())
        return head;
    size_t i = 0;
    const auto word = [&]() {
        std::string w;
        if (i < s.size() && isWordStart(s[i])) {
            w += s[i++];
            while (i < s.size() && isWordChar(s[i]))
                w += s[i++];
        }
        return w;
    };
    if (s[0] == '!') {
        head.standard = true;
        ++i;
    } else {
        // A first word that is no instruction is a label; after a dot it
        // is one whatever it says.
        const bool dotted = s[0] == '.';
        if (dotted)
            ++i;
        const std::string first = word();
        if (first.empty()) {
            head.bad = true;
            return head;
        }
        const std::string name = upper(first);
        if (!dotted && (macros_.count(name) || mnemonics().count(name) || directives().count(name))) {
            head.word = name;
            head.rest = trim(s.substr(i));
            return head;
        }
        head.label = first;
        skipSpaces(s, i);
        if (i == s.size())
            return head;
        if (s[i] == '=') {
            head.word = "=";
            head.rest = trim(s.substr(i + 1));
            return head;
        }
        if (s[i] == '!') {
            head.standard = true;
            ++i;
        }
    }
    const std::string w = word();
    if (w.empty()) {
        head.bad = true;
        return head;
    }
    head.word = upper(w);
    head.rest = trim(s.substr(i));
    return head;
}

// IF and its like are looked at even inside code that is being left out,
// to know where that code ends.
bool Assembler::conditional(const Statement& st, const Head& head)
{
    const std::string& w = head.word;
    if (w == "IF" || w == "IFNOT" || w == "IFDEF" || w == "IFNDEF") {
        Condition c;
        c.outer = !skipping();
        if (c.outer) {
            label(head);
            bool truth;
            if (w == "IFDEF" || w == "IFNDEF") {
                bool defined = false;
                for (auto scope = scopes_.rbegin(); !defined; ++scope) {
                    const auto it = symbols_.find(symbolKey(head.rest, scope == scopes_.rend() ? 0 : *scope));
                    defined = it != symbols_.end() && it->second.pass == pass_;
                    if (scope == scopes_.rend())
                        break;
                }
                truth = defined == (w == "IFDEF");
            } else {
                truth = (fixed(evaluate(head.rest)) != 0) == (w == "IF");
            }
            c.active = c.taken = truth;
            list(st);
        }
        conditions_.push_back(c);
        return true;
    }
    if (w == "ELSE" || w == "ELSEIF") {
        if (conditions_.empty()) {
            error(w + " without IF");
            return true;
        }
        Condition& c = conditions_.back();
        if (c.hadElse)
            error(w + " after ELSE");
        if (w == "ELSE")
            c.hadElse = true;
        const bool wasTaken = c.taken;
        c.active = false;
        if (c.outer && !wasTaken) {
            c.active = w == "ELSE" || fixed(evaluate(head.rest)) != 0;
            c.taken = c.active;
        }
        if (c.outer)
            list(st);
        return true;
    }
    if (w == "ENDIF") {
        if (conditions_.empty()) {
            error("ENDIF without IF");
            return true;
        }
        const bool outer = conditions_.back().outer;
        conditions_.pop_back();
        if (outer)
            list(st);
        return true;
    }
    return false;
}

size_t Assembler::defineMacro(const std::vector<Statement>& code, size_t at, size_t end, const Head& head)
{
    label(head);
    size_t i = 0;
    std::string name;
    while (i < head.rest.size() && isWordChar(head.rest[i]))
        name += head.rest[i++];
    Macro macro;
    for (const std::string& parameter : splitList(head.rest.substr(i)))
        macro.parameters.push_back(upper(parameter));
    list(code[at]);
    size_t last = at + 1;
    for (; last < end; ++last) {
        const std::string word = parseHead(code[last].text).word;
        if (word == "MEND" || word == "ENDM")
            break;
        macro.body.push_back(code[last]);
    }
    if (name.empty())
        error("Syntax Error");
    else if (last == end)
        error("Missing MEND");
    else
        macros_[upper(name)] = std::move(macro);
    for (size_t j = at + 1; j < end && j <= last; ++j) {
        line_ = code[j].line;
        statementAddress_ = codeAddress_;
        list(code[j]);
    }
    return last;
}

// The statements of a macro or of one turn of a loop: local labels and
// unfinished IFs stay inside.
void Assembler::block(const std::vector<Statement>& code, size_t begin, size_t end)
{
    const size_t depth = conditions_.size();
    scopes_.push_back(++scopeCount_);
    ++nesting_;
    execute(code, begin, end);
    --nesting_;
    scopes_.pop_back();
    if (conditions_.size() > depth) {
        error("Missing ENDIF");
        conditions_.resize(depth);
    }
}

size_t Assembler::loop(const std::vector<Statement>& code, size_t at, size_t end, const Head& head)
{
    const bool isWhile = head.word == "WHILE";
    const char* const closer = isWhile ? "WEND" : "REND";
    size_t last = at + 1;
    for (int depth = 0; last < end; ++last) {
        const std::string word = parseHead(code[last].text).word;
        if (word == head.word)
            ++depth;
        else if (word == closer && depth-- == 0)
            break;
    }
    label(head);
    if (last == end) {
        error(std::string("Missing ") + closer);
        return end;
    }
    if (nesting_ >= kMaxNesting) {
        error("Nesting Too Deep");
        return last;
    }
    const Statement& opening = code[at];
    int turns = 0;
    if (!isWhile) {
        turns = fixed(evaluate(head.rest));
        if (turns < 0 || turns > kMaxLoop) {
            error("Value Out of Range");
            turns = 0;
        }
    }
    list(opening);
    for (int turn = 0; !ended_; ++turn) {
        file_ = opening.file;
        line_ = opening.line;
        failed_ = false;
        if (isWhile ? fixed(evaluate(head.rest)) == 0 : turn >= turns)
            break;
        if (turn >= kMaxLoop) {
            error("WHILE Never Ends");
            break;
        }
        block(code, at + 1, last);
    }
    return last;
}

void Assembler::perform(const Statement& st, const Head& head)
{
    if (head.bad) {
        error("Syntax Error");
        list(st);
        return;
    }
    const std::string& w = head.word;
    if (!head.label.empty() && (w == "EQU" || w == "DEFL" || w == "=")) {
        const Value value = evaluate(head.rest);
        define(head.label, value, w == "=");
        listValue_ = value.number;
        list(st);
        return;
    }
    label(head);
    if (w.empty()) {
        list(st);
        return;
    }
    if (!head.standard) {
        const auto macro = macros_.find(w);
        if (macro != macros_.end()) {
            list(st);
            callMacro(macro->second, head, st);
            return;
        }
    }
    if (w == "READ") {
        // Another source file, assembled as if it stood here.
        const std::string name = unquote(head.rest);
        const std::vector<Statement>* statements = nesting_ < kMaxNesting ? source(name) : nullptr;
        if (nesting_ >= kMaxNesting)
            error("Nesting Too Deep");
        else if (!statements)
            error("File Not Found: " + name);
        list(st);
        if (statements) {
            ++nesting_;
            execute(*statements, 0, statements->size());
            --nesting_;
        }
        return;
    }
    if (!directive(w, head.rest) && !instruction(w, head.rest))
        error("Unknown Instruction: " + w);
    list(st);
}

void Assembler::callMacro(const Macro& macro, const Head& head, const Statement& st)
{
    if (nesting_ >= kMaxNesting) {
        error("Nesting Too Deep");
        return;
    }
    const std::vector<std::string> arguments = splitList(head.rest);
    // Each parameter gives way to its argument wherever it stands as a
    // whole word, strings included, or anywhere between curly brackets.
    const auto argument = [&](const std::string& name) -> const std::string* {
        static const std::string kNone;
        const auto it = std::find(macro.parameters.begin(), macro.parameters.end(), upper(name));
        if (it == macro.parameters.end())
            return nullptr;
        const size_t index = static_cast<size_t>(it - macro.parameters.begin());
        return index < arguments.size() ? &arguments[index] : &kNone;
    };
    const auto substitute = [&](const std::string& text) {
        std::string out;
        for (size_t i = 0; i < text.size();) {
            if (text[i] == '{') {
                const size_t close = text.find('}', i);
                const std::string* value =
                    close == std::string::npos ? nullptr : argument(trim(text.substr(i + 1, close - i - 1)));
                if (value) {
                    out += *value;
                    i = close + 1;
                    continue;
                }
            }
            if (!isWordChar(text[i])) {
                out += text[i++];
                continue;
            }
            size_t j = i;
            while (j < text.size() && isWordChar(text[j]))
                ++j;
            const std::string word = text.substr(i, j - i);
            const std::string* value = argument(word);
            out += value ? *value : word;
            i = j;
        }
        return out;
    };
    std::vector<Statement> body;
    body.reserve(macro.body.size());
    for (const Statement& line : macro.body)
        body.push_back({st.file, st.line, substitute(line.text), substitute(line.listed)});
    block(body, 0, body.size());
}

void Assembler::list(const Statement& st)
{
    if (!final_ || !list_)
        return;
    // Line, address, then the bytes or a value between brackets, then
    // the source, as WinAPE lays them out.
    const auto row = [&](int address, const std::string& field, const std::string& text) {
        char start[24];
        std::snprintf(start, sizeof start, "%06d  %04X  ", st.line, static_cast<unsigned>(address & 0xFFFF));
        std::string line = start + field;
        if (!text.empty()) {
            line.resize(std::max<size_t>(line.size(), 28), ' ');
            line += text;
        }
        result_.output.push_back(line);
    };
    const auto bytes = [&](size_t from) {
        std::string field;
        for (size_t i = from; i < listBytes_.size() && i < from + 4; ++i) {
            char text[4];
            std::snprintf(text, sizeof text, "%02X", listBytes_[i]);
            field += (i > from ? " " : "") + std::string(text);
        }
        return field;
    };
    if (listValue_ && listBytes_.empty()) {
        row(statementAddress_, "(" + hex4(static_cast<unsigned>(*listValue_)) + ")", st.listed);
        return;
    }
    row(statementAddress_, bytes(0), st.listed);
    for (size_t from = 4; from < listBytes_.size(); from += 4)
        row(statementAddress_ + static_cast<int>(from), bytes(from), {});
}

// ---------------------------------------------------------------- symbols

std::string Assembler::symbolKey(const std::string& name, int scope) const
{
    std::string key = upper(trim(name));
    if (!key.empty() && key[0] == '@')
        key += '\x01' + std::to_string(scope);
    return key;
}

void Assembler::define(const std::string& name, const Value& value, bool variable)
{
    const auto [it, created] = symbols_.try_emplace(symbolKey(name, scopes_.empty() ? 0 : scopes_.back()));
    Symbol& symbol = it->second;
    if (!created && symbol.pass == pass_ && !(variable && symbol.variable)) {
        error("Duplicate Definition: " + name);
        return;
    }
    if (created)
        symbol.name = name;
    if (!variable && (created || symbol.value != value.number || symbol.known != value.known))
        changed_ = true;
    if (!value.known)
        unresolved_ = true;
    symbol.value = value.number;
    symbol.known = value.known;
    symbol.variable = variable;
    symbol.pass = pass_;
    symbol.file = file_;
    symbol.line = line_;
}

void Assembler::label(const Head& head)
{
    if (head.label.empty())
        return;
    Value value;
    value.number = codeAddress_;
    define(head.label, value, false);
}

Value Assembler::symbolValue(const std::string& name)
{
    Value value;
    // A local label may belong to a macro or a loop around this one.
    Symbol* symbol = nullptr;
    for (auto scope = scopes_.rbegin(); !symbol; ++scope) {
        const auto it = symbols_.find(symbolKey(name, scope == scopes_.rend() ? 0 : *scope));
        if (it != symbols_.end())
            symbol = &it->second;
        if (scope == scopes_.rend() || name[0] != '@')
            break;
    }
    if (symbol)
        symbol->used = true;
    if (!symbol || !symbol->known) {
        value.known = false;
        error("Undefined Symbol: " + name);
    } else {
        value.number = symbol->value;
    }
    return value;
}

int32_t Assembler::fixed(const Value& value)
{
    if (!value.known)
        unresolved_ = true;
    return value.number;
}

Value Assembler::evaluate(const std::string& text)
{
    size_t i = 0;
    Value value = expression(text, i);
    skipSpaces(text, i);
    if (i < text.size())
        error("Bad Expression");
    return value;
}

// Operators are taken as they come, from left to right, as Maxam did.
Value Assembler::expression(const std::string& s, size_t& i)
{
    Value left = term(s, i);
    for (;;) {
        skipSpaces(s, i);
        if (i >= s.size())
            break;
        std::string op;
        if (std::isalpha(static_cast<unsigned char>(s[i]))) {
            size_t j = i;
            while (j < s.size() && isWordChar(s[j]))
                op += static_cast<char>(std::toupper(static_cast<unsigned char>(s[j++])));
            if (op != "MOD" && op != "AND" && op != "OR" && op != "XOR" && op != "SHL" && op != "SHR")
                break;
            i = j;
        } else {
            static const char* const kTwo[] = {"<<", ">>", "<=", ">=", "<>", "!=", "=="};
            for (const char* two : kTwo)
                if (s.compare(i, 2, two) == 0)
                    op = two;
            if (op.empty() && std::strchr("+-*/=<>&|^", s[i]))
                op = s[i];
            if (op.empty())
                break;
            i += op.size();
        }
        left = apply(op, left, term(s, i));
    }
    return left;
}

Value Assembler::apply(const std::string& op, const Value& left, const Value& right)
{
    Value out;
    out.known = left.known && right.known;
    const int32_t a = left.number, b = right.number;
    const uint32_t ua = static_cast<uint32_t>(a), ub = static_cast<uint32_t>(b);
    const auto truth = [](bool yes) { return yes ? -1 : 0; };
    // Two strings are compared as text, whatever their case.
    const bool texts = left.literal && right.literal;
    const int order = texts ? upper(left.text).compare(upper(right.text)) : a < b ? -1 : a > b ? 1 : 0;
    if (op == "+")
        out.number = static_cast<int32_t>(ua + ub);
    else if (op == "-")
        out.number = static_cast<int32_t>(ua - ub);
    else if (op == "*")
        out.number = static_cast<int32_t>(ua * ub);
    else if (op == "/" || op == "MOD") {
        if (b == 0) {
            if (out.known)
                error("Division by Zero");
        } else if (b == -1) {
            out.number = op == "/" ? static_cast<int32_t>(0u - ua) : 0;
        } else {
            out.number = op == "/" ? a / b : a % b;
        }
    } else if (op == "AND" || op == "&")
        out.number = a & b;
    else if (op == "OR" || op == "|")
        out.number = a | b;
    else if (op == "XOR" || op == "^")
        out.number = a ^ b;
    else if (op == "SHL" || op == "<<")
        out.number = static_cast<int32_t>(ua << (ub & 31));
    else if (op == "SHR" || op == ">>")
        out.number = static_cast<int32_t>(ua >> (ub & 31));
    else if (op == "=" || op == "==")
        out.number = truth(order == 0);
    else if (op == "<>" || op == "!=")
        out.number = truth(order != 0);
    else if (op == "<")
        out.number = truth(order < 0);
    else if (op == ">")
        out.number = truth(order > 0);
    else if (op == "<=")
        out.number = truth(order <= 0);
    else
        out.number = truth(order >= 0);
    return out;
}

Value Assembler::number(const std::string& digits, int base)
{
    Value value;
    uint32_t n = 0;
    bool bad = digits.empty();
    for (const char c : digits) {
        const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
            : std::isalpha(static_cast<unsigned char>(c)) ? std::toupper(static_cast<unsigned char>(c)) - 'A' + 10 : 99;
        if (digit >= base)
            bad = true;
        n = n * static_cast<uint32_t>(base) + static_cast<uint32_t>(digit);
    }
    if (bad)
        error("Bad Expression");
    else
        value.number = static_cast<int32_t>(n);
    return value;
}

Value Assembler::term(const std::string& s, size_t& i)
{
    skipSpaces(s, i);
    if (i >= s.size()) {
        error("Bad Expression");
        return {};
    }
    const char c = s[i];
    if (c == '-' || c == '+' || c == '~' || c == '!') {
        ++i;
        Value value = term(s, i);
        value.literal = false;
        if (c == '-')
            value.number = static_cast<int32_t>(0u - static_cast<uint32_t>(value.number));
        else if (c == '~')
            value.number = ~value.number;
        else if (c == '!')
            value.number = value.number ? 0 : -1;
        return value;
    }
    if (c == '(' || c == '[') {
        ++i;
        Value value = expression(s, i);
        value.literal = false;
        skipSpaces(s, i);
        if (i < s.size() && s[i] == (c == '(' ? ')' : ']')) {
            ++i;
        } else {
            error("Bad Expression");
            i = s.size();
        }
        return value;
    }
    if (c == '"' || c == '\'') {
        const size_t close = s.find(c, i + 1);
        Value value;
        if (close == std::string::npos) {
            error("Bad Expression");
            i = s.size();
            return value;
        }
        value.literal = true;
        value.text = s.substr(i + 1, close - i - 1);
        i = close + 1;
        if (!value.text.empty())
            value.number = charset_[static_cast<uint8_t>(value.text[0])];
        if (value.text.size() == 2)
            value.number |= charset_[static_cast<uint8_t>(value.text[1])] << 8;
        return value;
    }
    const auto run = [&](bool (*accept)(char)) {
        std::string text;
        while (i < s.size() && accept(s[i]))
            text += s[i++];
        return text;
    };
    if (c == '$') {
        ++i;
        if (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i])))
            return number(run(isWordChar), 16);
        Value value;
        value.number = statementAddress_;
        return value;
    }
    if (c == '#' || c == '&' || c == '%') {
        ++i;
        return number(run(isWordChar), c == '%' ? 2 : 16);
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
        const std::string digits = upper(run(isWordChar));
        if (digits.size() > 2 && digits[0] == '0' && digits[1] == 'X')
            return number(digits.substr(2), 16);
        if (digits.back() == 'H')
            return number(digits.substr(0, digits.size() - 1), 16);
        if (digits.back() == 'B')
            return number(digits.substr(0, digits.size() - 1), 2);
        return number(digits, 10);
    }
    if (isWordStart(c)) {
        std::string name(1, s[i++]);
        name += run(isWordChar);
        const std::string word = upper(name);
        if (word == "NOT") {
            Value value = term(s, i);
            value.literal = false;
            value.number = ~value.number;
            return value;
        }
        size_t next = i;
        skipSpaces(s, next);
        if ((word == "CHECKSUM" || word == "MEMORY") && next < s.size() && s[next] == '(') {
            i = next;
            return function(word, s, i);
        }
        return symbolValue(name);
    }
    error("Bad Expression");
    i = s.size();
    return {};
}

Value Assembler::function(const std::string& name, const std::string& s, size_t& i)
{
    Value value;
    const size_t close = closing(s, i);
    if (close == std::string::npos) {
        error("Bad Expression");
        i = s.size();
        return value;
    }
    const std::vector<std::string> arguments = splitList(s.substr(i + 1, close - i - 1));
    i = close + 1;
    if (name == "MEMORY" && arguments.size() == 1) {
        const Value address = evaluate(arguments[0]);
        value.known = address.known;
        const uint16_t a = static_cast<uint16_t>(address.number);
        value.number = peek(a) | peek(static_cast<uint16_t>(a + 1)) << 8;
    } else if (name == "CHECKSUM" && arguments.empty()) {
        value.number = static_cast<int32_t>(checksum_);
    } else if (name == "CHECKSUM" && arguments.size() == 2 && !isString(arguments[0])) {
        const Value start = evaluate(arguments[0]), count = evaluate(arguments[1]);
        value.known = start.known && count.known;
        uint32_t sum = 0;
        for (int32_t n = 0; n < count.number && n < 0x10000; ++n)
            sum += peek(static_cast<uint16_t>(start.number + n));
        value.number = static_cast<int32_t>(sum);
    } else if (name == "CHECKSUM") {
        error("CRC Checksums Are Not Supported");
    } else {
        error("Bad Expression");
    }
    return value;
}

uint8_t Assembler::peek(uint16_t address) const
{
    // What this assembly has already written counts as being in memory.
    const auto image = images_.find(-1);
    if (image != images_.end() && image->second->written[address])
        return image->second->data[address];
    return host_.memory ? host_.memory(address) : uint8_t(0);
}

uint8_t Assembler::byteOf(const std::string& expr)
{
    const Value value = evaluate(expr);
    if (value.known && (value.number < -128 || value.number > 255))
        error("Value Out of Range");
    return static_cast<uint8_t>(value.number);
}

uint16_t Assembler::wordOf(const std::string& expr)
{
    const Value value = evaluate(expr);
    if (value.known && (value.number < -32768 || value.number > 65535))
        error("Value Out of Range");
    return static_cast<uint16_t>(value.number);
}

// ----------------------------------------------------------------- output

void Assembler::emit(uint8_t value)
{
    if (limit_ >= 0 && codeAddress_ > limit_)
        error("Code Beyond the Limit");
    if (final_) {
        if (code_ && !toFile_ && listBytes_.empty()) {
            const std::string& file = files_[static_cast<size_t>(file_)];
            if (result_.addresses.empty() || result_.addresses.back().line != line_
                || result_.addresses.back().file != file)
                result_.addresses.push_back({file, line_, static_cast<uint16_t>(codeAddress_)});
        }
        if (listBytes_.size() < kListedBytes)
            listBytes_.push_back(value);
        if (code_ && toFile_) {
            AsmFile& file = result_.files.back();
            if (file.data.empty())
                file.loadAddress = static_cast<uint16_t>(codeAddress_);
            file.data.push_back(value);
        } else if (code_) {
            std::unique_ptr<Image>& image = images_[bank_];
            if (!image)
                image = std::make_unique<Image>();
            image->data[static_cast<size_t>(outAddress_)] = value;
            image->written[static_cast<size_t>(outAddress_)] = true;
        }
        if (code_)
            ++result_.bytes;
    }
    checksum_ += value;
    codeAddress_ = (codeAddress_ + 1) & 0xFFFF;
    outAddress_ = (outAddress_ + 1) & 0xFFFF;
}

void Assembler::emitWord(uint16_t value)
{
    emit(static_cast<uint8_t>(value));
    emit(static_cast<uint8_t>(value >> 8));
}

void Assembler::emitString(const std::string& text, bool markLast)
{
    for (size_t i = 0; i < text.size(); ++i) {
        const uint8_t c = charset_[static_cast<uint8_t>(text[i])];
        emit(markLast && i + 1 == text.size() ? c | 0x80 : c);
    }
}

const std::vector<Statement>* Assembler::source(const std::string& name)
{
    const std::string key = files_[static_cast<size_t>(file_)] + '\n' + name;
    auto it = sources_.find(key);
    if (it == sources_.end()) {
        std::string found = name;
        const std::optional<std::string> text =
            host_.source ? host_.source(found, files_[static_cast<size_t>(file_)]) : std::nullopt;
        if (!text)
            return nullptr;
        files_.push_back(found);
        std::vector<Statement> statements;
        split(*text, static_cast<int>(files_.size() - 1), statements, result_.lines);
        it = sources_.emplace(key, std::move(statements)).first;
    }
    return &it->second;
}

const std::vector<uint8_t>* Assembler::binary(const std::string& name)
{
    const std::string key = files_[static_cast<size_t>(file_)] + '\n' + name;
    auto it = binaries_.find(key);
    if (it == binaries_.end()) {
        std::optional<std::vector<uint8_t>> data =
            host_.binary ? host_.binary(name, files_[static_cast<size_t>(file_)]) : std::nullopt;
        if (!data)
            return nullptr;
        it = binaries_.emplace(key, std::move(*data)).first;
    }
    return &it->second;
}

// ------------------------------------------------------------- directives

bool Assembler::directive(const std::string& w, const std::string& rest)
{
    if (!directives().count(w) && w != "=")
        return false;
    const std::vector<std::string> args = splitList(rest);
    const auto count = [&](size_t least, size_t most) {
        if (args.size() >= least && args.size() <= most)
            return true;
        error("Bad Expression");
        return false;
    };

    if (w == "ORG") {
        if (count(1, 2)) {
            codeAddress_ = fixed(evaluate(args[0])) & 0xFFFF;
            outAddress_ = args.size() == 2 ? fixed(evaluate(args[1])) & 0xFFFF : codeAddress_;
            listValue_ = codeAddress_;
        }
    } else if (w == "ALIGN") {
        if (count(1, 2)) {
            const int32_t boundary = fixed(evaluate(args[0]));
            const uint8_t fill = args.size() == 2 ? byteOf(args[1]) : uint8_t(0);
            if (boundary < 1 || boundary > 0x10000)
                error("Value Out of Range");
            else
                while (codeAddress_ % boundary)
                    emit(fill);
        }
    } else if (w == "BRK") {
        emit(0xF7);  // RST #30, as Maxam's breakpoints were
    } else if (w == "BYTE" || w == "DB" || w == "DEFB" || w == "DEFM" || w == "DM" || w == "TEXT" || w == "STR") {
        if (count(1, SIZE_MAX)) {
            for (const std::string& arg : args) {
                if (isString(arg))
                    emitString(arg.substr(1, arg.size() - 2), w == "STR");
                else
                    emit(byteOf(arg));
            }
        }
    } else if (w == "DEFW" || w == "DW" || w == "WORD") {
        if (count(1, SIZE_MAX))
            for (const std::string& arg : args)
                emitWord(wordOf(arg));
    } else if (w == "DEFS" || w == "DS" || w == "RMEM") {
        if (count(1, SIZE_MAX)) {
            for (size_t i = 0; i < args.size(); i += 2) {
                const int32_t size = fixed(evaluate(args[i]));
                const uint8_t fill = i + 1 < args.size() ? byteOf(args[i + 1]) : uint8_t(0);
                if (size < 0 || size > 0x10000) {
                    error("Value Out of Range");
                    break;
                }
                for (int32_t n = 0; n < size; ++n)
                    emit(fill);
            }
        }
    } else if (w == "CHARSET") {
        if (args.empty()) {
            for (size_t c = 0; c < charset_.size(); ++c)
                charset_[c] = static_cast<uint8_t>(c);
        } else if (args.size() == 2 && isString(args[0])) {
            int32_t value = fixed(evaluate(args[1]));
            for (const char c : args[0].substr(1, args[0].size() - 2))
                charset_[static_cast<uint8_t>(c)] = static_cast<uint8_t>(value++);
        } else if (count(2, 3)) {
            // The characters named here are meant as themselves, not as
            // an earlier charset made them.
            const std::array<uint8_t, 256> current = charset_;
            for (size_t c = 0; c < charset_.size(); ++c)
                charset_[c] = static_cast<uint8_t>(c);
            const int32_t first = fixed(evaluate(args[0]));
            const int32_t last = args.size() == 3 ? fixed(evaluate(args[1])) : first;
            charset_ = current;
            int32_t value = fixed(evaluate(args.back()));
            if (first < 0 || last > 255 || first > last)
                error("Value Out of Range");
            else
                for (int32_t c = first; c <= last; ++c)
                    charset_[static_cast<size_t>(c)] = static_cast<uint8_t>(value++);
        }
    } else if (w == "CHECKSUM") {
        size_t i = 0;
        std::string word;
        while (i < rest.size() && isWordChar(rest[i]))
            word += rest[i++];
        if (upper(word) != "RESET")
            error("Bad Expression");
        else
            checksum_ = trim(rest.substr(i)).empty() ? 0 : static_cast<uint32_t>(fixed(evaluate(rest.substr(i))));
    } else if (w == "CLOSE") {
        toFile_ = false;
        bank_ = -1;
    } else if (w == "CODE" || w == "NOCODE") {
        code_ = w == "CODE";
    } else if (w == "LIST" || w == "NOLIST") {
        list_ = w == "LIST";
    } else if (w == "DUMP" || w == "PAUSE" || w == "TITLE") {
        // Maxam's printing: nothing to do here, as in WinAPE.
    } else if (w == "END") {
        ended_ = true;
    } else if (w == "STOP") {
        error("Assembly Stopped");
        ended_ = true;
    } else if (w == "LET") {
        const size_t equals = rest.find('=');
        const std::string name = equals == std::string::npos ? std::string() : trim(rest.substr(0, equals));
        if (name.empty() || !isWordStart(name[0]) || !std::all_of(name.begin() + 1, name.end(), isWordChar)) {
            error("Syntax Error");
        } else {
            const Value value = evaluate(rest.substr(equals + 1));
            define(name, value, true);
            listValue_ = value.number;
        }
    } else if (w == "LIMIT") {
        if (count(1, 1)) {
            limit_ = fixed(evaluate(args[0])) & 0xFFFF;
            listValue_ = limit_;
        }
    } else if (w == "RUN") {
        if (count(1, 2)) {
            const uint16_t address = wordOf(args[0]);
            listValue_ = address;
            if (final_) {
                result_.run = address;
                if (args.size() == 2)
                    result_.breakpoint = wordOf(args[1]);
            }
        }
    } else if (w == "PRINT") {
        if (final_) {
            std::string line;
            for (const std::string& arg : args) {
                if (!isString(arg)) {
                    line += std::to_string(evaluate(arg).number);
                    continue;
                }
                // In a string, $name gives a symbol in hexadecimal and
                // &name in decimal.
                const std::string text = arg.substr(1, arg.size() - 2);
                for (size_t i = 0; i < text.size();) {
                    if ((text[i] != '$' && text[i] != '&') || i + 1 >= text.size() || !isWordStart(text[i + 1])) {
                        line += text[i++];
                        continue;
                    }
                    size_t j = i + 2;
                    while (j < text.size() && isWordChar(text[j]))
                        ++j;
                    const Value value = symbolValue(text.substr(i + 1, j - i - 1));
                    line += text[i] == '$' ? hex4(static_cast<unsigned>(value.number)) : std::to_string(value.number);
                    i = j;
                }
            }
            result_.output.push_back(line);
        }
    } else if (w == "INCBIN") {
        if (count(1, 4)) {
            const std::string name = unquote(args[0]);
            const std::vector<uint8_t>* data = binary(name);
            if (!data) {
                error("File Not Found: " + name);
            } else {
                int64_t offset = args.size() > 1 ? fixed(evaluate(args[1])) : 0;
                if (args.size() > 3)
                    offset += static_cast<int64_t>(fixed(evaluate(args[3]))) * 0x10000;
                int64_t size = args.size() > 2 ? fixed(evaluate(args[2])) : static_cast<int64_t>(data->size()) - offset;
                if (offset < 0 || size < 0 || offset + size > static_cast<int64_t>(data->size()) || size > 0x10000)
                    error("Value Out of Range");
                else
                    for (int64_t n = 0; n < size; ++n)
                        emit((*data)[static_cast<size_t>(offset + n)]);
            }
        }
    } else if (w == "WRITE" || w == "SAVE") {
        // "direct" sends a file to the disc in the drive; on its own it
        // sends the code back to the machine's memory.
        std::vector<std::string> items = args;
        bool direct = false;
        if (!items.empty() && upper(items[0]).compare(0, 6, "DIRECT") == 0
            && (items[0].size() == 6 || !isWordChar(items[0][6]))) {
            direct = true;
            items[0] = trim(items[0].substr(6));
            if (items[0].empty())
                items.erase(items.begin());
        }
        if (w == "WRITE" && direct && (items.empty() || !isString(items[0]))) {
            if (!items.empty() && upper(items[0]).compare(0, 7, "SECTORS") == 0) {
                error("Writing to Sectors Is Not Supported");
            } else {
                toFile_ = false;
                bank_ = -1;
                if (items.size() >= 3) {
                    const int32_t bank = fixed(evaluate(items[2]));
                    bank_ = bank >= 0xC0 ? bank : -1;
                }
            }
        } else if (items.empty() || unquote(items[0]).empty()) {
            error("Bad Expression");
        } else if (w == "WRITE") {
            const int exec = items.size() > 1 ? wordOf(items[1]) : -1;
            toFile_ = true;
            if (final_) {
                AsmFile file;
                file.name = unquote(items[0]);
                file.direct = direct;
                file.execAddress = exec;
                result_.files.push_back(std::move(file));
            }
        } else {
            AsmSave save;
            save.name = unquote(items[0]);
            save.direct = direct;
            size_t i = 1;
            for (; i + 1 < items.size(); i += 2)
                save.regions.emplace_back(wordOf(items[i]), static_cast<int>(wordOf(items[i + 1])));
            if (i < items.size())
                save.execAddress = wordOf(items[i]);
            if (final_)
                result_.saves.push_back(std::move(save));
        }
    } else if (w == "MEND" || w == "ENDM" || w == "REND" || w == "WEND") {
        error(w + " without " + (w == "REND" ? "REPEAT" : w == "WEND" ? "WHILE" : "MACRO"));
    } else if (w == "RELOCATE_START" || w == "RELOCATE_END" || w == "RELOCATE_TABLE") {
        error("Relocation Is Not Supported");
    } else {
        error("Syntax Error");  // EQU or DEFL without a label, and the like
    }
    return true;
}

// ----------------------------------------------------------- instructions

Operand Assembler::operand(const std::string& raw) const
{
    Operand op;
    op.expr = trim(raw);
    op.text = upper(op.expr);
    const std::string& t = op.text;
    const auto is = [&](Operand::Kind kind, int code = 0, uint8_t prefix = 0) {
        op.kind = kind;
        op.code = code;
        op.prefix = prefix;
        return op;
    };
    static const char* const kReg8[8] = {"B", "C", "D", "E", "H", "L", "", "A"};
    static const char* const kPair[4] = {"BC", "DE", "HL", "SP"};
    for (int r = 0; r < 8; ++r)
        if (r != 6 && t == kReg8[r])
            return is(Operand::Reg8, r);
    for (int p = 0; p < 4; ++p)
        if (t == kPair[p])
            return is(Operand::Pair, p);
    if (t == "AF")
        return is(Operand::Af);
    if (t == "AF'")
        return is(Operand::AfAlt);
    if (t == "IX" || t == "IY")
        return is(Operand::Index, 2, t == "IX" ? 0xDD : 0xFD);
    if (t == "I")
        return is(Operand::RegI);
    if (t == "R")
        return is(Operand::RegR);
    if (t == "HX" || t == "IXH")
        return is(Operand::Half, 4, 0xDD);
    if (t == "LX" || t == "IXL")
        return is(Operand::Half, 5, 0xDD);
    if (t == "HY" || t == "IYH")
        return is(Operand::Half, 4, 0xFD);
    if (t == "LY" || t == "IYL")
        return is(Operand::Half, 5, 0xFD);
    if (t.size() >= 2 && t[0] == '(' && closing(t, 0) == t.size() - 1) {
        const std::string inner = trim(op.expr.substr(1, op.expr.size() - 2));
        const std::string u = upper(inner);
        op.expr = inner;
        if (u == "BC")
            return is(Operand::IndBc);
        if (u == "DE")
            return is(Operand::IndDe);
        if (u == "HL")
            return is(Operand::IndHl, 6);
        if (u == "SP")
            return is(Operand::IndSp);
        if (u == "C")
            return is(Operand::IndC);
        if (u.compare(0, 2, "IX") == 0 || u.compare(0, 2, "IY") == 0) {
            const std::string offset = trim(inner.substr(2));
            if (offset.empty() || offset[0] == '+' || offset[0] == '-') {
                op.expr = offset;
                return is(Operand::IndIndex, 6, u[1] == 'X' ? 0xDD : 0xFD);
            }
        }
        return is(Operand::IndAddress);
    }
    return is(Operand::Immediate);
}

// An operand that goes into the three register bits of an opcode.
bool Assembler::reg8(const Operand& op, int& code)
{
    code = op.code;
    return op.kind == Operand::Reg8 || op.kind == Operand::Half || op.kind == Operand::IndHl
        || op.kind == Operand::IndIndex;
}

int Assembler::condition(const Operand& op)
{
    static const char* const kNames[8] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
    for (int c = 0; c < 8; ++c)
        if (op.text == kNames[c])
            return c;
    return -1;
}

uint8_t Assembler::displacementOf(const Operand& op)
{
    if (op.expr.empty())
        return 0;
    const Value value = evaluate(op.expr);
    if (value.known && (value.number < -128 || value.number > 127))
        error("Value Out of Range");
    return static_cast<uint8_t>(value.number);
}

// An opcode with a register in it: the index prefix before, the
// displacement after.
void Assembler::emitReg(uint8_t opcode, const Operand& op)
{
    if (op.prefix)
        emit(op.prefix);
    emit(opcode);
    if (op.kind == Operand::IndIndex)
        emit(displacementOf(op));
}

bool Assembler::instruction(const std::string& word, const std::string& rest)
{
    if (!mnemonics().count(word))
        return false;
    std::vector<Operand> operands;
    for (const std::string& item : splitList(rest))
        operands.push_back(operand(item));
    if (!z80(word, operands))
        error("Invalid Operand");
    return true;
}

bool Assembler::arithmetic(int alu, std::vector<Operand>& o)
{
    if (o.size() == 2 && o[0].kind == Operand::Pair && o[0].code == 2) {
        if (o[1].kind != Operand::Pair)
            return false;
        if (alu == 0) {
            emit(static_cast<uint8_t>(0x09 | o[1].code << 4));
        } else if (alu == 1 || alu == 3) {
            emit(0xED);
            emit(static_cast<uint8_t>((alu == 1 ? 0x4A : 0x42) | o[1].code << 4));
        } else {
            return false;
        }
        return true;
    }
    if (o.size() == 2 && o[0].kind == Operand::Index) {
        // ADD IX,pp: IX itself stands where HL would.
        const bool pair = o[1].kind == Operand::Pair && o[1].code != 2;
        const bool same = o[1].kind == Operand::Index && o[1].prefix == o[0].prefix;
        if (alu != 0 || (!pair && !same))
            return false;
        emit(o[0].prefix);
        emit(static_cast<uint8_t>(0x09 | o[1].code << 4));
        return true;
    }
    // The accumulator may be named or left out.
    if (o.size() == 2 && !(o[0].kind == Operand::Reg8 && o[0].code == 7))
        return false;
    if (o.empty() || o.size() > 2)
        return false;
    const Operand& source = o.back();
    int code;
    if (reg8(source, code)) {
        emitReg(static_cast<uint8_t>(0x80 | alu << 3 | code), source);
        return true;
    }
    if (source.kind != Operand::Immediate)
        return false;
    emit(static_cast<uint8_t>(0xC6 | alu << 3));
    emit(byteOf(source.expr));
    return true;
}

bool Assembler::load(const Operand& a, const Operand& b)
{
    using K = Operand::Kind;
    int ca, cb;
    const bool ra = reg8(a, ca), rb = reg8(b, cb);
    if (ra && rb) {
        if (ca == 6 && cb == 6)
            return false;
        if (ca == 6 || cb == 6) {
            // Beside (HL) or (IX+d), only the plain registers.
            if ((ca == 6 ? b : a).kind != K::Reg8)
                return false;
        } else if (a.kind == K::Half || b.kind == K::Half) {
            // The halves of IX do not go with H, L or the halves of IY.
            const uint8_t prefix = a.kind == K::Half ? a.prefix : b.prefix;
            for (const Operand* op : {&a, &b}) {
                if (op->kind == K::Half ? op->prefix != prefix : op->code == 4 || op->code == 5)
                    return false;
            }
        }
        if (a.prefix | b.prefix)
            emit(static_cast<uint8_t>(a.prefix | b.prefix));
        emit(static_cast<uint8_t>(0x40 | ca << 3 | cb));
        if (a.kind == K::IndIndex)
            emit(displacementOf(a));
        else if (b.kind == K::IndIndex)
            emit(displacementOf(b));
        return true;
    }
    if (ra && b.kind == K::Immediate) {
        emitReg(static_cast<uint8_t>(0x06 | ca << 3), a);
        emit(byteOf(b.expr));
        return true;
    }
    const auto extended = [&](uint8_t opcode) {
        emit(0xED);
        emit(opcode);
        return true;
    };
    if (a.kind == K::Reg8 && a.code == 7) {
        switch (b.kind) {
        case K::IndBc: emit(0x0A); return true;
        case K::IndDe: emit(0x1A); return true;
        case K::IndAddress:
            emit(0x3A);
            emitWord(wordOf(b.expr));
            return true;
        case K::RegI: return extended(0x57);
        case K::RegR: return extended(0x5F);
        default: return false;
        }
    }
    if (b.kind == K::Reg8 && b.code == 7) {
        switch (a.kind) {
        case K::IndBc: emit(0x02); return true;
        case K::IndDe: emit(0x12); return true;
        case K::IndAddress:
            emit(0x32);
            emitWord(wordOf(a.expr));
            return true;
        case K::RegI: return extended(0x47);
        case K::RegR: return extended(0x4F);
        default: return false;
        }
    }
    if (a.kind == K::Pair || a.kind == K::Index) {
        if (b.kind == K::Immediate) {
            if (a.prefix)
                emit(a.prefix);
            emit(static_cast<uint8_t>(0x01 | a.code << 4));
            emitWord(wordOf(b.expr));
            return true;
        }
        if (b.kind == K::IndAddress) {
            if (a.code == 2) {
                if (a.prefix)
                    emit(a.prefix);
                emit(0x2A);
            } else {
                extended(static_cast<uint8_t>(0x4B | a.code << 4));
            }
            emitWord(wordOf(b.expr));
            return true;
        }
        if (a.kind == K::Pair && a.code == 3 && (b.kind == K::Index || (b.kind == K::Pair && b.code == 2))) {
            if (b.prefix)
                emit(b.prefix);
            emit(0xF9);
            return true;
        }
        return false;
    }
    if (a.kind == K::IndAddress && (b.kind == K::Pair || b.kind == K::Index)) {
        if (b.code == 2) {
            if (b.prefix)
                emit(b.prefix);
            emit(0x22);
        } else {
            extended(static_cast<uint8_t>(0x43 | b.code << 4));
        }
        emitWord(wordOf(a.expr));
        return true;
    }
    return false;
}

// RLC and its like; with (IX+d) a register may follow, which the result
// is copied into.
bool Assembler::shift(int index, std::vector<Operand>& o)
{
    if (o.empty() || o.size() > 2)
        return false;
    const Operand& target = o[0];
    if (o.size() == 2 && (target.kind != Operand::IndIndex || o[1].kind != Operand::Reg8))
        return false;
    if (target.kind != Operand::Reg8 && target.kind != Operand::IndHl && target.kind != Operand::IndIndex)
        return false;
    const int code = o.size() == 2 ? o[1].code : target.code;
    if (target.prefix)
        emit(target.prefix);
    emit(0xCB);
    if (target.kind == Operand::IndIndex)
        emit(displacementOf(target));
    emit(static_cast<uint8_t>(index << 3 | code));
    return true;
}

bool Assembler::bitwise(uint8_t base, std::vector<Operand>& o)
{
    if (o.size() < 2 || o.size() > 3 || o[0].kind != Operand::Immediate)
        return false;
    const Operand& target = o[1];
    if (o.size() == 3 && (base == 0x40 || target.kind != Operand::IndIndex || o[2].kind != Operand::Reg8))
        return false;
    if (target.kind != Operand::Reg8 && target.kind != Operand::IndHl && target.kind != Operand::IndIndex)
        return false;
    const Value bit = evaluate(o[0].expr);
    if (bit.known && (bit.number < 0 || bit.number > 7))
        error("Value Out of Range");
    const int code = o.size() == 3 ? o[2].code : target.code;
    if (target.prefix)
        emit(target.prefix);
    emit(0xCB);
    if (target.kind == Operand::IndIndex)
        emit(displacementOf(target));
    emit(static_cast<uint8_t>(base | (bit.number & 7) << 3 | code));
    return true;
}

bool Assembler::z80(const std::string& m, std::vector<Operand>& o)
{
    using K = Operand::Kind;
    static const std::map<std::string, std::vector<uint8_t>> kPlain = {
        {"NOP", {0x00}},        {"RLCA", {0x07}},       {"RRCA", {0x0F}},       {"RLA", {0x17}},
        {"RRA", {0x1F}},        {"DAA", {0x27}},        {"CPL", {0x2F}},        {"SCF", {0x37}},
        {"CCF", {0x3F}},        {"HALT", {0x76}},       {"EXX", {0xD9}},        {"DI", {0xF3}},
        {"EI", {0xFB}},         {"NEG", {0xED, 0x44}},  {"RETN", {0xED, 0x45}}, {"RETI", {0xED, 0x4D}},
        {"RRD", {0xED, 0x67}},  {"RLD", {0xED, 0x6F}},  {"LDI", {0xED, 0xA0}},  {"CPI", {0xED, 0xA1}},
        {"INI", {0xED, 0xA2}},  {"OUTI", {0xED, 0xA3}}, {"LDD", {0xED, 0xA8}},  {"CPD", {0xED, 0xA9}},
        {"IND", {0xED, 0xAA}},  {"OUTD", {0xED, 0xAB}}, {"LDIR", {0xED, 0xB0}}, {"CPIR", {0xED, 0xB1}},
        {"INIR", {0xED, 0xB2}}, {"OTIR", {0xED, 0xB3}}, {"LDDR", {0xED, 0xB8}}, {"CPDR", {0xED, 0xB9}},
        {"INDR", {0xED, 0xBA}}, {"OTDR", {0xED, 0xBB}}};
    const auto plain = kPlain.find(m);
    if (plain != kPlain.end()) {
        if (!o.empty())
            return false;
        for (const uint8_t byte : plain->second)
            emit(byte);
        return true;
    }
    static const char* const kAlu[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
    for (int alu = 0; alu < 8; ++alu)
        if (m == kAlu[alu])
            return arithmetic(alu, o);
    static const char* const kShift[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};
    for (int index = 0; index < 8; ++index)
        if (m == kShift[index])
            return shift(index, o);
    if (m == "BIT" || m == "RES" || m == "SET")
        return bitwise(m == "BIT" ? 0x40 : m == "RES" ? 0x80 : 0xC0, o);
    const size_t n = o.size();
    // The jump of JR and DJNZ is counted from the instruction after.
    const auto relative = [&](const Operand& target) {
        const Value value = evaluate(target.expr);
        const int32_t offset = value.number - (statementAddress_ + 2);
        if (value.known && (offset < -128 || offset > 127))
            error("Relative Jump Out of Range");
        return static_cast<uint8_t>(offset);
    };

    if (m == "LD")
        return n == 2 && load(o[0], o[1]);
    if (m == "INC" || m == "DEC") {
        if (n != 1)
            return false;
        const bool dec = m == "DEC";
        int code;
        if (reg8(o[0], code)) {
            emitReg(static_cast<uint8_t>((dec ? 0x05 : 0x04) | code << 3), o[0]);
        } else if (o[0].kind == K::Pair || o[0].kind == K::Index) {
            if (o[0].prefix)
                emit(o[0].prefix);
            emit(static_cast<uint8_t>((dec ? 0x0B : 0x03) | o[0].code << 4));
        } else {
            return false;
        }
        return true;
    }
    if (m == "PUSH" || m == "POP") {
        if (n != 1)
            return false;
        const bool pair = o[0].kind == K::Pair && o[0].code != 3;
        if (!pair && o[0].kind != K::Af && o[0].kind != K::Index)
            return false;
        if (o[0].prefix)
            emit(o[0].prefix);
        emit(static_cast<uint8_t>((m == "PUSH" ? 0xC5 : 0xC1) | (o[0].kind == K::Af ? 3 : o[0].code) << 4));
        return true;
    }
    if (m == "EX") {
        if (n != 2)
            return false;
        const auto hl = [](const Operand& op) { return op.kind == K::Pair && op.code == 2; };
        const auto de = [](const Operand& op) { return op.kind == K::Pair && op.code == 1; };
        if (o[0].kind == K::Af && o[1].kind == K::AfAlt) {
            emit(0x08);
        } else if ((de(o[0]) && hl(o[1])) || (hl(o[0]) && de(o[1]))) {
            emit(0xEB);
        } else if (o[0].kind == K::IndSp && (hl(o[1]) || o[1].kind == K::Index)) {
            if (o[1].prefix)
                emit(o[1].prefix);
            emit(0xE3);
        } else {
            return false;
        }
        return true;
    }
    if (m == "JP") {
        if (n == 1 && (o[0].kind == K::IndHl || (o[0].kind == K::IndIndex && o[0].expr.empty()))) {
            if (o[0].prefix)
                emit(o[0].prefix);
            emit(0xE9);
            return true;
        }
        const int cc = n == 2 ? condition(o[0]) : -1;
        if ((n != 1 && cc < 0) || o.back().kind != K::Immediate)
            return false;
        emit(static_cast<uint8_t>(n == 1 ? 0xC3 : 0xC2 | cc << 3));
        emitWord(wordOf(o.back().expr));
        return true;
    }
    if (m == "JR") {
        const int cc = n == 2 ? condition(o[0]) : -1;
        if (n < 1 || n > 2 || (n == 2 && (cc < 0 || cc > 3)) || o.back().kind != K::Immediate)
            return false;
        emit(static_cast<uint8_t>(n == 1 ? 0x18 : 0x20 | cc << 3));
        emit(relative(o.back()));
        return true;
    }
    if (m == "DJNZ") {
        if (n != 1 || o[0].kind != K::Immediate)
            return false;
        emit(0x10);
        emit(relative(o[0]));
        return true;
    }
    if (m == "CALL") {
        const int cc = n == 2 ? condition(o[0]) : -1;
        if (n < 1 || n > 2 || (n == 2 && cc < 0) || o.back().kind != K::Immediate)
            return false;
        emit(static_cast<uint8_t>(n == 1 ? 0xCD : 0xC4 | cc << 3));
        emitWord(wordOf(o.back().expr));
        return true;
    }
    if (m == "RET") {
        const int cc = n == 1 ? condition(o[0]) : -1;
        if (n > 1 || (n == 1 && cc < 0))
            return false;
        emit(static_cast<uint8_t>(n == 0 ? 0xC9 : 0xC0 | cc << 3));
        return true;
    }
    if (m == "RST") {
        // 0 to 7 count the restarts; otherwise the address itself. A
        // second value is a word that follows the instruction.
        if (n < 1 || n > 2)
            return false;
        const Value value = evaluate(o[0].expr);
        int32_t p = value.number;
        if (p >= 0 && p < 8)
            p *= 8;
        if (value.known && (p < 0 || p > 0x38 || p % 8))
            error("Value Out of Range");
        emit(static_cast<uint8_t>(0xC7 | (p & 0x38)));
        if (n == 2)
            emitWord(wordOf(o[1].expr));
        return true;
    }
    if (m == "IM") {
        if (n != 1)
            return false;
        const Value mode = evaluate(o[0].expr);
        if (mode.known && (mode.number < 0 || mode.number > 2))
            error("Value Out of Range");
        static const uint8_t kModes[3] = {0x46, 0x56, 0x5E};
        emit(0xED);
        emit(kModes[mode.number >= 0 && mode.number <= 2 ? mode.number : 0]);
        return true;
    }
    if (m == "IN") {
        if (n == 2 && o[0].kind == K::Reg8 && o[0].code == 7 && o[1].kind == K::IndAddress) {
            emit(0xDB);
            emit(byteOf(o[1].expr));
            return true;
        }
        // IN (C), also written IN F,(C), only sets the flags.
        const bool flags = (n == 1 && o[0].kind == K::IndC) || (n == 2 && o[0].text == "F" && o[1].kind == K::IndC);
        if (!flags && !(n == 2 && o[0].kind == K::Reg8 && o[1].kind == K::IndC))
            return false;
        emit(0xED);
        emit(static_cast<uint8_t>(0x40 | (flags ? 6 : o[0].code) << 3));
        return true;
    }
    if (m == "OUT") {
        if (n != 2)
            return false;
        if (o[0].kind == K::IndAddress && o[1].kind == K::Reg8 && o[1].code == 7) {
            emit(0xD3);
            emit(byteOf(o[0].expr));
            return true;
        }
        if (o[0].kind != K::IndC)
            return false;
        int code = o[1].code;
        if (o[1].kind == K::Immediate) {
            const Value zero = evaluate(o[1].expr);  // OUT (C),0
            if (zero.known && zero.number != 0)
                return false;
            code = 6;
        } else if (o[1].kind != K::Reg8) {
            return false;
        }
        emit(0xED);
        emit(static_cast<uint8_t>(0x41 | code << 3));
        return true;
    }
    return false;
}

}  // namespace

bool isAssemblerWord(const std::string& word)
{
    const std::string name = upper(word);
    return mnemonics().count(name) != 0 || directives().count(name) != 0;
}

AsmResult assemble(const std::string& source, const std::string& fileName, const AsmHost& host)
{
    return Assembler(host).run(source, fileName);
}

}  // namespace tuxape
