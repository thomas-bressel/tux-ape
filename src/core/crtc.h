#pragma once

#include <cstdint>

namespace tuxape {

// 6845 CRT controller, in the five variants found in Amstrad machines. They
// are numbered as in WinAPE and in CPC literature.
enum class CrtcType : uint8_t {
    HD6845S = 0,   // Hitachi HD6845S / UMC UM6845
    UM6845R = 1,   // UMC UM6845R
    MC6845 = 2,    // Motorola MC6845
    AsicPlus = 3,  // 6845 inside the Amstrad Plus ASIC
    PreAsic = 4,   // "cost down" CPC 40226 gate array
};

// The CRTC is clocked once per microsecond (one character, 16 mode-2 pixels).
// After each tick() its outputs describe that character: the refresh address,
// the raster line, and the sync and display-enable signals.
//
// The differences between the five types follow the "Amstrad CPC CRTC
// Compendium" by Longshot / Logon System (CC BY-NC-ND).
class Crtc {
public:
    void setType(CrtcType type) { type_ = type; }
    CrtcType type() const { return type_; }

    void reset();

    // Bus side.
    void select(uint8_t value) { selected_ = value & 0x1F; }
    void write(uint8_t value);
    uint8_t readStatus() const;  // port &BExx
    uint8_t readData() const;    // port &BFxx

    void tick();

    // Outputs.
    bool hsync() const { return hsync_; }
    bool vsync() const { return vsync_; }
    bool displayEnable() const;
    uint16_t ma() const { return ma_; }  // 14-bit refresh address
    uint8_t ra() const { return vlc_; }  // raster line within the character row

    // Registers and internal counters, named as in the WinAPE register window.
    uint8_t reg(int n) const { return reg_[n & 0x1F]; }
    uint8_t selected() const { return selected_; }
    uint8_t hcc() const { return hcc_; }    // horizontal character counter
    uint8_t vcc() const { return vcc_; }    // vertical character counter
    uint8_t vlc() const { return vlc_; }    // vertical line counter
    uint8_t vtac() const { return vtac_; }  // vertical total adjust counter
    uint8_t hsc() const { return hsc_; }    // horizontal sync counter
    uint8_t vsc() const { return vsc_; }    // vertical sync counter
    bool inVerticalAdjust() const { return inAdjust_; }

private:
    CrtcType type_ = CrtcType::HD6845S;
    uint8_t reg_[32] = {};
    uint8_t selected_ = 0;

    uint8_t hcc_ = 0;
    uint8_t vcc_ = 0;
    uint8_t vlc_ = 0;
    uint8_t vtac_ = 0;
    uint8_t hsc_ = 0;
    uint8_t vsc_ = 0;
    uint16_t ma_ = 0;
    uint16_t maRow_ = 0;  // refresh address at the start of the current row
    bool hsync_ = false;
    bool vsync_ = false;
    bool hDisp_ = false;
    bool vDisp_ = false;
    bool inAdjust_ = false;

    uint16_t startAddress() const { return static_cast<uint16_t>((reg_[12] << 8 | reg_[13]) & 0x3FFF); }
    void endOfLine();
    void startRow();
    void startFrame();
    void startVsync();
};

}  // namespace tuxape
