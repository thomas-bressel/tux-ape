#include "core/condition.h"

#include <cctype>
#include <vector>

#include "core/cpc.h"

namespace tuxape {

namespace {

class Evaluator {
public:
    Evaluator(const std::string& text, Cpc& cpc, const ConditionContext& context)
        : s_(text)
        , cpc_(cpc)
        , context_(context)
    {
    }

    std::optional<int32_t> run()
    {
        const int32_t value = either();
        skip();
        if (bad_ || i_ < s_.size())
            return std::nullopt;
        return value;
    }

private:
    static constexpr int32_t kTrue = -1;

    void skip()
    {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])))
            ++i_;
    }
    bool symbol(const char* text)
    {
        skip();
        const size_t length = std::char_traits<char>::length(text);
        if (s_.compare(i_, length, text) != 0)
            return false;
        i_ += length;
        return true;
    }
    // The word at the cursor, in upper case, without moving past it.
    std::string peekWord()
    {
        skip();
        std::string word;
        for (size_t j = i_; j < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[j])) || s_[j] == '_'); ++j)
            word += static_cast<char>(std::toupper(static_cast<unsigned char>(s_[j])));
        return word;
    }
    bool word(const char* text)
    {
        if (peekWord() != text)
            return false;
        i_ += std::char_traits<char>::length(text);
        return true;
    }

    // or, xor
    int32_t either()
    {
        int32_t left = both();
        for (;;) {
            if (word("OR") || symbol("|"))
                left |= both();
            else if (word("XOR") || symbol("^"))
                left ^= both();
            else
                return left;
        }
    }
    // and
    int32_t both()
    {
        int32_t left = comparison();
        while (word("AND"))
            left &= comparison();
        return left;
    }
    int32_t comparison()
    {
        int32_t left = sum();
        for (;;) {
            if (symbol("<>") || symbol("!="))
                left = left != sum() ? kTrue : 0;
            else if (symbol("<="))
                left = left <= sum() ? kTrue : 0;
            else if (symbol(">="))
                left = left >= sum() ? kTrue : 0;
            else if (symbol("==") || symbol("="))
                left = left == sum() ? kTrue : 0;
            else if (symbol("<"))
                left = left < sum() ? kTrue : 0;
            else if (symbol(">"))
                left = left > sum() ? kTrue : 0;
            else
                return left;
        }
    }
    int32_t sum()
    {
        uint32_t left = static_cast<uint32_t>(product());
        for (;;) {
            if (symbol("+"))
                left += static_cast<uint32_t>(product());
            else if (symbol("-"))
                left -= static_cast<uint32_t>(product());
            else
                return static_cast<int32_t>(left);
        }
    }
    int32_t product()
    {
        int32_t left = unary();
        for (;;) {
            const bool divide = symbol("/");
            const bool modulo = !divide && word("MOD");
            if (divide || modulo) {
                const int32_t right = unary();
                if (right == 0)
                    bad_ = true;
                else if (right == -1)
                    left = divide ? static_cast<int32_t>(0u - static_cast<uint32_t>(left)) : 0;
                else
                    left = divide ? left / right : left % right;
            } else if (symbol("*")) {
                left = static_cast<int32_t>(static_cast<uint32_t>(left) * static_cast<uint32_t>(unary()));
            } else {
                return left;
            }
        }
    }
    int32_t unary()
    {
        if (symbol("-"))
            return static_cast<int32_t>(0u - static_cast<uint32_t>(unary()));
        if (symbol("+"))
            return unary();
        if (word("NOT") || symbol("~"))
            return ~unary();
        return primary();
    }

    int32_t number(int base)
    {
        uint32_t value = 0;
        bool any = false;
        for (; i_ < s_.size() && std::isalnum(static_cast<unsigned char>(s_[i_])); ++i_) {
            const int c = std::toupper(static_cast<unsigned char>(s_[i_]));
            const int digit = c <= '9' ? c - '0' : c - 'A' + 10;
            if (digit >= base)
                bad_ = true;
            value = value * static_cast<uint32_t>(base) + static_cast<uint32_t>(digit);
            any = true;
        }
        if (!any)
            bad_ = true;
        return static_cast<int32_t>(value);
    }

    int32_t primary()
    {
        skip();
        if (i_ >= s_.size()) {
            bad_ = true;
            return 0;
        }
        const char c = s_[i_];
        if (c == '(') {
            ++i_;
            const int32_t value = either();
            if (!symbol(")"))
                bad_ = true;
            return value;
        }
        if (c == '#' || c == '&' || c == '%') {
            ++i_;
            return number(c == '%' ? 2 : 16);
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
            return number(10);
        std::string name = peekWord();
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) {
            bad_ = true;
            return 0;
        }
        i_ += name.size();
        if (i_ < s_.size() && s_[i_] == '\'')  // AF' and the other shadows
            name += s_[i_++];
        skip();
        if (i_ < s_.size() && s_[i_] == '(')
            return function(name);
        return named(name);
    }

    int32_t named(const std::string& name)
    {
        auto& z80 = cpc_.cpu();
        const auto pair = [&](int high, int low) { return z80.reg[high] << 8 | z80.reg[low]; };
        static const char* const kRegisters[] = {"B", "C", "D", "E", "H", "L", "F", "A"};  // as Z80::Reg has them
        for (int r = 0; r < 8; ++r)
            if (name == kRegisters[r])
                return z80.reg[r];
        if (name == "AF") return pair(z80.A, z80.F);
        if (name == "BC") return pair(z80.B, z80.C);
        if (name == "DE") return pair(z80.D, z80.E);
        if (name == "HL") return pair(z80.H, z80.L);
        if (name == "AF'") return z80.af2;
        if (name == "BC'") return z80.bc2;
        if (name == "DE'") return z80.de2;
        if (name == "HL'") return z80.hl2;
        if (name == "IX") return z80.ix;
        if (name == "IY") return z80.iy;
        if (name == "SP") return z80.sp;
        if (name == "PC") return z80.pc;
        if (name == "I") return z80.i;
        if (name == "R") return z80.r;
        if (name == "IXH" || name == "HX" || name == "XH") return z80.ix >> 8;
        if (name == "IXL" || name == "LX" || name == "XL") return z80.ix & 0xFF;
        if (name == "IYH" || name == "HY" || name == "YH") return z80.iy >> 8;
        if (name == "IYL" || name == "LY" || name == "YL") return z80.iy & 0xFF;

        if (name == "ADDRESS") return static_cast<int32_t>(context_.address);
        if (name == "VALUE") return static_cast<int32_t>(context_.value);
        if (name == "PREVIOUS") return static_cast<int32_t>(context_.previous);
        if (name == "TRUE") return kTrue;
        if (name == "FALSE") return 0;
        if (name == "MODE") return cpc_.gateArray().requestedMode();
        if (name == "PSG_SELECT" || name == "AY_SELECT") return cpc_.psg().selected();
        if (name == "CRTC_SELECT") return cpc_.crtc().selected();
        if (name == "PALETTE_SELECT") return cpc_.gateArray().selectedPen();
        if (name == "LOWER_ENABLED") return cpc_.memory().lowerRomEnabled() ? kTrue : 0;
        if (name == "UPPER_ENABLED") return cpc_.memory().upperRomEnabled() ? kTrue : 0;
        if (name == "UPPER_ROM") return cpc_.memory().selectedUpperRom();
        if (name == "RAM_BANK") return cpc_.memory().ramBank();
        if (name == "CARTRIDGE_BANK") return cpc_.asic().rmr2() & 7;
        if (name == "SECONDARY_ROM") return cpc_.asic().rmr2();
        if (name == "PPI_A") return cpc_.ppi().outputA();
        if (name == "PPI_C") return cpc_.ppi().outputC();
        if (name == "PPI_CONTROL") return cpc_.ppi().control();
        if (name == "FDC_MOTOR") return cpc_.fdc().motor() ? kTrue : 0;
        if (name == "TAPE_MOTOR") return cpc_.tape().motor() ? kTrue : 0;
        if (context_.symbol) {
            if (const std::optional<int32_t> value = context_.symbol(name))
                return *value;
        }
        bad_ = true;
        return 0;
    }

    int32_t function(const std::string& name)
    {
        ++i_;  // the bracket
        std::vector<int32_t> args;
        if (!symbol(")")) {
            do
                args.push_back(either());
            while (symbol(","));
            if (!symbol(")"))
                bad_ = true;
        }
        const size_t n = args.size();
        if (name == "PEEK" && n == 1) return cpc_.memory().read(static_cast<uint16_t>(args[0]));
        if (name == "POKE" && n >= 1) {
            for (size_t k = 1; k < n; ++k)
                cpc_.memory().write(static_cast<uint16_t>(args[0] + static_cast<int32_t>(k) - 1), static_cast<uint8_t>(args[k]));
            return 0;
        }
        if (name == "BYTE" && n == 1) return args[0] & 0xFF;
        if (name == "HIBYTE" && n == 1) return args[0] >> 8 & 0xFF;
        if (name == "WORD" && n == 1) return args[0] & 0xFFFF;
        if (name == "HIWORD" && n == 1) return args[0] >> 16 & 0xFFFF;
        if (name == "CRTC" && n <= 1) return cpc_.crtc().reg(n ? args[0] : cpc_.crtc().selected());
        if ((name == "PSG" || name == "AY") && n <= 1) return cpc_.psg().reg(n ? args[0] : cpc_.psg().selected());
        if (name == "GA_PALETTE" && n <= 1) {
            const int32_t pen = n ? args[0] : cpc_.gateArray().selectedPen();
            if (pen >= 0 && pen <= 16)
                return cpc_.gateArray().ink(pen);
        }
        if (context_.function) {
            if (const std::optional<int32_t> value = context_.function(name, args))
                return *value;
        }
        bad_ = true;
        return 0;
    }

    const std::string& s_;
    Cpc& cpc_;
    const ConditionContext& context_;
    size_t i_ = 0;
    bool bad_ = false;
};

}  // namespace

std::optional<int32_t> evaluateCondition(const std::string& text, Cpc& cpc, const ConditionContext& context)
{
    return Evaluator(text, cpc, context).run();
}

}  // namespace tuxape
