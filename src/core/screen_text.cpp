#include "core/screen_text.h"

#include <cstring>

#include "core/cpc.h"

namespace tuxape {

namespace {

// Reads the firmware ROM whatever is currently mapped in.
struct LowerRomReader {
    Memory& memory;
    bool lower, upper;

    explicit LowerRomReader(Memory& m)
        : memory(m)
        , lower(m.lowerRomEnabled())
        , upper(m.upperRomEnabled())
    {
        memory.setRomEnables(true, upper);
    }
    ~LowerRomReader() { memory.setRomEnables(lower, upper); }
};

}  // namespace

std::string readScreenText(Cpc& cpc)
{
    const Crtc& crtc = cpc.crtc();
    const uint8_t* ram = cpc.memory().baseRam();
    const int mode = cpc.gateArray().mode();
    const int bytesPerChar = mode == 0 ? 4 : mode == 1 ? 2 : 1;
    const int columns = crtc.reg(1) * 2 / bytesPerChar;
    const int rows = crtc.reg(6);
    const unsigned start = static_cast<unsigned>(crtc.reg(12) << 8 | crtc.reg(13));

    // The character matrices sit at the top of the firmware ROM, 8 bytes each.
    uint8_t font[256][8];
    {
        LowerRomReader rom(cpc.memory());
        for (int c = 0; c < 256; ++c)
            for (int line = 0; line < 8; ++line)
                font[c][line] = cpc.memory().read(static_cast<uint16_t>(0x3800 + c * 8 + line));
    }

    std::string text;
    for (int row = 0; row < rows; ++row) {
        std::string lineText;
        for (int col = 0; col < columns; ++col) {
            // Rebuild the 8x8 shape: a pixel counts as set when it is not pen 0.
            uint8_t shape[8];
            for (int line = 0; line < 8; ++line) {
                uint8_t bits = 0;
                for (int n = 0; n < bytesPerChar; ++n) {
                    const unsigned byteIndex = static_cast<unsigned>((row * crtc.reg(1) * 2) + col * bytesPerChar + n);
                    const unsigned ma = start + byteIndex / 2;
                    const unsigned addr = ((ma & 0x3000) << 2 | static_cast<unsigned>(line) << 11
                                           | (ma & 0x3FF) << 1 | (byteIndex & 1));
                    const uint8_t b = ram[addr & 0xFFFF];
                    switch (mode) {
                    case 0:
                        bits = static_cast<uint8_t>(bits << 2 | ((b & 0xAA) ? 2 : 0) | ((b & 0x55) ? 1 : 0));
                        break;
                    case 1:
                        for (int p = 0; p < 4; ++p)
                            bits = static_cast<uint8_t>(bits << 1 | ((b & (0x88 >> p)) ? 1 : 0));
                        break;
                    default:
                        bits = b;
                        break;
                    }
                }
                shape[line] = bits;
            }
            char found = '?';
            for (int c = 32; c < 127; ++c) {
                if (std::memcmp(shape, font[c], 8) == 0) {
                    found = static_cast<char>(c);
                    break;
                }
            }
            lineText += found;
        }
        while (!lineText.empty() && lineText.back() == ' ')
            lineText.pop_back();
        text += lineText;
        text += '\n';
    }
    return text;
}

}  // namespace tuxape
