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
    hsyncCut_ = previousHsyncCut_ = HsyncCut::None;
    hDisp_ = vDisp_ = false;
    inAdjust_ = false;
    lastLine_ = adjust_ = adjustRunning_ = adjustUndecided_ = false;
    c9Enabled_ = c4CountArmed_ = c9MatchAtEnd_ = false;
    vsyncAllowed_ = r7Match_ = vsyncFresh_ = false;
    parityFrame_ = parityR6_ = false;
    extraLine_ = interlaceLine_ = midVsync_ = lateVsync_ = false;
    c9Out_ = 0;
}

void Crtc::write(uint8_t value, bool early)
{
    if (selected_ > 15)
        return;
    const uint8_t old = reg_[selected_];
    reg_[selected_] = value & kWriteMask[selected_];

    switch (selected_) {
    case 2:
        // R2 set to the character in progress starts the HSYNC there and
        // then ("R2.JIT", Compendium 14.7): the comparison is not only made
        // as the character begins.
        if (!hsync_ && hcc_ == reg_[2] && old != reg_[2]) {
            const bool none = (reg_[3] & 0x0F) == 0
                              && (type_ == CrtcType::HD6845S || type_ == CrtcType::UM6845R);
            if (!none) {
                hsync_ = true;
                hsc_ = 0;
            }
        }
        break;
    case 3:
        // The HSYNC ends when its counter meets R3. Bringing R3 down to the
        // counter while the pulse lasts ("R3.JIT") ends it there and then
        // on the three discrete chips; lower still, and the counter has to
        // go all the way round. The UM6845R also takes a width of 0 as "no
        // HSYNC" at any moment (Compendium 14.5).
        if (hsync_ && type_ != CrtcType::AsicPlus && type_ != CrtcType::PreAsic) {
            const uint8_t width = reg_[3] & 0x0F;
            const bool noPulse = width == 0 && type_ != CrtcType::MC6845;
            if (early) {
                // In time for this character's own decisions: a pulse about
                // to start with a width of 0 does not, and one whose counter
                // has just met the new width ends as it normally would.
                if (hsc_ == 0 && noPulse) {
                    hsync_ = false;
                    hsyncCut_ = HsyncCut::Never;
                } else if (hsc_ != 0 && (width == hsc_ || (noPulse && type_ == CrtcType::UM6845R))) {
                    hsync_ = false;
                    hsyncCut_ = HsyncCut::AtStart;
                }
            } else if ((width == hsc_ && (old & 0x0F) != width) || (noPulse && type_ == CrtcType::UM6845R)) {
                hsync_ = false;
                // The pulse outlives the write by a quarter of a character.
                // Cut on its very first character with a width of 0, types
                // 0 and 1 give the half character it had begun with
                // (14.5.4).
                if (type_ == CrtcType::MC6845)
                    hsyncCut_ = HsyncCut::None;
                else
                    hsyncCut_ = hsc_ == 0 && width == 0 ? HsyncCut::SecondHalf : HsyncCut::AfterQuarter;
            }
        }
        break;
    case 6:
        // The UM6845R compares R6 continuously; the others only when the row
        // changes.
        if (type_ == CrtcType::UM6845R && vcc_ == reg_[6])
            vDisp_ = false;
        break;
    case 7:
        if (type_ == CrtcType::HD6845S) {
            // Making R7 equal to the current row starts a VSYNC at once,
            // unless the line is still on its first two characters: then
            // the match is noted and that VSYNC is lost (16.4.1.1). The
            // note is only dropped at the start of a line, so taking R7
            // away and back within one line brings nothing; and during a
            // VSYNC the register is not looked at (16.3).
            if (!vsync_ && vcc_ == reg_[7] && !r7Match_) {
                r7Match_ = true;
                if (hcc_ >= 2) {
                    startVsync();
                    vsyncFresh_ = true;
                }
            }
        } else if (old != reg_[7] && vcc_ == reg_[7] && !vsync_) {
            // Making R7 equal to the current row starts a VSYNC straight away.
            startVsync();
        }
        break;
    }

    // On the last character of a line the C4 step follows the registers to
    // the end: R9 brought to C9 there still counts.
    if (type_ == CrtcType::HD6845S && hcc_ == reg_[0] && c9AtR9())
        c9MatchAtEnd_ = true;
}

