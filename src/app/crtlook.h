#pragma once

// How the CTM644 shader is set: each figure is a percentage of TuxAPE's own
// setting, which is the monitor as it was photographed. 0 takes the effect
// away, 200 doubles it.
struct CrtLook {
    int curvature = 100;  // the curve of the glass
    int scanLines = 100;  // how much the scan lines show
    int mask = 100;       // how dark the tube's mask is
    int glow = 100;       // the light bright areas throw around them
    int blur = 100;       // how far a pixel runs into its neighbours along a line
    int fringe = 100;     // how far off the blue gun lands

    static constexpr int kMost = 200;
    bool operator==(const CrtLook&) const = default;
};
