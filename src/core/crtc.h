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
    // The HSYNC in progress began, a character ago, on the very character
    // where the one before it ended: the signal has not moved, but the
    // ASIC's sound channels take it for a new one.
    bool hsyncJoinedJustNow() const
    {
        // (The ASICs' counter was at R3 as the two pulses met, and went on.)
        const uint8_t next = asic() ? static_cast<uint8_t>((reg_[3] + 1) & 0x0F) : 1;
        return hsyncJoined_ && hsc_ == next && hsync_;
    }
    bool vsync() const { return vsync_ && !ghostVsync_; }
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
    // "interlace sync & video" mode, where each frame shows every other
    // line: types 0 and 2 (bits 0 and 2 of the mask) then put out a value
    // worked out as the line begins, or as R8 is written.
    uint8_t ra() const { return (0x05 >> static_cast<unsigned>(type_)) & 1 ? c9Out_ : vlc_; }
    // Interlace: whether the frame in progress is an odd one.
    bool oddFrame() const { return parityFrame_; }

    // Registers and internal counters, named as in the WinAPE register window.
    uint8_t reg(int n) const { return reg_[n & 0x1F]; }
    uint8_t selected() const { return selected_; }
    uint8_t hcc() const { return hcc_; }    // horizontal character counter
    uint8_t vcc() const { return vcc_; }    // vertical character counter
    uint8_t vlc() const { return vlc_; }    // vertical line counter

    // The Plus ASIC's additions. The split: the picture goes on from
    // `address` after line number `line` (C4 in bits 7-3, C9 in bits 2-0; 0
    // for no split). The ASIC looks at the two as that line's display ends
    // (C0 = R1), where it gets the next line's address ready, and again as
    // a frame's last line ends, in place of R12/R13. A value written to
    // its registers only counts a microsecond later, as with the CRTC
    // registers it holds (Compendium 4.4.3). Kevin Thacker's "splittrig"
    // programs hold the split line for nine microseconds at a chosen
    // moment: on a real machine the split shows for delays &36B to &373 on
    // an ordinary line and &352 to &35A on a frame's last line, and so it
    // does here. (Taken as the line began, it showed 39 microseconds early.)
    void setSplit(uint8_t line, uint16_t address)
    {
        splitDueLine_ = line;
        splitDueAddress_ = address & 0x3FFF;
        splitDue_ = 2;
        late_ = true;
    }
    // The Plus's vertical scroll (SSCR bits 6-4) shows each line of a row
    // `lines` further down: line (C9 + lines) modulo 8 of the row on show.
    // That row gives way to the next when the line so shown is its last,
    // C9 + lines = R9, and not where C9 itself gets there. Nothing is added
    // to the address for the lines that wrap round: a row whose scrolled
    // line never meets R9 stays on show, and after a split the lines come
    // from the split's row whatever the scroll. (Kevin Thacker's "vscrl2"
    // on a real GX4000: "vertical scrolling but not perfect because of no
    // ma being added"; with the scroll changed on every line so that each
    // is doubled, the "chunky" row is followed by the same row at its
    // usual size.)
    void setScrollLines(uint8_t lines) { scrollLines_ = lines & 7; }
    // A snapshot has put the scroll back after the counters: the row on
    // show is what an undisturbed frame would have here.
    void scrollRestored();
    // The line as the ASIC numbers it for its raster interrupt and its
    // split: C9's low three bits under six bits of C4. A register of eight
    // bits can therefore only name the first 256 lines of every 512: rows
    // 32 to 63 never match, and a row of more than eight lines has each
    // number several times. (Kevin Thacker's "pritest" on a real machine:
    // one interrupt a frame whatever the line, not two for lines 1 to 55;
    // two with 128 rows, four with rows of 32 lines.)
    uint16_t asicLine() const { return static_cast<uint16_t>((vcc_ & 0x3F) << 3 | (vlc_ & 7)); }
    int frameLine() const { return asic() ? frameLine_ : vcc_ * (reg_[9] + 1) + vlc_; }
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
    uint8_t splitLine_ = 0;
    uint16_t splitAddress_ = 0;
    int frameLine_ = 0;
    bool hsync_ = false;
    HsyncCut hsyncCut_ = HsyncCut::None;
    HsyncCut previousHsyncCut_ = HsyncCut::None;
    bool vsync_ = false;
    bool hDisp_ = false;
    bool vDisp_ = false;
    // Types 0 and 2, first line of a frame with R6 = 0: the border comes
    // and goes with each character, until the line's display ends (18.3.2).
    bool r6Conflict_ = false;
    // What a write to R6 has just decided, which type 0 only shows from
    // the next character on.
    enum class R6Write : uint8_t { None, ConflictOn, ConflictOff, Border };
    R6Write r6Due_ = R6Write::None;
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
    bool rowR4AtEnd_ = false;      // C4 equalled R4 as the last character began
    bool endSeenOnOne_ = false;     // R0 was 1 as character 1 began: the line was to end there
    bool vsyncAllowed_ = false;    // the line reached character 2
    bool r7Match_ = false;         // C4 = R7 has been noted: no VSYNC from it again
    bool vsyncFresh_ = false;      // VSYNC began mid-line: its line count restarts
    bool vsyncDue_ = false;        // R7 met C4 on a line's last character: settled on the next

    // ---- CRTC 1 (UM6845R) ------------------------------------------------
    // This chip counts by what its registers hold as each line ends, with
    // two things of its own (Compendium 11.2.4, 17.4.2). As a line's last
    // character begins it notes whether the frame ends there, and the
    // lines of R5 follow from that note. And each line starts at R12/R13
    // for as long as a status lasts that the start of a frame sets and the
    // end of a row takes down.
    bool lastLine1_ = false;       // C4 and C9 were at the frame's end as the last character began
    bool overran1_ = false;        // ... and the line has gone on past that character
    bool fromR12_ = true;          // each line starts at R12/R13
    bool parityInTest_ = false;    // after a write to R5 at the wrong moment: the lines' parity counts in the row-end test

    // ---- CRTC 2 (MC6845) -------------------------------------------------
    // This chip too decides ahead that a line is the frame's last, but when
    // it looks is another matter (Compendium 12.4.1, 15.6): as the line
    // begins, and whenever R4 or R9 is written, though not during an HSYNC
    // and, on a frame's first line, not before that line's HSYNC has found
    // the frame's end unmet. A line the HSYNC found at the frame's end does
    // not let the next one be the last as it begins: two frames of a single
    // line in a row need R4 or R9 moved away and back. lastLine_ above is
    // what it decided.
    bool lastLineOpen_ = false;    // a write to R4 or R9 may still make this line the last
    bool previousLast_ = false;    // the last HSYNC ended with C4 and C9 at the frame's end
    bool hsyncJudged_ = false;     // ... which the HSYNC in progress has already been asked
    // A VSYNC that comes on the heels of an HSYNC is counted like any
    // other and keeps the next one away, but the pin stays low: the "ghost
    // VSYNC" (15.6, 16.4.3).
    bool ghostVsync_ = false;
    uint8_t r9AtStart_ = 0;        // R9 as the line began: what its first character goes by
    bool newFrame2_ = false;       // a frame has just begun: its first character has not been judged yet
    bool firstLine2_ = false;      // on the first line of a frame that began without an added interlace line
    uint8_t c9Ivm_ = 0;            // the display's own line counter ("C9.IVM")
    // What was set aside where C0 met R1, to be settled as the line ends.
    enum class Held2 : uint8_t { Nothing, Address, ForMode, Start };
    Held2 held2_ = Held2::Nothing;
    uint16_t heldAddress2_ = 0;

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
    void displayEnds(bool keepAddress);
    void r6Written(R6Write effect);
    [[gnu::cold]] void settleLate();
    void endOfLine1(bool oneCharacter);
    bool rowEnds1() const;
    bool keepsAddress1() const;
    void nextRow1();
    void newFrame1();
    void startRow();
    void startFrame();
    void startVsync();

    // ---- Type 2 ----------------------------------------------------------
    void keepAddress2();
    void settleAddress2();
    bool veryLastLine2() const;
    bool rowEnds2() const;
    void latchC9Of2();
    void endFrame2();
    void endOfLine2();
    void countLine2();
    void lineStart2();
    void judgeLineStart2();
    void frameEndWritten2(bool r4);
    void hsyncEnds2();

    // ---- Types 3 and 4 (the 6845 inside Amstrad's ASICs) -----------------
    bool asic() const { return type_ == CrtcType::AsicPlus || type_ == CrtcType::PreAsic; }
    void endOfLineAsic();
    bool frameEndsAsic() const;
    void endFrameAsic();
    void newFrameAsic();
    void rowStartAsic(bool oddFrame);
    uint8_t status1() const;
    uint8_t status2() const;

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

    // Something written is waiting to take effect: R6 on type 0, the
    // split on the ASICs. (Kept after everything else: where the rest
    // sits was measured, and moving it costs a few per cent.)
    bool hsyncJoined_ = false;     // the HSYNC in progress began as the one before ended
    uint8_t r8Due_ = 0;            // type 1: interlace video has just come on (1) or gone off (2)
    bool late_ = false;
    uint8_t splitDue_ = 0;         // characters to go before the split written is seen
    uint8_t splitDueLine_ = 0;
    uint16_t splitDueAddress_ = 0;
    uint8_t frames_ = 0;           // types 3 and 4: frames counted, for the status that turns over every sixteen
    uint8_t scrollLines_ = 0;      // the Plus: lines of vertical scroll
};

}  // namespace tuxape
