#include "core/crtc.h"

namespace tuxape {

namespace {

// Bits that exist in each writable register.
constexpr uint8_t kWriteMask[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F, 0x7F, 0x7F,
    0xFF, 0x1F, 0x7F, 0x1F, 0x3F, 0xFF, 0x3F, 0xFF,
};

}  // namespace

void Crtc::reset()
{
    for (uint8_t& r : reg_)
        r = 0;
    selected_ = 0;
    hcc_ = vcc_ = vlc_ = vtac_ = hsc_ = vsc_ = 0;
    ma_ = maRow_ = 0;
    hsync_ = vsync_ = false;
    hDisp_ = vDisp_ = false;
    inAdjust_ = false;
}

void Crtc::write(uint8_t value)
{
    if (selected_ > 15)
        return;
    const uint8_t old = reg_[selected_];
    reg_[selected_] = value & kWriteMask[selected_];

    switch (selected_) {
    case 6:
        // The UM6845R compares R6 continuously; the others only when the row
        // changes.
        if (type_ == CrtcType::UM6845R && vcc_ == reg_[6])
            vDisp_ = false;
        break;
    case 7:
        // Making R7 equal to the current row starts a VSYNC straight away.
        if (old != reg_[7] && vcc_ == reg_[7] && !vsync_)
            startVsync();
        break;
    }
}

uint8_t Crtc::readStatus() const
{
    switch (type_) {
    case CrtcType::UM6845R:
        // Bit 5 is set while the beam is outside the displayed rows.
        return vDisp_ ? 0x00 : 0x20;
    case CrtcType::AsicPlus:
    case CrtcType::PreAsic:
        return readData();
    default:
        return 0xFF;  // not connected
    }
}

uint8_t Crtc::readData() const
{
    switch (type_) {
    case CrtcType::HD6845S:
        return selected_ >= 12 && selected_ <= 17 ? reg_[selected_] : 0x00;
    case CrtcType::UM6845R:
        if (selected_ == 31)
            return 0xFF;
        return selected_ >= 14 && selected_ <= 17 ? reg_[selected_] : 0x00;
    case CrtcType::MC6845:
        return selected_ >= 14 && selected_ <= 17 ? reg_[selected_] : 0x00;
    default: {
        // Only three address bits are decoded.
        static constexpr uint8_t map[8] = {16, 17, 10, 11, 12, 13, 14, 15};
        return reg_[map[selected_ & 7]];
    }
    }
}

bool Crtc::displayEnable() const
{
    if (!hDisp_ || !vDisp_)
        return false;
    // R6 = 0 blanks the picture at once on the UM6845R.
    if (type_ == CrtcType::UM6845R)
        return reg_[6] != 0;
    // Where R8 has skew bits, setting both turns the display off.
    if (type_ != CrtcType::MC6845 && (reg_[8] & 0x30) == 0x30)
        return false;
    return true;
}

void Crtc::tick()
{
    if (hcc_ == reg_[0]) {
        hcc_ = 0;
        endOfLine();
    } else {
        ++hcc_;
    }

    if (hcc_ == 0) {
        hDisp_ = true;
        // While on the first row the UM6845R follows R12/R13 live.
        if (type_ == CrtcType::UM6845R && vcc_ == 0)
            maRow_ = startAddress();
        ma_ = maRow_;
    } else {
        ma_ = (ma_ + 1) & 0x3FFF;
    }

    if (hcc_ == reg_[1]) {
        hDisp_ = false;
        // The address reached at the end of a row's last line becomes the
        // start of the next row.
        if (vlc_ == reg_[9])
            maRow_ = ma_;
    }

    if (hsync_) {
        hsc_ = (hsc_ + 1) & 0x0F;
        if (hsc_ == (reg_[3] & 0x0F))
            hsync_ = false;
    }
    if (hcc_ == reg_[2] && !hsync_) {
        // A width of 0 means no HSYNC at all on types 0 and 1, and 16
        // characters on the others.
        const bool none = (reg_[3] & 0x0F) == 0
                          && (type_ == CrtcType::HD6845S || type_ == CrtcType::UM6845R);
        if (!none) {
            hsync_ = true;
            hsc_ = 0;
        }
    }
}

void Crtc::endOfLine()
{
    if (vsync_) {
        // Types 1 and 2 ignore the programmed width and always use 16 lines.
        const bool fixed = type_ == CrtcType::UM6845R || type_ == CrtcType::MC6845;
        const uint8_t width = fixed ? 0 : reg_[3] >> 4;
        vsc_ = (vsc_ + 1) & 0x0F;
        if (vsc_ == width)
            vsync_ = false;
    }

    if (inAdjust_) {
        vtac_ = (vtac_ + 1) & 0x1F;
        vlc_ = (vlc_ + 1) & 0x1F;
        if (vtac_ == reg_[5])
            startFrame();
    } else if (vlc_ == reg_[9]) {
        if (vcc_ == reg_[4]) {
            if (reg_[5] != 0) {
                // Extra scanlines after the last row to trim the frame length.
                inAdjust_ = true;
                vtac_ = 0;
                vlc_ = 0;
                vcc_ = (vcc_ + 1) & 0x7F;
                startRow();
            } else {
                startFrame();
            }
        } else {
            vlc_ = 0;
            vcc_ = (vcc_ + 1) & 0x7F;
            startRow();
        }
    } else {
        vlc_ = (vlc_ + 1) & 0x1F;
    }
}

void Crtc::startRow()
{
    if (vcc_ == reg_[6])
        vDisp_ = false;
    if (vcc_ == reg_[7] && !vsync_)
        startVsync();
}

void Crtc::startFrame()
{
    inAdjust_ = false;
    vtac_ = 0;
    vlc_ = 0;
    vcc_ = 0;
    vDisp_ = true;
    maRow_ = startAddress();
    startRow();
}

void Crtc::startVsync()
{
    vsync_ = true;
    vsc_ = 0;
}

}  // namespace tuxape
