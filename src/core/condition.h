#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tuxape {

class Cpc;

// What a breakpoint's condition may name besides the machine itself.
struct ConditionContext {
    uint32_t address = 0;   // the address or port read or written
    uint32_t value = 0;     // the value read or written
    uint32_t previous = 0;  // what a write to memory replaced
    // An assembler symbol's value, whatever its case, if there is one.
    std::function<std::optional<int32_t>(const std::string& name)> symbol;
    // A function the machine itself does not have (its name in upper
    // case): the emulator's timers, say.
    std::function<std::optional<int32_t>(const std::string& name, const std::vector<int32_t>& args)> function;
};

// A breakpoint's condition, worked out as WinAPE's are: the Z80's registers
// by name (the halves of IX and IY too), numbers in decimal, in hexadecimal
// after '#' or '&' and in binary after '%', brackets, the operators
// + - * / mod and or xor not (all on bits: "true" is #FFFFFFFF) and the
// comparisons = <> < <= > >=, which come before `and`, which comes before
// `or` and `xor`. Variables: address, value, previous, true, false, mode,
// psg_select (or ay_select), crtc_select, palette_select, lower_enabled,
// upper_enabled, upper_rom, ram_bank, cartridge_bank, secondary_rom,
// ppi_a, ppi_c, ppi_control, fdc_motor, tape_motor. Functions: peek(addr),
// poke(addr,n...), byte(v), hibyte(v), word(v), hiword(v), crtc([r]),
// psg([r]) (or ay), ga_palette([pen]), and those the context adds.
//
// Nothing when the text is not such an expression.
std::optional<int32_t> evaluateCondition(const std::string& text, Cpc& cpc, const ConditionContext& context = {});

}  // namespace tuxape
