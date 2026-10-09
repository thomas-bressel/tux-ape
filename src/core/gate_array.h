#pragma once

#include <cstdint>

namespace tuxape {

class Asic;
class Crtc;
class Monitor;

enum class MonitorKind : uint8_t { Colour, Green, Greyscale };

// The Amstrad Gate Array: palette, screen mode, ROM enables, the raster
// interrupt, the sync signals sent to the monitor, and the conversion of
// video memory into pixels.
//
// Timing details (when an ink or mode change shows, how the monitor's sync
// is derived from the CRTC's, when interrupts are raised) follow the
// "Amstrad CPC CRTC Compendium" by Longshot / Logon System (CC BY-NC-ND),
// chapters 9, 14.3, 16.2 and 27.
class GateArray {
public:
    GateArray();
    void reset();

    // Writes to port &7Fxx whose top two bits are 00, 01 or 10. The fourth
    // function, RAM banking, belongs to the memory manager.
    void write(uint8_t value);

    // A microsecond of video is handled in two steps. At its start, once
    // the CRTC has moved on, sync() follows the CRTC's sync signals: monitor
    // sync, screen mode, interrupts. At its end render() draws 16 pixels,
    // with whatever the CPU changed meanwhile, and fetches the next
    // character from video memory.
    void sync(const Crtc& crtc, Monitor& monitor);
    void render(const Crtc& crtc, const uint8_t* videoRam, Monitor& monitor);

    bool interruptRequested() const { return interrupt_; }
    // An R2 write has just started an HSYNC in the middle of a character.
    // `late` is for the OUT (C),r kind of write, after which the picture
    // goes blank a little further on.
    void hsyncStartedByWrite(const Crtc& crtc, bool late);
    // An R3 write has just ended the HSYNC in progress.
    void hsyncEndedByWrite(const Crtc& crtc);
    void acknowledgeInterrupt();
    // The Plus: a line has just been set for the raster interrupt where
    // there was none. The interrupt of the CPC kind still waiting to be
    // taken is dropped (see Asic::write).
    void rasterLineSet() { interrupt_ = false; }
    // A snapshot has put the ASIC's registers back: if the HSYNC of the
    // line it holds for the raster interrupt is on, that line has asked
    // for its interrupt already.
    void rasterLineRestored(const Crtc& crtc);

    bool lowerRomEnabled() const { return !(rmr_ & 0x04); }
    bool upperRomEnabled() const { return !(rmr_ & 0x08); }

    // Rebuilds the colours sent to the monitor. `linear` selects evenly
    // spaced levels, as on the Plus, instead of the CPC's brighter half tone.
    // `brightness` is the monitor's knob, from -100 to 100 with 0 in the
    // middle.
    void setMonitor(MonitorKind kind, bool linear, int brightness = 0);
    // What a hardware colour (0-31) looks like on such a monitor, 0xAARRGGBB.
    static uint32_t monitorColour(int hardwareColour, MonitorKind kind, bool linear, int brightness);

    // The Plus. Its ASIC keeps a palette of 12-bit colours (green, red and
    // blue from the top) which takes the place of the 27 colours: pens 0
    // to 15, the border (16), and colours 1 to 15 of the sprites (17-31).
    // Nothing of it applies to a machine that is not a Plus.
    void setPlus(bool plus);
    bool plus() const { return plus_; }
    // The ASIC, for its sprites, its scrolling and its raster interrupt.
    void attach(Asic* asic) { asic_ = asic; }
    void setPlusColour(int index, uint16_t grb);
    uint32_t spriteColour(int number) const { return spriteRgb_[number & 15]; }
    // The 12-bit colour the ASIC keeps for one of the CPC's.
    static uint16_t plusColour(int hardwareColour);
    static uint32_t monitorColour12(uint16_t grb, MonitorKind kind, int brightness);

