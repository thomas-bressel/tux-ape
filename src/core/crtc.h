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
    // `early` tells apart the two places within a character where a write
    // can land: those of OUTI and OUT (n),A come before the chip has dealt
    // with the HSYNC for that character, those of OUT (C),r after.
    void write(uint8_t value, bool early = false);
    uint8_t readStatus() const;  // port &BExx
    uint8_t readData() const;    // port &BFxx

    void tick();

    // Puts back the counters and sync outputs taken from a snapshot. The
    // registers themselves go through select() and write().
    void restoreCounters(uint8_t hcc, uint8_t vcc, uint8_t vlc, uint8_t hsc, uint8_t vsc, bool hsync, bool vsync);

    // Outputs.
    bool hsync() const { return hsync_; }
    bool vsync() const { return vsync_; }
    // How a write to R3 cut the HSYNC short during the character in
    // progress, and during the one before it.
    enum class HsyncCut : uint8_t {
        None,
        AfterQuarter,  // the pulse went on for a quarter of the character
        SecondHalf,    // the pulse was the second half of the character, no more
        AtStart,       // the pulse ended as the character began
        Never,         // the pulse that was to start here did not
    };
    HsyncCut hsyncCut() const { return hsyncCut_; }
    HsyncCut previousHsyncCut() const { return previousHsyncCut_; }
    // Display enable (DISPTMG) for the first (0) and second (1) half of
    // the character: the Gate Array takes it in once per byte.
    bool displayEnable(int half = 0) const;
    uint16_t ma() const { return ma_; }  // 14-bit refresh address
    // Raster line within the character row. It is C9 itself, except in
    // "interlace sync & video" mode, where each frame shows every other line.
    uint8_t ra() const { return type_ == CrtcType::HD6845S ? c9Out_ : vlc_; }
    // Interlace: whether the frame in progress is an odd one.
    bool oddFrame() const { return parityFrame_; }

    // Registers and internal counters, named as in the WinAPE register window.
    uint8_t reg(int n) const { return reg_[n & 0x1F]; }
    uint8_t selected() const { return selected_; }
    uint8_t hcc() const { return hcc_; }    // horizontal character counter
    uint8_t vcc() const { return vcc_; }    // vertical character counter
    uint8_t vlc() const { return vlc_; }    // vertical line counter
    uint8_t vtac() const { return vtac_; }  // vertical total adjust counter
    uint8_t hsc() const { return hsc_; }    // horizontal sync counter
    uint8_t vsc() const { return vsc_; }    // vertical sync counter
    bool inVerticalAdjust() const { return type_ == CrtcType::HD6845S ? adjust_ && !lastLine_ : inAdjust_; }

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
    HsyncCut hsyncCut_ = HsyncCut::None;
    HsyncCut previousHsyncCut_ = HsyncCut::None;
    bool vsync_ = false;
    bool hDisp_ = false;
    bool vDisp_ = false;
    // DISPTMG before R8's skew, for the character before the one in
    // progress (bits 0 and 1: its two halves) and the one before that (bits
    // 2 and 3).
    uint8_t dispHistory_ = 0;
    bool inAdjust_ = false;

    // ---- CRTC 0 (HD6845S / UM6845) ---------------------------------------
    // This chip makes its vertical decisions at fixed places on the line
    // (Compendium, chapters 10 to 13 and 16): during the first two
    // characters it works out whether the line is the last of the frame and
    // arms the vertical adjustment by default; on the third it keeps or
    // drops that adjustment and allows the next VSYNC. Lines too short to
    // get there leave things armed or frozen, which is what the "rupture"
    // techniques play with.
    bool lastLine_ = false;        // this line was found to be the last of the frame
    bool adjust_ = false;          // vertical adjustment armed or running
    bool adjustRunning_ = false;   // ... and past the point where it can be dropped
    bool adjustUndecided_ = false; // still to be confirmed against R5
    bool c9Enabled_ = false;       // the line reached character 1: C9 may count
    bool c4CountArmed_ = false;    // C9 equalled R9 on character 0
    bool c9MatchAtEnd_ = false;    // C9 equalled R9 during the last character
    bool vsyncAllowed_ = false;    // the line reached character 2
    bool r7Match_ = false;         // C4 = R7 has been noted: no VSYNC from it again
    bool vsyncFresh_ = false;      // VSYNC began mid-line: its line count restarts

    // ---- Interlace (R8, Compendium chapter 19) ---------------------------
    // Frames are told apart by a parity that the chip keeps whatever R8
    // holds. With an interlace mode set, even frames get one more line and
    // their VSYNC waits for the middle of the line; in "sync & video" mode
    // C9 is also put out doubled, with the parity as its low bit.
    bool parityFrame_ = false;     // the frame in progress is an odd one
    bool parityR6_ = false;        // parity the next frame will take (set as C4 meets R6)
    uint8_t c9Out_ = 0;            // C9 as put out on the line in progress
    bool extraLine_ = false;       // on the line a row of even lines gets when R9 is odd
    bool interlaceLine_ = false;   // on the line added to an even frame
    bool midVsync_ = false;        // a VSYNC is waiting for the middle of the line
    bool lateVsync_ = false;       // a VSYNC is waiting for the next line
    bool parityC9_ = false;        // types 3 and 4: the lines shown are the odd ones

    uint16_t startAddress() const { return static_cast<uint16_t>((reg_[12] << 8 | reg_[13]) & 0x3FFF); }
    void endOfLine(bool oneCharacter);
    void startRow();
    void startFrame();
    void startVsync();

    // ---- Types 3 and 4 (the 6845 inside Amstrad's ASICs) -----------------
    bool asic() const { return type_ == CrtcType::AsicPlus || type_ == CrtcType::PreAsic; }
    void endOfLineAsic();
    void endFrameAsic();
    void newFrameAsic();
    void rowStartAsic(bool oddFrame);

    uint8_t dispNow() const;
    bool interlace() const { return (reg_[8] & 1) != 0; }
    bool interlaceVideo() const { return (reg_[8] & 3) == 3; }
    bool c9Parity0() const { return parityFrame_ != ((reg_[9] & 1) != 0 && (vcc_ & 1) != 0); }
    bool c9AtR9() const;
    bool longRow0() const;
    void latchC9();
    bool onLastLine() const { return vcc_ == reg_[4] && c9AtR9(); }
    void lineStart0();
    void rowChanged0();
    void newFrame0();
    void endFrame0();
    void matchedR7();
    void decideAdjustment0();
};

}  // namespace tuxape
