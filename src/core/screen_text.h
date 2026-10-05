#pragma once

#include <string>

namespace tuxape {

class Cpc;

// Reads the text on the CPC's screen by matching the pixels in video memory
// against the character shapes in the firmware ROM. Works for text printed
// with the standard character set in any mode; cells that match nothing
// come out as '?'. Rows are separated by '\n', trailing spaces removed.
std::string readScreenText(Cpc& cpc);

}  // namespace tuxape
