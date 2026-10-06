#pragma once

#include <cstdint>
#include <vector>

// WinAPE's "Disc Drive Sounds": a whirr while the drive's motor runs and a
// click each time its head moves a cylinder. Made here from noise, not
// taken from a recording of a drive.
class DriveSound {
public:
    // Adds the drive's sounds to a stretch of sound: stereo samples at so
    // many a second. `steps` is how many cylinders the head has moved
    // since the last call; the clicks are spread out as a drive's are.
    void mix(std::vector<int16_t>& stereo, int rate, bool motor, int steps);
    // Silence at once: the machine was reset, or the sounds turned off.
    void reset();

private:
    uint32_t noise_ = 0x2545F491;
    float whirr_ = 0.0f;      // the motor's noise, smoothed
    float level_ = 0.0f;      // the motor running up and down
    double turn_ = 0.0;       // where the disc is in its turn
    int pending_ = 0;         // clicks still to be heard
    int untilClick_ = 0;      // samples to the next of them
    int clickAge_ = -1;       // samples since the click now sounding began; -1: none
};
