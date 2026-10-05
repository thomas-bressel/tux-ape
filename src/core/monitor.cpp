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

void Monitor::hsync()
{
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
