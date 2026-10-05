#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace tuxape {

class Crtc;
class GateArray;

// The ASIC of the Plus machines (464 Plus, 6128 Plus, GX4000), which holds
// their Gate Array and CRTC and adds to them: a palette of 4096 colours,
// sixteen hardware sprites, a screen that can be split and scrolled by the
// pixel, an interrupt on any line, and three sound channels fed from
// memory. A program has all that once it has sent the ASIC its unlocking
// sequence, and reaches it through a page of registers it asks to see at
// &4000-&7FFF.
class Asic {
public:
    Asic();

    // Where the palette's colours and the split screen go.
    void attach(GateArray* gateArray, Crtc* crtc)
    {
        gateArray_ = gateArray;
        crtc_ = crtc;
    }

    void reset();

    // The lock. Every byte written to the CRTC's register select port
    // comes through here.
    void sequence(uint8_t value);
    bool unlocked() const { return unlocked_; }

    // RMR2, which the Gate Array's port takes once the ASIC is unlocked:
    // bits 2-0, the cartridge page that is the lower ROM; bits 4-3, where
    // it shows (&0000, &4000, &8000) or, 11, the page of registers shown at
    // &4000 with the lower ROM at &0000.
    void setRmr2(uint8_t value) { rmr2_ = value & 0x1F; }
    uint8_t rmr2() const { return rmr2_; }

    // The page of registers. What a program reads there is kept as a
    // picture of the page; what it writes goes through write().
    static constexpr int kPageSize = 0x4000;
    const uint8_t* page() const { return page_.data(); }
    void write(uint16_t address, uint8_t value);

    // A colour set the way a CPC does, through the Gate Array: the ASIC
    // puts its own 12-bit colour for it in the palette.
    void setHardwareColour(int pen, int hardwareColour);
    // The palette: pens 0-15, the border (16), sprite colours 1-15 (17-31).
    // Twelve bits: green, red, blue from the top.
    uint16_t colour(int index) const { return palette_[index & 31]; }

    // ---- Sprites ----
    struct Sprite {
        int16_t x = 0;       // in mode 2 pixels from the left of the screen
        int16_t y = 0;       // in lines from its top
        uint8_t magX = 0;    // 0: not shown; 1, 2, 4: its pixels' width
        uint8_t magY = 0;
    };
    const Sprite& sprite(int n) const { return sprites_[n & 15]; }
    // One of the 16 x 16 pixels of a sprite: 0 lets the screen show.
    uint8_t spritePixel(int n, int x, int y) const { return page_[static_cast<size_t>((n & 15) << 8 | (y & 15) << 4 | (x & 15))]; }
    // False while no sprite is shown: the picture needs no second look.
    bool spritesShown() const { return spritesShown_; }

    // ---- The raster ----
    uint8_t rasterInterruptLine() const { return pri_; }  // 0: the CPC's own interrupts
    uint8_t splitLine() const { return splt_; }           // 0: no split
    uint16_t splitAddress() const { return ssa_; }
    uint8_t scroll() const { return sscr_; }              // bit 7 border, bits 6-4 lines, bits 3-0 pixels
    uint8_t interruptVector() const { return ivr_; }

    // ---- Sound fed from memory ("DMA") ----
    // Three channels, each of which, while its bit of the status register
    // is set, takes one instruction a line from the list its address
    // register points at: a value for a register of the sound chip, a
    // pause, a loop, an interrupt or a stop. `readRam` and `writeSound`
    // are the machine's memory and sound chip.
    void attachSound(std::function<uint8_t(uint16_t)> readRam, std::function<void(int, uint8_t)> writeSound)
    {
        readRam_ = std::move(readRam);
        writeSound_ = std::move(writeSound);
    }
    bool soundChannelsOn() const { return (dcsr_ & 0x07) != 0; }
    // Called as each line's HSYNC starts.
    void soundTick();

    // ---- Interrupts ----
    // The raster's interrupt, raised by the Gate Array on the line asked
    // for, and those of the three sound channels. The status register
    // shows them (bit 7, and bits 6 to 4 for channels 0 to 2).
    void raiseRasterInterrupt();
    void raiseChannelInterrupt(int channel);
    // A sound channel is asking for an interrupt.
    bool interruptPending() const { return (dcsr_ & 0x70) != 0; }
    // The Z80 has taken an interrupt, the raster's if `raster`: the byte it
    // reads in mode 2 (the vector register, with which interrupt it is in
    // bits 2-1), and what the taking clears.
    uint8_t acknowledgeInterrupt(bool raster);

private:
    GateArray* gateArray_ = nullptr;
    Crtc* crtc_ = nullptr;

    bool unlocked_ = false;
    int sequenceAt_ = 0;
    uint8_t previous_ = 0;
    uint8_t rmr2_ = 0;

    std::array<uint8_t, kPageSize> page_ = {};
    std::array<uint16_t, 32> palette_ = {};
    std::array<Sprite, 16> sprites_ = {};
    bool spritesShown_ = false;
    uint8_t pri_ = 0, splt_ = 0, sscr_ = 0, ivr_ = 0;
    uint16_t ssa_ = 0;
    uint8_t dcsr_ = 0;

    struct Channel {
        uint16_t address = 0;   // of the next instruction
        uint8_t prescaler = 0;  // lines to a pause's unit, less one
        uint16_t loopStart = 0;
        uint16_t repeats = 0;
        uint16_t pause = 0;     // units still to wait
        uint8_t pauseLines = 0; // lines left of the unit under way
    };
    std::array<Channel, 3> channels_ = {};
    std::function<uint8_t(uint16_t)> readRam_;
    std::function<void(int, uint8_t)> writeSound_;

    void setColour(int index, uint16_t grb);
    void showDcsr();
};

}  // namespace tuxape
