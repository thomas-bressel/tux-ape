#include "core/symbols.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace tuxape {

namespace {

std::string upper(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

bool isDigits(const std::string& text, int base)
{
    if (text.empty())
        return false;
    for (const char c : text) {
        const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                          : std::isxdigit(static_cast<unsigned char>(c)) ? std::toupper(static_cast<unsigned char>(c)) - 'A' + 10
                                                                         : 99;
        if (digit >= base)
            return false;
    }
    return true;
}

// A value as symbol files write them; `hex` when a plain number is
// hexadecimal, as NoICE's are.
std::optional<int32_t> number(std::string text, bool hex)
{
    int base = hex ? 16 : 10;
    if (text.size() > 1 && (text[0] == '#' || text[0] == '&' || text[0] == '$')) {
        text.erase(0, 1);
        base = 16;
    } else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.erase(0, 2);
        base = 16;
    } else if (text.size() > 1 && text[0] == '%') {
        text.erase(0, 1);
        base = 2;
    } else if (text.size() > 1 && (text.back() == 'h' || text.back() == 'H')
               && std::isdigit(static_cast<unsigned char>(text[0]))) {
        text.pop_back();
        base = 16;
    }
    if (!isDigits(text, base) || text.size() > 10)
        return std::nullopt;
    const long long value = std::stoll(text, nullptr, base);
    if (value > 0xFFFFFFFFLL)
        return std::nullopt;
    return static_cast<int32_t>(static_cast<uint32_t>(value));
}

// What a name may be: a letter, '_', '.' or '@' first, then no space.
bool isName(const std::string& text)
{
    if (text.empty())
        return false;
    const unsigned char first = static_cast<unsigned char>(text[0]);
    return std::isalpha(first) || first == '_' || first == '.' || first == '@';
}

std::vector<std::string> words(const std::string& line)
{
    std::vector<std::string> out;
    std::string word;
    for (const char c : line) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '=' || c == ',') {
            if (!word.empty())
                out.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty())
        out.push_back(word);
    return out;
}

}  // namespace

std::optional<std::vector<AsmSymbol>> readSymbolFile(const std::string& text, const std::string& fileName)
{
    std::vector<AsmSymbol> symbols;
    std::istringstream lines(text);
    std::string line;
    for (int number_ = 1; std::getline(lines, line); ++number_) {
        if (const size_t comment = line.find(';'); comment != std::string::npos)
            line.resize(comment);
        std::vector<std::string> parts = words(line);
        bool hex = false;
        // NoICE: DEF name value, the value in hexadecimal.
        if (parts.size() >= 3 && upper(parts[0]) == "DEF") {
            parts.erase(parts.begin());
            hex = true;
        }
        if (parts.size() < 2)
            continue;
        std::string name = parts[0];
        if (name.size() > 1 && name.back() == ':')
            name.pop_back();
        // name EQU value, or name value.
        const size_t at = parts.size() >= 3 && upper(parts[1]) == "EQU" ? 2 : 1;
        if (!isName(name) || upper(name) == "EQU" || at >= parts.size())
            continue;
        const std::optional<int32_t> value = number(parts[at], hex);
        if (!value)
            continue;
        AsmSymbol symbol;
        symbol.name = name;
        symbol.value = *value;
        symbol.file = fileName;
        symbol.line = number_;
        symbols.push_back(std::move(symbol));
    }
    if (symbols.empty())
        return std::nullopt;
    return symbols;
}

std::string symbolFileText(const std::vector<AsmSymbol>& symbols)
{
    std::string text;
    for (const AsmSymbol& symbol : symbols) {
        char value[8];
        std::snprintf(value, sizeof value, "%04X", static_cast<unsigned>(symbol.value & 0xFFFF));
        text += symbol.name + " equ #" + value + "\n";
    }
    return text;
}

void SymbolNames::set(const std::vector<AsmSymbol>& symbols)
{
    byValue_.clear();
    byName_.clear();
    for (const AsmSymbol& symbol : symbols) {
        if (symbol.value < 0 || symbol.value > 0xFFFF)
            continue;
        const auto value = static_cast<uint16_t>(symbol.value);
        byValue_[value].push_back(symbol.name);
        byName_.emplace(upper(symbol.name), value);
    }
}

const std::vector<std::string>& SymbolNames::at(uint16_t address) const
{
    static const std::vector<std::string> none;
    const auto found = byValue_.find(address);
    return found == byValue_.end() ? none : found->second;
}

std::string SymbolNames::named(const std::string& instruction) const
{
    if (byValue_.empty())
        return instruction;
    std::string out;
    for (size_t i = 0; i < instruction.size();) {
        // '#' and exactly four hexadecimal digits: an address.
        size_t digits = 0;
        if (instruction[i] == '#')
            while (i + 1 + digits < instruction.size()
                   && std::isxdigit(static_cast<unsigned char>(instruction[i + 1 + digits])))
                ++digits;
        if (digits == 4) {
            const auto value = static_cast<uint16_t>(std::stoul(instruction.substr(i + 1, 4), nullptr, 16));
            const auto found = byValue_.find(value);
            if (found != byValue_.end()) {
                out += found->second.front();
                i += 5;
                continue;
            }
        }
        out += instruction[i++];
    }
    return out;
}

std::optional<uint16_t> SymbolNames::value(const std::string& name) const
{
    const auto found = byName_.find(upper(name));
    return found == byName_.end() ? std::nullopt : std::optional<uint16_t>(found->second);
}

}  // namespace tuxape
