#include "core/monitor.h"

#include <algorithm>

namespace tuxape {

namespace {
constexpr uint32_t kBlack = 0xFF000000;
}

Monitor::Monitor()
    : bufferA_(kWidth * kHeight, kBlack)
    , bufferB_(kWidth * kHeight, kBlack)
    , back_(bufferA_.data())
    , front_(bufferB_.data())
{
}

void Monitor::reset()
{
    std::fill(bufferA_.begin(), bufferA_.end(), kBlack);
    std::fill(bufferB_.begin(), bufferB_.end(), kBlack);
    x_ = y_ = 0;
}

void Monitor::hsync(bool late)
{
    // The ASICs (CRTC 3 and 4) keep the HSYNC in step with the picture,
    // which a Gate Array draws a character behind it: their pulse reaches
    // the monitor a microsecond later. On a CTM 640 or 644 the picture
    // would sit a character to the left. But Amstrad set the monitors of
    // those machines, the Plus's CM14 and the CTM sold with the last CPCs,
    // so that the frame is centred all the same (Compendium 15.1): the
    // visible area starts a character sooner after the pulse.
    firstColumn_ = late ? kFirstColumn - 1 : kFirstColumn;
    if (x_ >= kMinLine)
        newLine();
}

void Monitor::vsync()
{
    if (y_ >= kMinFrame)
        newFrame();
}

void Monitor::newLine()
{
    x_ = 0;
    if (++y_ >= kFreeRunFrame)
        newFrame();
}

void Monitor::newFrame()
{
    y_ = 0;
    std::swap(back_, front_);
    std::fill(back_, back_ + kWidth * kHeight, kBlack);
    ++frameNumber_;
}

}  // namespace tuxape
