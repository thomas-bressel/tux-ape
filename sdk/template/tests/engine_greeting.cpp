// The plugin's engine alone: what it makes of a screen.

#include "check.h"
#include "greeting.h"

int main()
{
    const hello::ScreenInfo one = hello::screenInfo(1, 0xC000);
    CHECK(one.mode == 1 && one.colours == 4 && one.address == 0xC000);
    CHECK_EQ(hello::screenInfo(0, 0x4000).colours, 16);
    CHECK_EQ(hello::screenInfo(2, 0x18000).address, 0x8000u);
    // A mode the CPC has not: no colours to say.
    CHECK_EQ(hello::screenInfo(3, 0).colours, 0);
    return checkSummary("engine_greeting");
}
