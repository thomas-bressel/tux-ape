// The plugin's engine alone: what it says of a screen.

#include "check.h"
#include "greeting.h"

int main()
{
    CHECK(hello::describeScreen(1, 0xC000) == "Mode 1 (4 colours), from &C000");
    CHECK(hello::describeScreen(0, 0x4000) == "Mode 0 (16 colours), from &4000");
    CHECK(hello::describeScreen(2, 0x18000) == "Mode 2 (2 colours), from &8000");
    return checkSummary("engine_greeting");
}
