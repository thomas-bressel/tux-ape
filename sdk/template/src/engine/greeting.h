#pragma once

#include <string>

namespace hello {

// What the window says of the CPC's screen: its mode and where it starts in
// memory, as PluginHost::screen() gives them.
std::string describeScreen(int mode, int address);

}  // namespace hello