void Crtc::restoreCounters(uint8_t hcc, uint8_t vcc, uint8_t vlc, uint8_t hsc, uint8_t vsc, bool hsync, bool vsync)
{
    hcc_ = hcc;
    vcc_ = vcc & 0x7F;
    vlc_ = vlc & 0x1F;
    hsc_ = hsc & 0x0F;
    vsc_ = vsc & 0x0F;
    hsync_ = hsync;
    vsync_ = vsync;
    hsyncCut_ = previousHsyncCut_ = HsyncCut::None;
    // What a snapshot does not hold is set to what an undisturbed frame
    // would have at this place.
    hDisp_ = hcc_ < reg_[1];
    vDisp_ = vcc_ < reg_[6];
    maRow_ = static_cast<uint16_t>((startAddress() + vcc_ * reg_[1]) & 0x3FFF);
    ma_ = static_cast<uint16_t>((maRow_ + hcc_) & 0x3FFF);
    inAdjust_ = false;
    vtac_ = 0;
    extraLine_ = interlaceLine_ = midVsync_ = lateVsync_ = false;
    latchC9();
    lastLine_ = onLastLine();
    adjust_ = lastLine_ && hcc_ < 3;
    adjustRunning_ = adjustUndecided_ = false;
    c9Enabled_ = hcc_ >= 1;
    vsyncAllowed_ = hcc_ >= 2;
    c4CountArmed_ = c9AtR9();
    c9MatchAtEnd_ = false;
    r7Match_ = vcc_ == reg_[7];
    vsyncFresh_ = false;
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
    previousHsyncCut_ = hsyncCut_;
    hsyncCut_ = HsyncCut::None;

    const bool type0 = type_ == CrtcType::HD6845S;
    if (hcc_ == reg_[0]) {
        hcc_ = 0;
        if (type0)
            lineStart0();
        else
            endOfLine();
    } else {
        ++hcc_;
    }

    if (type0) {
        switch (hcc_) {
        case 1:
            // From here C9 may count at the end of the line. The last-line
            // test still follows the registers: a write during character 0
            // can make or unmake it.
            c9Enabled_ = true;
            if (!adjust_ || lastLine_) {
                lastLine_ = onLastLine();
                adjust_ = lastLine_;
                adjustRunning_ = false;
            }
            break;
        case 2:
            vsyncAllowed_ = true;
            if (lastLine_ && adjust_) {
                // A write during character 1 that breaks the last-line
                // condition turns this line into the first adjustment line.
                if (!onLastLine()) {
                    lastLine_ = false;
                    adjustRunning_ = true;
                } else {
                    adjustUndecided_ = true;
                }
            }
            break;
        case 3:
            decideAdjustment0();
            break;
        }
        // The C4 step is decided on the last character and cannot be taken
        // back: not by R9 moving away during that character (10.3.1.2), nor
        // by R0 changing so that the line carries on.
        if (hcc_ == reg_[0] && c9AtR9())
            c9MatchAtEnd_ = true;
        // The VSYNC of an even frame, held back to the middle of the line.
        if (midVsync_ && hcc_ == reg_[0] / 2) {
            midVsync_ = false;
            if (!vsync_) {
                startVsync();
                vsyncFresh_ = true;
            }
        }
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
        if (c9AtR9())
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

// ---- CRTC 0 ----------------------------------------------------------------

// R5 is looked at up to the third character of the last line, no later
// (11.4.2): with adjustment lines to add, the adjustment takes over;
// without, the frame ends with this line.
void Crtc::decideAdjustment0()
{
    if (!adjustUndecided_)
        return;
    adjustUndecided_ = false;
    if (reg_[5] != 0) {
        lastLine_ = false;
        adjustRunning_ = true;
    } else {
        adjust_ = adjustRunning_ = false;
    }
}

// Has C9 reached the end of the character row? Outside "interlace sync &
// video" mode that is C9 = R9. In that mode each frame only shows the lines
// of its parity: C9 is put out doubled, with the parity as its low bit, and
// is compared with R9 whose low bit is replaced by that parity. With R9 odd
// the row has one line more in total than the two frames can share evenly;
// the rows showing even lines then get it, as a line that follows the match
// and ends the row whatever happens during it (19.5.2).
//
// The parity counts as soon as R8 is written, but C9 is only doubled from
// the next line on, and for one line too long when the mode is left: the
// line of the change compares the wrong value (19.8.1). The Compendium gives
// the comparison as "R9 + parity"; taking the low bit away first, and the
// extra line being what it is said to be here, is what the 40 measurements
// of Shaker B test 1 on a real CRTC 0 need (R9 = 5 and 6, both parities).
bool Crtc::c9AtR9() const
{
    if (type_ != CrtcType::HD6845S)
        return vlc_ == reg_[9];
    if (extraLine_)
        return true;
    if (!interlaceVideo())
        return c9Out_ == reg_[9];
    return !longRow0() && c9Out_ == ((reg_[9] & 0x1E) | c9Parity0());
}

// A row of even lines with R9 odd: its match brings one more line.
bool Crtc::longRow0() const
{
    return interlaceVideo() && (reg_[9] & 1) != 0 && !c9Parity0();
}

// C9 as the line that starts will put it out. Only here does it take, or
// lose, its doubled form.
void Crtc::latchC9()
{
    c9Out_ = interlaceVideo() ? static_cast<uint8_t>((vlc_ << 1 | c9Parity0()) & 0x1F) : vlc_;
}

void Crtc::rowChanged0()
{
    if (vcc_ == reg_[6]) {
        vDisp_ = false;
        // This is also where the chip settles the parity of the next frame,
        // whatever R8 holds: a frame whose C4 never meets R6 leaves the
        // parity where it was (19.5.2).
        parityR6_ = !parityFrame_;
    }
}

void Crtc::newFrame0()
{
    vcc_ = 0;
    vlc_ = 0;
    lastLine_ = adjust_ = adjustRunning_ = false;
    interlaceLine_ = false;
    vDisp_ = true;
    parityFrame_ = parityR6_;
    maRow_ = startAddress();
    rowChanged0();
}

// The frame has run its lines. With an interlace mode set, one that is to
// be followed by an odd frame gets one more line, counted as if it had been
// asked for through R5 (11.2.2, 11.9, 19.6.1).
void Crtc::endFrame0()
{
    if (!interlace() || !parityR6_) {
        newFrame0();
        return;
    }
    interlaceLine_ = true;
    if (vcc_ == reg_[4]) {
        vcc_ = (vcc_ + 1) & 0x7F;
        rowChanged0();
        vlc_ = 0;
    } else {
        vlc_ = (vlc_ + 1) & 0x1F;
    }
    lastLine_ = false;
    adjust_ = adjustRunning_ = true;
}

// C4 has just become equal to R7. The VSYNC follows at once, except that an
// interlace mode holds it back: to the middle of the line on even frames,
// and by a whole line on the rows that are a line longer on odd frames,
// which keeps the two frames in step (16.5.1, 19.5.2).
void Crtc::matchedR7()
{
    if (interlace() && !parityFrame_)
        midVsync_ = true;
    else if (interlaceVideo() && parityFrame_ && (reg_[9] & 1) && (vcc_ & 1))
        lateVsync_ = true;
    else
        startVsync();
}

void Crtc::lineStart0()
{
    // A line of three characters or fewer never reached the place where R5
    // is settled.
    decideAdjustment0();

    // Did the line that just ended get as far as character 1? If not, C9
    // (and the VSYNC line count with it) stays as it is.
    const bool managed = c9Enabled_;
    c9Enabled_ = false;
    const bool c4Step = c9MatchAtEnd_;
    c9MatchAtEnd_ = false;

    if (managed) {
        if (vsync_) {
            if (vsyncFresh_) {
                // Started part-way through the line: the count begins here.
                vsyncFresh_ = false;
                vsc_ = 0;
            } else {
                vsc_ = (vsc_ + 1) & 0x0F;
                if (vsc_ == reg_[3] >> 4)
                    vsync_ = false;
            }
        }

        // C4 moves on if C9 equalled R9 at some point of the last
        // character; C9 goes back to 0 only if it still does (10.3.1.2).
        const bool c9Match = c4Step && c9AtR9();
        const bool extra = !extraLine_ && longRow0() && c9Out_ == (reg_[9] & 0x1E);
        extraLine_ = false;
        const uint8_t row = vcc_;
        const uint8_t nextLine = (vlc_ + 1) & 0x1F;
        if (interlaceLine_) {
            newFrame0();
        } else if (adjust_) {
            if (vcc_ != reg_[4]) {
                // Adjustment lines: C9 runs on to R5 whatever R9 says, and
                // reaching it ends the frame (11.2.2, 13.2.4). One that was
                // only armed, by a last line of one or two characters, gives
                // a single line when R5 is 0, however long that line is
                // (13.2.5, Shaker B "RVNI LTD").
                const bool reached = ((vlc_ + 1) & 0x1F) == reg_[5];
                if (reached || (!adjustRunning_ && vlc_ == reg_[5])) {
                    endFrame0();
                } else {
                    vlc_ = (vlc_ + 1) & 0x1F;
                }
            } else {
                // Still on row R4: C4 steps past it, once.
                if (c4Step) {
                    vcc_ = (vcc_ + 1) & 0x7F;
                    rowChanged0();
                }
                vlc_ = c9Match ? 0 : (vlc_ + 1) & 0x1F;
                lastLine_ = false;
            }
        } else if (lastLine_) {
            endFrame0();
        } else {
            if (c4Step) {
                vcc_ = (vcc_ + 1) & 0x7F;
                rowChanged0();
            }
            vlc_ = c9Match ? 0 : (vlc_ + 1) & 0x1F;
        }

        // Work out what this new line is.
        extraLine_ = extra && !interlaceLine_ && vcc_ == row && vlc_ == nextLine;
        latchC9();
        c4CountArmed_ = c9AtR9();
        if (interlaceLine_) {
            // Nothing to work out: the frame ends with this line.
        } else if (!adjust_ || lastLine_ || onLastLine()) {
            lastLine_ = onLastLine();
            if (lastLine_) {
                adjust_ = true;
                adjustRunning_ = false;
            }
        }
    } else if (c4CountArmed_) {
        // The previous line stopped on character 0 (R0 = 0). C9 is frozen,
        // but the C4 step armed when it equalled R9 still happens, once. An
        // adjustment armed with it is then under way: when lines get longer
        // again C9 counts on to R5 (13.2.6, Shaker A7). What sets this apart
        // from the single line above is not spelled out in the Compendium;
        // C9 not having gone back to 0 with the C4 step is what fits both.
        c4CountArmed_ = false;
        vcc_ = (vcc_ + 1) & 0x7F;
        rowChanged0();
        lastLine_ = false;
        adjustRunning_ = adjust_;
        latchC9();
    } else {
        latchC9();
    }

    // A VSYNC needs C4 to become equal to R7, and the previous line to have
    // reached character 2. Failing the second, the match is recorded all
    // the same and that VSYNC is lost (13.2.2, 16.3).
    if (lateVsync_) {
        lateVsync_ = false;
        if (!vsync_)
            startVsync();
    }
    const bool match = vcc_ == reg_[7];
    if (match && !r7Match_ && !vsync_ && vsyncAllowed_)
        matchedR7();
    r7Match_ = match;
    vsyncAllowed_ = false;
}

}  // namespace tuxape
