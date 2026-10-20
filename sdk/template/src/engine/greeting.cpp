#include "greeting.h"

#include <cstdio>

namespace hello {

std::string describeScreen(int mode, int address)
{
    static const char* const kColours[] = {"16 colours", "4 colours", "2 colours"};
    char text[96];
    std::snprintf(text, sizeof text, "Mode %d (%s), from &%04X", mode, mode >= 0 && mode <= 2 ? kColours[mode] : "?",
                  static_cast<unsigned>(address & 0xFFFF));
    return text;
}

}  // namespace hello
