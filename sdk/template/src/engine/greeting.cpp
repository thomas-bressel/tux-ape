#include "greeting.h"

namespace hello {

ScreenInfo screenInfo(int mode, int address)
{
    constexpr int kColours[] = {16, 4, 2};
    ScreenInfo info;
    info.mode = mode;
    info.colours = mode >= 0 && mode <= 2 ? kColours[mode] : 0;
    info.address = static_cast<unsigned>(address) & 0xFFFF;
    return info;
}

}  // namespace hello
