#pragma once

#include <cstdint>

namespace tuxape {

class Crtc;
class Monitor;

enum class MonitorKind : uint8_t { Colour, Green, Greyscale };

// The Amstrad Gate Array: palette, screen mode, ROM enables, the raster
// interrupt, and the conversion of video memory into pixels.
class GateArray {
public:
    GateArray();
    void reset();

    // Writes to port &7Fxx whose top two bits are 00, 01 or 10. The fourth
    // function, RAM banking, belongs to the memory manager.
    void write(uint8_t value);

    // One microsecond of video: follows the CRTC, raises interrupts, and
    // draws 16 pixels on the monitor.
    void tick(const Crtc& crtc, const uint8_t* videoRam, Monitor& monitor);

    bool interruptRequested() const { return interrupt_; }
    void acknowledgeInterrupt();

    bool lowerRomEnabled() const { return !(rmr_ & 0x04); }
    bool upperRomEnabled() const { return !(rmr_ & 0x08); }

    // Rebuilds the colours sent to the monitor. `linear` selects evenly
    // spaced levels, as on the Plus, instead of the CPC's brighter half tone.
    void setMonitor(MonitorKind kind, bool linear);

    // State, named as in the WinAPE register window.
    uint8_t mode() const { return mode_; }                  // mode being displayed
    uint8_t requestedMode() const { return rmr_ & 3; }      // takes effect at the next HSYNC
    uint8_t selectedPen() const { return pen_; }            // 0-15, or 16 for the border
    uint8_t ink(int pen) const { return ink_[pen]; }        // hardware colour number, 0-31
    uint8_t interruptCounter() const { return r52_; }       // R52
    uint32_t colour(int hardwareColour) const { return colours_[hardwareColour & 31]; }

private:
    uint8_t rmr_ = 0;       // ROM enables and requested mode
    uint8_t mode_ = 0;
    uint8_t pen_ = 0;
    uint8_t ink_[17] = {};
    uint32_t rgb_[17] = {};  // ink_ translated through colours_
    uint32_t colours_[32] = {};

    uint8_t r52_ = 0;
    uint8_t vsyncDelay_ = 0;  // HSYNCs left before the counter is resynchronised
    uint8_t hsyncAge_ = 0;    // microseconds since HSYNC rose
    bool interrupt_ = false;
    bool prevHsync_ = false;
    bool prevVsync_ = false;
};

}  // namespace tuxape