    // State, named as in the WinAPE register window.
    uint8_t mode() const { return mode_; }                  // mode being displayed
    uint8_t requestedMode() const { return rmr_ & 3; }      // takes effect at the next HSYNC
    uint8_t selectedPen() const { return pen_; }            // 0-15, or 16 for the border
    uint8_t ink(int pen) const { return ink_[pen]; }        // hardware colour number, 0-31
    uint8_t interruptCounter() const { return r52_; }       // R52
    // How long after the end of an HSYNC the interrupt counter moves on,
    // in microseconds. One: a program is interrupted a microsecond after
    // an HSYNC has ended, not as it ends (Compendium 27.6.1, 27.6.2: with
    // R3 = 14 the code is interrupted 15 microseconds after C0 = R2 on
    // types 0, 1 and 2). The Shaker's tests that count from an interrupt
    // agree on a real CRTC 0 ("OUTI story", "R52 inc in HSYNC").
    // WinAPE's own model of the machine has none: a session recorded there
    // keeps in step better when played back the same way (see
    // core/winape_session.h).
    static constexpr int kInterruptDelay = 1;
    void setInterruptDelay(int microseconds) { interruptDelay_ = microseconds < 0 ? 0 : microseconds > 31 ? 31 : microseconds; }
    int interruptDelay() const { return interruptDelay_; }
    uint8_t romAndMode() const { return rmr_ & 0x0F; }      // as last written to the register

    // Puts back a state taken from a snapshot. `pen` is 0-15 or 16 for the
    // border, `inks` the 17 hardware colour numbers.
    void restore(uint8_t pen, const uint8_t* inks, uint8_t romAndMode, uint8_t interruptCounter, bool interrupt);
    uint32_t colour(int hardwareColour) const { return colours_[hardwareColour & 31]; }

private:
    uint8_t rmr_ = 0;       // ROM enables and requested mode
    uint8_t mode_ = 0;
    uint8_t pen_ = 0;
    uint8_t ink_[17] = {};
    uint32_t rgb_[17] = {};  // ink_ translated through colours_
    uint32_t colours_[32] = {};
    MonitorKind monitorKind_ = MonitorKind::Colour;
    int brightness_ = 0;
    bool plus_ = false;
    uint16_t plus12_[32] = {};
    uint32_t spriteRgb_[16] = {};
    Asic* asic_ = nullptr;
    uint8_t pensBefore_[16] = {};  // the character drawn last, for soft scrolling
    bool fetchedFirst_ = false;    // the character fetched is the line's first
    int fetchedX_ = 0;             // where it is for the sprites
    int fetchedY_ = 0;

    // Colours as they were before an ink change made during the current
    // microsecond; the change only shows part-way through the character.
    uint32_t rgbBefore_[17] = {};
    bool inkChanged_ = false;

    // The character fetched from video memory is displayed one microsecond
    // later.
    uint8_t fetched_[2] = {};
    bool fetchedDisplay_[2] = {};  // display enable for each of the two bytes

    uint8_t r52_ = 0;
    uint8_t hsyncAge_ = 0;      // microseconds since HSYNC rose
    uint8_t vsyncLines_ = 0;    // HSYNCs counted since VSYNC rose
    bool vsyncSequence_ = false;  // the Gate Array is timing a vertical sync
    bool vsyncBlack_ = false;
    uint8_t blackLines_ = 0;    // HSYNCs since VSYNC rose, as the blanking counts them
    // With a delay set: what the interrupt counter has still to hear of,
    // bit n for what is due in n + 1 microseconds.
    int interruptDelay_ = kInterruptDelay;
    uint32_t countsDue_ = 0;
    uint32_t vsyncDue_ = 0;
    bool interrupt_ = false;
    bool prevHsync_ = false;
    bool rasterMatch_ = false;  // the Plus: HSYNC is on, on the line set for the raster interrupt
    bool rasterDue_ = false;    // ... and has just come on: the interrupt follows
    bool soundRound_ = false;   // the Plus: the sound channels' round for this line is under way
    bool prevVsync_ = false;
    bool delayedHsync_ = false;  // the CRTC's HSYNC one microsecond ago
    bool lastPixelBlack_ = false;  // the character drawn last ended blanked
    bool hsync_ = false;         // HSYNC as the Gate Array sees it now
    bool blankedBefore_ = false;  // the previous character was blanked to its end
    bool lateBlanking_ = false;   // this HSYNC was started by an R2 write that came late

    void drawPlus(uint32_t* out, const uint8_t* left, const uint8_t* right, int split);
    void countHsync(Monitor& monitor);
    void rasterInterrupt(const Crtc& crtc, bool hsync);
};

}  // namespace tuxape
