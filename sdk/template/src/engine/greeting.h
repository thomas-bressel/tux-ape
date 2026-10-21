#pragma once

namespace hello {

// What the CPC's screen is, from its mode and where it starts in memory, as
// PluginHost::screen() gives them. Data, not a sentence: the window words
// it, in the user's language. An engine has no texts of its own.
struct ScreenInfo {
    int mode = 0;
    int colours = 0;       // 16, 4 or 2; 0 for a mode the CPC has not
    unsigned address = 0;  // within the 64K the screen is read from
};

ScreenInfo screenInfo(int mode, int address);

}  // namespace hello
