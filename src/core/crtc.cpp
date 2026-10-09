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
    r6Conflict_ = false;
    r6Due_ = R6Write::None;
    splitDue_ = 0;
    late_ = false;
    dispHistory_ = 0;
    inAdjust_ = false;
    lastLine_ = adjust_ = adjustRunning_ = adjustUndecided_ = false;
    c9Enabled_ = c4CountArmed_ = c9MatchAtEnd_ = rowR4AtEnd_ = false;
    vsyncAllowed_ = r7Match_ = vsyncFresh_ = vsyncDue_ = false;
    lastLineOpen_ = previousLast_ = hsyncJudged_ = false;
    ghostVsync_ = false;
    r9AtStart_ = 0;
    newFrame2_ = firstLine2_ = false;
    c9Ivm_ = 0;
    held2_ = Held2::Nothing;
    heldAddress2_ = 0;
    lastLine1_ = overran1_ = false;
    fromR12_ = true;
    parityInTest_ = false;
    r8Due_ = 0;
    parityFrame_ = parityR6_ = parityC9_ = false;
    extraLine_ = interlaceLine_ = midVsync_ = lateVsync_ = false;
    c9Out_ = 0;
    frames_ = 0;
}

void Crtc::write(uint8_t value, bool early)
{
    if (selected_ > 15)
        return;
    const uint8_t old = reg_[selected_];
    reg_[selected_] = value & kWriteMask[selected_];

    switch (selected_) {
    case 1:
        // R1 is looked at all along the character too: brought to the one
        // in progress, it ends the line's display there, and on a row's
        // last line the address reached is kept for the next row (17.3).
        // Hence R1 = 0 still counts when written on character 0 (17.5.1).
        // The ASICs have made up their mind by then (17.5.2), and type 2
        // has already kept, or not, the address for the next row (17.4.3).
        // Type 1 keeps it as the character ends, if R1 is still there.
        if (!asic() && hcc_ == reg_[1] && old != reg_[1])
            displayEnds(type_ == CrtcType::HD6845S);
        break;
    case 0:
        // R0 brought to the character in progress makes it the line's
        // last: type 1 takes its note of the frame's end there.
        if (type_ == CrtcType::UM6845R && hcc_ == reg_[0] && old != reg_[0] && vcc_ == reg_[4] && rowEnds1())
            lastLine1_ = true;
        break;
    case 5:
        // Type 1's "rupture for dummies" (11.6): R5 brought from 0 to
        // something else during a line's last character upsets the logic
        // that has just looked for the frame's end. On a line that is not
        // its row's last, every line from there starts at R12/R13 as on a
        // frame's first row, and the parity of the lines has its say in
        // the test that ends this; on a row's last line it is called off
        // instead.
        if (type_ == CrtcType::UM6845R && old == 0 && reg_[5] != 0 && hcc_ == reg_[0]) {
            fromR12_ = !rowEnds1();
            parityInTest_ = fromR12_;
        }
        break;
    case 8:
        // Type 1, "interlace sync & video" coming on or going off: bit 0 of
        // C9 and the two parities are worked out anew, in two steps a
        // microsecond apart (19.5.3). On the first, the parity of the
        // lines is that of C9, turned over on an odd row when R9 is even,
        // and C9's lowest bit takes it.
        if (type_ == CrtcType::UM6845R && ((old & 3) == 3) != interlaceVideo()) {
            const bool oddRow = (vcc_ & 1) && !(reg_[9] & 1);
            parityC9_ = ((vlc_ & 1) != 0) != oddRow;
            vlc_ = static_cast<uint8_t>((vlc_ & 0x1E) | parityC9_);
            r8Due_ = interlaceVideo() ? 1 : 2;
            late_ = true;
        }
        // Type 2 puts out the other counter, or C9 again, from the very
        // character of the write (19.5.4).
        if (type_ == CrtcType::MC6845)
            latchC9Of2();
        break;
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
                hsyncJudged_ = false;
                hsyncJoined_ = false;
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
            } else if (type_ == CrtcType::MC6845 && hsc_ == 0) {
                // Type 2 on the pulse's first character: a width of 0 is 16
                // characters for this chip, and there is nothing to cut
                // (14.5.4, 14.6; Shaker "R3 JIT", R3 = 0 written on C0 = R2,
                // on a real chip: sixteen characters of black, and the mode
                // set at their end).
            } else if ((width == hsc_ && (old & 0x0F) != width) || (noPulse && type_ == CrtcType::UM6845R)) {
                hsync_ = false;
                // The pulse outlives the write by a quarter of a character.
                // Cut on its very first character with a width of 0, types
                // 0 and 1 give the half character it had begun with
                // (14.5.4).
                hsyncCut_ = hsc_ == 0 && width == 0 ? HsyncCut::SecondHalf : HsyncCut::AfterQuarter;
                if (type_ == CrtcType::MC6845)
                    hsyncEnds2();
            }
        }
        break;
    case 4:
    case 9:
        if (type_ == CrtcType::MC6845)
            frameEndWritten2(selected_ == 4);
        break;
    case 6:
        // Types 0, 1 and 2 look at R6 all along the line: made equal to the
        // row being drawn, it brings the border for the rest of the frame
        // (18.2.2, 18.2.3). Types 3 and 4 only look when a line starts.
        if (type_ == CrtcType::UM6845R) {
            if (vcc_ == reg_[6])
                vDisp_ = false;
        } else if (type_ == CrtcType::HD6845S || type_ == CrtcType::MC6845) {
            // On types 0 and 2 the first line of a frame is a case of its
            // own: R6 = 0 there starts the coming and going of the border,
            // and another value calls it off (18.3.2).
            R6Write effect = R6Write::None;
            if (vcc_ == 0 && vlc_ == 0)
                effect = reg_[6] == 0 ? R6Write::ConflictOn : R6Write::ConflictOff;
            else if (vcc_ == reg_[6])
                effect = R6Write::Border;
            // Type 2 shows it on the character of the write, type 0 on the
            // next one (Shaker AP, "R6 stories", the patchworks of R6 = 0/8
            // and of R6 = 9/25: photographs of both chips, five characters
            // shown on type 0 and four on type 2 for a write on the fifth).
            if (type_ == CrtcType::HD6845S) {
                r6Due_ = effect;
                late_ = true;
            } else {
                r6Written(effect);
            }
        }
        break;
    case 7:
        if (type_ == CrtcType::HD6845S) {
            // Making R7 equal to the current row starts a VSYNC at once,
            // unless the line is still on its first two characters: then
            // the match is noted and that VSYNC is lost (16.4.1.1). The
            // note is only dropped at the start of a line, so taking R7
            // away and back within one line brings nothing; and during a
            // VSYNC the register is not looked at (16.3). On the line's
            // last character the VSYNC waits for the next one, and only
            // comes if C4 is still R7 there: on a row's last line the write
            // is too late (Shaker "OUTI story", R7 last chance, against
            // "VSYNC conditions", fourth screen, both on a real chip).
            if (!vsync_ && vcc_ == reg_[7] && !r7Match_) {
                r7Match_ = true;
                if (hcc_ >= 2 && hcc_ == reg_[0]) {
                    vsyncDue_ = true;
                } else if (hcc_ >= 2) {
                    startVsync();
                    vsyncFresh_ = true;
                }
            }
        } else if (type_ == CrtcType::UM6845R) {
            // Type 1: R7 made equal to the current row starts a VSYNC
            // straight away, unless one is in progress (16.3, 16.4.2). On
            // an even frame with an interlace mode set it waits for the
            // middle of a line like any other (19.7.2; Shaker "VSYNC IVM
            // story", R7 = 0 written on the frame's second line: 96
            // microseconds from the frame's start on a real chip, not 80).
            const bool match = vcc_ == reg_[7];
            if (match && old != reg_[7] && !vsync_) {
                if (interlace() && !parityFrame_)
                    midVsync_ = true;
                else
                    startVsync();
            }
            r7Match_ = match;
        } else if (!asic() && old != reg_[7] && vcc_ == reg_[7] && !vsync_) {
            // Making R7 equal to the current row starts a VSYNC straight
            // away. Not on the ASICs, which only look at R7 as a row
            // begins (16.4.4). Type 2 keeps it to itself when the write
            // comes during an HSYNC (16.4.3; Shaker "VSYNC conditions" on
            // a real chip: 14 characters out of 64 with R3 = 14). And on
            // an even frame with an interlace mode set it waits for the
            // middle of the line like any other (19.7.2; Shaker "VSYNC IVM
            // story", R7 = 0 written on character 16 of a frame's second
            // line: 96 microseconds from the frame's start on a real chip,
            // not 80).
            if (type_ == CrtcType::MC6845 && interlace() && !parityFrame_) {
                midVsync_ = true;
            } else {
                startVsync();
                ghostVsync_ = type_ == CrtcType::MC6845 && hsync_;
            }
        }
        // R7 moved away from C4 lets the next meeting count again (16.3).
        if (type_ == CrtcType::MC6845)
            r7Match_ = vcc_ == reg_[7];
        break;
    case 12:
    case 13:
        // Type 2 takes R12/R13 as the character on which C0 met R1 ends:
        // written during that very character they are still in time, a
        // character later they wait a frame (20.3.3; Shaker "CRTC 2
        // offset" on a real chip).
        if (type_ == CrtcType::MC6845 && hcc_ == reg_[1] && hcc_ != 0) {
            if (held2_ == Held2::Start)
                heldAddress2_ = startAddress();
            else if (veryLastLine2())
                maRow_ = startAddress();
        }
        break;
    }

    // On the last character of a line the C4 step follows the registers to
    // the end: R9 brought to C9 there still counts.
    if (type_ == CrtcType::HD6845S && hcc_ == reg_[0]) {
        if (c9AtR9())
            c9MatchAtEnd_ = true;
        // R0 brought to the character in progress makes it the last one.
        if (selected_ == 0)
            rowR4AtEnd_ = vcc_ == reg_[4];
    }
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
    r6Due_ = R6Write::None;
    if (splitDue_ != 0) {
        splitLine_ = splitDueLine_;
        splitAddress_ = splitDueAddress_;
        splitDue_ = 0;
    }
    late_ = false;
    // What a snapshot does not hold is set to what an undisturbed frame
    // would have at this place.
    hDisp_ = hcc_ < reg_[1];
    vDisp_ = vcc_ < reg_[6];
    dispHistory_ = hDisp_ && vDisp_ ? 0x0F : 0;
    maRow_ = static_cast<uint16_t>((startAddress() + vcc_ * reg_[1]) & 0x3FFF);
    ma_ = static_cast<uint16_t>((maRow_ + hcc_) & 0x3FFF);
    inAdjust_ = false;
    vtac_ = 0;
    lastLine1_ = hcc_ == reg_[0] && vcc_ == reg_[4] && vlc_ == reg_[9];
    overran1_ = parityInTest_ = false;
    r8Due_ = 0;
    fromR12_ = vcc_ == 0;
    extraLine_ = interlaceLine_ = midVsync_ = lateVsync_ = false;
    latchC9();
    lastLine_ = onLastLine();
    adjust_ = lastLine_ && hcc_ < 3;
    adjustRunning_ = adjustUndecided_ = false;
    c9Enabled_ = hcc_ >= 1;
    vsyncAllowed_ = hcc_ >= 2;
    c4CountArmed_ = c9AtR9();
    c9MatchAtEnd_ = false;
    rowR4AtEnd_ = vcc_ == reg_[4];
    endSeenOnOne_ = false;
    r7Match_ = vcc_ == reg_[7];
    vsyncFresh_ = vsyncDue_ = false;
    if (type_ == CrtcType::MC6845) {
        lastLine_ = vcc_ == reg_[4] && vlc_ == reg_[9];
        lastLineOpen_ = vcc_ != 0 || vlc_ != 0;
        previousLast_ = hsyncJudged_ = false;
        r9AtStart_ = reg_[9];
        newFrame2_ = firstLine2_ = false;
        held2_ = Held2::Nothing;
        // The display's own line counter, as a row counted from its start
        // has it.
        const uint8_t half = reg_[9] / 2;
        c9Ivm_ = vlc_ > half ? static_cast<uint8_t>(vlc_ - half - 1) : vlc_;
        latchC9Of2();
    }
    ghostVsync_ = false;
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
        // Only three address bits are decoded. Where registers 10 and 11
        // would be, the ASICs answer with their statuses.
        static constexpr uint8_t map[8] = {16, 17, 10, 11, 12, 13, 14, 15};
        const uint8_t which = map[selected_ & 7];
        if (which == 10)
            return status1();
        if (which == 11)
            return status2();
        return reg_[which];
    }
    }
}

// The two status bytes of types 3 and 4 (Compendium 21.3.4). Each bit tells
// of one meeting of a counter and a register, for as long as it lasts: a
// character for most, so that a program has to read at the right
// microsecond. Several look one character ahead.
uint8_t Crtc::status1() const
{
    const bool lineEnds = hcc_ == reg_[0];
    uint8_t status = 0xFE;
    if (lineEnds)
        status |= 0x01;
    if (hcc_ == reg_[0] / 2)
        status &= ~0x02;
    // The display ends with the next character.
    if (reg_[0] >= reg_[1] && static_cast<uint8_t>(hcc_ + 1) == reg_[1])
        status &= ~0x04;
    if (hcc_ == reg_[2])
        status &= ~0x08;
    // The character after the HSYNC's last one (a width of 0 is 16).
    const uint8_t width = reg_[3] & 0x0F ? reg_[3] & 0x0F : 16;
    if (hcc_ == static_cast<uint8_t>(reg_[2] + width))
        status &= ~0x10;
    // The VSYNC's lines, counted from 1, against R3's high half: the last
    // line of a VSYNC. With 0 there it is the sixteenth, and every line
    // outside the VSYNC as well, the count being 0 then.
    if ((vsync_ ? (vsc_ + 1) & 0x0F : 0) == reg_[3] >> 4)
        status &= ~0x20;
    // The address the next character will have ends in &00.
    uint16_t next = static_cast<uint16_t>(ma_ + 1);
    if (lineEnds && !frameEndsAsic())
        next = maRow_;
    else if (lineEnds)
        next = splitLine_ != 0 && asicLine() == splitLine_ ? splitAddress_ : startAddress();
    if ((next & 0xFF) == 0)
        status &= ~0x80;
    return status;
}

uint8_t Crtc::status2() const
{
    const bool lineEnds = hcc_ == reg_[0];
    const bool rowEnds = vlc_ == reg_[9];
    uint8_t status = 0x3F;
    // The last character of the last row, of the rows shown, and of the
    // row before the VSYNC. The lines of R5 are no row's end: with R4 = 37
    // and R5 = 8 the first is seen once a frame, eight lines before the
    // frame ends (the Shaker's "CRTC 3/4 status" on a real machine).
    if (lineEnds && rowEnds && !inAdjust_) {
        if (vcc_ == reg_[4])
            status &= ~0x01;
        if (vcc_ == static_cast<uint8_t>(reg_[6] - 1))
            status &= ~0x02;
        if (vcc_ == static_cast<uint8_t>(reg_[7] - 1))
            status &= ~0x04;
    }
    // A bit that turns over every sixteen frames.
    if (!(frames_ & 0x10))
        status &= ~0x08;
    // The lines of R5 as counted once the one in progress has ended,
    // against R5: the last character of the last of them. The count is 0
    // outside them, so that with R5 = 0 the bit is always set. (The same
    // test finds R5 that way: it counts the lines after the last row whose
    // last character leaves the bit at 0.)
    if ((inAdjust_ ? (vlc_ + lineEnds) & 0x1F : 0) != reg_[5])
        status &= ~0x10;
    if (rowEnds)
        status &= ~0x20;
    // The next character is on a row's first line.
    if (lineEnds ? rowEnds : vlc_ == 0)
        status |= 0x80;
    return status;
}

// DISPTMG for the two halves of the character in progress (bit 0, bit 1),
// before R8's skew. On types 0 and 2 a line that ends without C0 having met
// R1 still turns the display off, for the second half of its last character
// (17.6.2). This follows R0 to the end of the character: a line made longer
// in time loses that half character of border (Shaker A7).
uint8_t Crtc::dispNow() const
{
    if (!hDisp_ || !vDisp_)
        return 0;
    // First line of a frame with R6 = 0, types 0 and 2: each character is
    // border for half of its time and shown for the other (18.3.2). On a
    // type 0's screen the border comes first (Shaker AP, the patchwork of
    // R6 = 0/8, measured on a photograph: shown from 10.5 to 11, from 11.5
    // to 12... for a write on character 9). The photograph of a type 2
    // does not settle which half it is there.
    if (r6Conflict_)
        return type_ == CrtcType::HD6845S ? 2 : 1;
    const bool lateBorder = hcc_ == reg_[0] && (type_ == CrtcType::HD6845S || type_ == CrtcType::MC6845);
    return lateBorder ? 1 : 3;
}

bool Crtc::displayEnable(int half) const
{
    const bool now = (dispNow() >> (half ? 1 : 0)) & 1;
    switch (type_) {
    case CrtcType::UM6845R:
        // R6 = 0 blanks the picture at once.
        return now && reg_[6] != 0;
    case CrtcType::MC6845:
        return now;
    default:
        break;
    }
    // Where R8 has skew bits (19.2), they send the signal out through one
    // or two latches clocked once per character, or not at all. Changing
    // them changes at once which of the three is sent, so that the border
    // between two lines can be moved, doubled or made to vanish; a write
    // counts for the whole of the character it is made during (Shaker A2,
    // "R8 stories").
    switch (reg_[8] & 0x30) {
    case 0x00: return now;
    case 0x10: return (dispHistory_ >> 1) & 1;
    case 0x20: return (dispHistory_ >> 3) & 1;
    default: return false;
    }
}

void Crtc::tick()
{
    previousHsyncCut_ = hsyncCut_;
    hsyncCut_ = HsyncCut::None;
    dispHistory_ = static_cast<uint8_t>((dispHistory_ << 2 | dispNow()) & 0x0F);
    if (late_) [[unlikely]]
        settleLate();

    const bool type0 = type_ == CrtcType::HD6845S;
    const bool type1 = type_ == CrtcType::UM6845R;
    // Type 1 keeps the address for the next row as the character on which
    // C0 met R1 ends, not as it begins: R1 moved away during that very
    // character, and nothing is kept (17.4.2, Shaker "R1 stories": R1
    // raised above R0 on C0 = R1, on a real chip).
    if (type1 && hcc_ == reg_[1] && keepsAddress1())
        maRow_ = ma_;
    const bool newLine = hcc_ == reg_[0];
    // Type 1: the frame's end was noted as this character began, and R0
    // has been moved away since: the line goes on (13.7.1.2).
    if (type1 && lastLine1_ && !newLine)
        overran1_ = true;
    if (newLine) {
        // A first line that ends with R6 still 0 leaves the border for the
        // rest of the frame, as one whose C0 meets R1 does. With R0 = 0
        // every character is such an end (Shaker BU, "R0 = 0 after add
        // line").
        if (r6Conflict_) {
            r6Conflict_ = false;
            vDisp_ = false;
        }
        const bool oneCharacter = hcc_ == 0;
        hcc_ = 0;
        if (type0)
            lineStart0();
        else if (asic())
            endOfLineAsic();
        else if (type_ == CrtcType::MC6845)
            endOfLine2();
        else
            endOfLine1(oneCharacter);
    } else {
        ++hcc_;
    }
    // Type 1 notes, as a line's last character begins, whether the frame
    // ends with it (11.2.4).
    if (type1 && hcc_ == reg_[0] && vcc_ == reg_[4] && rowEnds1())
        lastLine1_ = true;

    if (type0) {
        // R7 was made equal to C4 on the character before this one, the
        // last of its line then.
        if (vsyncDue_) {
            vsyncDue_ = false;
            if (!vsync_ && vcc_ == reg_[7]) {
                startVsync();
                vsyncFresh_ = hcc_ != 0;
            }
        }
        if (hcc_ == 2 && endSeenOnOne_ && c9MatchAtEnd_) {
            // R0 was 1 and the line was ending on character 1, on the last
            // line of a row, when R0 was raised: the line goes on, but the
            // C4 step it had armed is taken here, C9 and C0 staying as they
            // are (13.7.2). On a frame's last line that leaves the chip
            // adding lines: C9 runs on, up to 31 with R5 = 0, before the
            // frame ends (13.7.2.2; Shaker "Bug OVF C4" on a real chip).
            c9MatchAtEnd_ = false;
            vcc_ = (vcc_ + 1) & 0x7F;
            rowChanged0();
        }
        if (hcc_ == 1)
            endSeenOnOne_ = reg_[0] == 1;
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
            } else if (!adjust_ && onLastLine()) {
                // And one that makes it is still in time: the frame ends
                // with this line (12.2; Shaker E, "switch comparator", R9
                // brought to C9 on character 1 of row R4, on a real chip).
                lastLine_ = adjust_ = true;
                adjustRunning_ = false;
                adjustUndecided_ = true;
            }
            break;
        case 3:
            decideAdjustment0();
            break;
        }
        // The C4 step is decided on the last character and cannot be taken
        // back: not by R9 moving away during that character (10.3.1.2), nor
        // by R0 changing so that the line carries on.
        if (hcc_ == reg_[0]) {
            if (c9AtR9())
                c9MatchAtEnd_ = true;
            rowR4AtEnd_ = vcc_ == reg_[4];
        }
    }
    // The VSYNC of an even frame, held back to the middle of the line.
    if (midVsync_ && hcc_ == reg_[0] / 2) {
        midVsync_ = false;
        // (Type 1 still wants C4 at R7 there.)
        if (!vsync_ && (!type1 || vcc_ == reg_[7])) {
            startVsync();
            vsyncFresh_ = true;
        }
    }

    if (hcc_ == 0) {
        // C0 back at 0 for having run past 255, not for having met R0, does
        // not turn the display back on (17.1, seen on CRTC 0). Nor does
        // type 2 turn it back on with an HSYNC on that character: the
        // border stays for the whole line (15.5.2, 15.6).
        bool hsyncHere = false;
        if (type_ == CrtcType::MC6845) {
            const bool goesOn = hsync_ && ((hsc_ + 1) & 0x0F) != (reg_[3] & 0x0F);
            hsyncHere = goesOn || reg_[2] == 0;
        }
        if ((newLine || !type0) && !hsyncHere)
            hDisp_ = true;
        // On its first row the UM6845R takes every line's address straight
        // from R12/R13, which can therefore be changed from one line to the
        // next there. The address kept for the next row is another thing:
        // it only moves when C0 meets R1 on a row's last line, so a frame
        // that never does shows R12/R13 on its first row and whatever was
        // kept last on all the others (17.4.2, Shaker "R1 stories").
        ma_ = type1 && fromR12_ ? startAddress() : maRow_;
    } else {
        ma_ = (ma_ + 1) & 0x3FFF;
    }

    if (hcc_ == reg_[1]) {
        const bool type2 = type_ == CrtcType::MC6845;
        if (type2)
            keepAddress2();
        displayEnds(type0 || asic());
    }

    // On type 0 the character an HSYNC ends on cannot start the next one:
    // two pulses are never joined (15.3.1; with R0 = 0, R2 = 0 and R3 = 1
    // there is one every second character, 15.3.2).
    bool hsyncEnded = false;
    bool joined = false;
    if (hsync_) {
        hsc_ = (hsc_ + 1) & 0x0F;
        if (hsc_ == (reg_[3] & 0x0F)) {
            hsync_ = false;
            hsyncEnded = type0;
            joined = true;
            if (type_ == CrtcType::MC6845)
                hsyncEnds2();
        }
    }
    if (hcc_ == reg_[2] && !hsync_ && !hsyncEnded) {
        // A width of 0 means no HSYNC at all on types 0 and 1, and 16
        // characters on the others.
        const bool none = (reg_[3] & 0x0F) == 0
                          && (type_ == CrtcType::HD6845S || type_ == CrtcType::UM6845R);
        if (!none) {
            hsync_ = true;
            hsc_ = 0;
            hsyncJudged_ = false;
            // With R0 = 0 the counter never leaves R2 and there is nothing
            // new about the pulse that follows (Kevin Thacker's "dmatest":
            // the sound channels run once, then no more).
            hsyncJoined_ = joined && reg_[0] != 0;
        }
    }
    // Type 2 takes its first look at the line once it knows whether an
    // HSYNC lies on the first character.
    if (newLine && type_ == CrtcType::MC6845)
        lineStart2();
}

// What was written a moment ago and only counts from this character on:
// R6 on type 0, the split on the ASICs.
void Crtc::settleLate()
{
    if (r6Due_ != R6Write::None) {
        r6Written(r6Due_);
        r6Due_ = R6Write::None;
    }
    if (splitDue_ != 0 && --splitDue_ == 0) {
        splitLine_ = splitDueLine_;
        splitAddress_ = splitDueAddress_;
    }
    if (r8Due_ != 0) {
        // Type 1, a microsecond after "interlace sync & video" came on or
        // went off (19.5.3). Coming on in an even frame, the lines take
        // the parity of the row (even, unless R9 is even and the row odd);
        // and the frame becomes even, unless it was odd and the lines too.
        // Going off, the frame takes the parity of the lines.
        if (r8Due_ == 1) {
            const bool oddRow = (vcc_ & 1) && !(reg_[9] & 1);
            if (!parityFrame_) {
                parityC9_ = oddRow;
                vlc_ = static_cast<uint8_t>((vlc_ & 0x1E) | parityC9_);
            }
            parityFrame_ = parityFrame_ && parityC9_ != oddRow;
        } else {
            parityFrame_ = parityC9_;
        }
        r8Due_ = 0;
    }
    late_ = splitDue_ != 0;
}

// What a write to R6 does to the picture, on types 0 and 2.
void Crtc::r6Written(R6Write effect)
{
    switch (effect) {
    case R6Write::ConflictOn: r6Conflict_ = vDisp_; break;
    case R6Write::ConflictOff: r6Conflict_ = false; break;
    case R6Write::Border: vDisp_ = false; break;
    case R6Write::None: break;
    }
}

// C0 has met R1: the border from here to the end of the line.
void Crtc::displayEnds(bool keepAddress)
{
    hDisp_ = false;
    // R6 still 0 where the first line's display ends: the border stays
    // (18.3.2).
    if (r6Conflict_) {
        r6Conflict_ = false;
        vDisp_ = false;
    }
    // The address reached at the end of a row's last line becomes the
    // start of the next row. On the ASICs the lines added after the
    // last row keep its address (11.2.6).
    if (keepAddress && c9AtR9() && !(asic() && (inAdjust_ || interlaceLine_)))
        maRow_ = ma_;
    // The Plus's split screen: after this line the picture comes from
    // another address.
    if (splitLine_ != 0 && asic() && asicLine() == splitLine_) [[unlikely]]
        maRow_ = splitAddress_;
}

// ---- CRTC 1 ----------------------------------------------------------------

// The end of a line on the UM6845R. It counts in the plainest way, by what
// its registers hold as the line ends (Compendium 10.3.2, 12.3): C9 up to
// R9, then C4 up to R4, then a new frame. The lines of R5 are another
// matter (11.2.3, 11.2.4, 11.3.2). They follow a frame whose end was noted
// as its last line's last character began, whatever R4 and R9 have become
// during that character; C5 counts them while C9 and C4 go on counting,
// C4 without a look at R4; and R5 brought to 0 meanwhile does not end
// them: C5 goes round, ready to end the frame on any line where R5 is
// given its number, while C4 is compared with R4 again, which ends the
// frame the ordinary way.
void Crtc::endOfLine1(bool oneCharacter)
{
    // The VSYNC lasts 16 lines whatever R3 says. A line of a single
    // character (R0 = 0) does not count as one: C9 and C4 go on, but a VSYNC
    // begun there is still up when lines get longer again, where the MC6845
    // has long finished it (Shaker "VSYNC conditions", first screen, on real
    // chips of both kinds).
    if (vsync_ && !oneCharacter) {
        vsc_ = (vsc_ + 1) & 0x0F;
        if (vsc_ == 0)
            vsync_ = false;
    }

    // C9 goes up one line at a time; in "interlace sync & video" two, from
    // the parity of the lines, and a row ends when it has R9 but for its
    // lowest bit (19.8.2).
    const bool video = interlaceVideo();
    const bool rowEnds = rowEnds1();
    const bool lastRow = vcc_ == reg_[4];
    const uint8_t nextLine = static_cast<uint8_t>((vlc_ + (video ? 2 : 1)) & 0x1F);
    const bool noted = lastLine1_;
    const bool overran = overran1_;
    lastLine1_ = overran1_ = false;
    // The frame's end was noted, R0 was raised on that very character so
    // that the line went on, and R4 or R9 has since been moved away: no
    // frame ends here, but the address logic has been told one did. From
    // the next line on every line starts at R12/R13, with the parity of
    // the lines in the row-end test, as after a write to R5 at the wrong
    // moment (13.7.1.2; Shaker "RFD round 2").
    if (noted && overran && reg_[5] == 0 && !(rowEnds && lastRow)) {
        fromR12_ = true;
        parityInTest_ = true;
    }
    // Each line starts at R12/R13 until a row ends; not the frame's last
    // row, so that with R4 = 0 the first row of the lines of R5 does too
    // (11.2.4, 17.4.2; Shaker "UPD OFF ADD LINE" on a real chip).
    // (Once the test has been met, the lines' parity has no more say in
    // it: in "RFD round 2" the parity is made even right after, and the
    // rows that follow are not repeated on a real chip.)
    if (keepsAddress1() && !lastRow)
        fromR12_ = parityInTest_ = false;
    // An even frame gets one line more with an interlace mode set, counted
    // as if R5 were one more (11.2.3, 19.6.2).
    const uint8_t lines = static_cast<uint8_t>((reg_[5] + (interlace() && !parityFrame_ ? 1 : 0)) & 0x1F);
    const bool anyLines = reg_[5] != 0 || (interlace() && !parityFrame_);

    if (inAdjust_) {
        const uint8_t next = (vtac_ + 1) & 0x1F;
        if (anyLines && next == lines) {
            inAdjust_ = false;
            newFrame1();
            return;
        }
        vtac_ = next;
        if (!rowEnds) {
            vlc_ = nextLine;
        } else if (!anyLines && lastRow) {
            // (That the lines of R5 are over with it is what the Shaker's
            // module E needs: each of its measurements leaves R5 at 0 in
            // the middle of them, and a real chip is found counting whole
            // frames again a few frames later.)
            inAdjust_ = false;
            newFrame1();
        } else {
            vcc_ = (vcc_ + 1) & 0x7F;
            nextRow1();
        }
        return;
    }
    if (noted && anyLines) {
        // The first of the lines of R5: counted by R4 and R9 as they are
        // now.
        inAdjust_ = true;
        vtac_ = 0;
        if (rowEnds) {
            vcc_ = (vcc_ + 1) & 0x7F;
            nextRow1();
        } else {
            vlc_ = nextLine;
        }
        return;
    }
    if (!rowEnds) {
        vlc_ = nextLine;
    } else if (lastRow) {
        newFrame1();
    } else {
        vcc_ = (vcc_ + 1) & 0x7F;
        nextRow1();
    }
}

// Has C9 reached the end of its row, as type 1 sees it? In "interlace sync
// & video" the lowest bit, which is the parity of the lines, is left out,
// after C9 has been moved up one if R9 is even (19.8.2).
bool Crtc::rowEnds1() const
{
    if (!interlaceVideo())
        return vlc_ == reg_[9];
    return ((vlc_ + (~reg_[9] & 1)) & 0x1E) == (reg_[9] & 0x1E);
}

// The test type 1 makes when C0 meets R1, to keep the address for the next
// row, and at the end of the line, to stop starting its lines at R12/R13:
// is this the row's last line? After a "rupture for dummies" the parity of
// the lines has its say as well, and on even lines the test is never met:
// nothing is kept and every line starts at R12/R13, one frame in two since
// the parity turns over with each frame, or every frame once the parity
// has been pinned by switching "interlace sync & video" on and off (11.6.1,
// 11.6.2; Shaker "RFD round 2", where R9 is 14 when the row ends: it is
// not the parity of R9 that counts).
bool Crtc::keepsAddress1() const
{
    if (!rowEnds1())
        return false;
    return !parityInTest_ || interlaceVideo() || parityC9_;
}

// A new row on type 1. In "interlace sync & video" its first line has the
// parity of the lines, which turns over with each row when R9 is even: rows
// of even lines and rows of odd lines then follow one another (19.5.3).
void Crtc::nextRow1()
{
    if (interlaceVideo()) {
        if (!(reg_[9] & 1))
            parityC9_ = !parityC9_;
        vlc_ = parityC9_;
    } else {
        vlc_ = 0;
    }
    startRow();
}

// A new frame on type 1. The parity of the frame turns over, whatever R8
// holds, and the lines take it (19.5.3).
void Crtc::newFrame1()
{
    parityFrame_ = !parityFrame_;
    parityC9_ = parityFrame_;
    parityInTest_ = false;
    vlc_ = interlaceVideo() ? parityC9_ : 0;
    vcc_ = 0;
    vDisp_ = true;
    fromR12_ = true;
    startRow();
}

void Crtc::startRow()
{
    // On type 2 a frame that starts with R6 = 0 is not border at once
    // (18.3.2).
    if (vcc_ == reg_[6]) {
        if (type_ == CrtcType::MC6845 && vcc_ == 0)
            r6Conflict_ = vDisp_;
        else
            vDisp_ = false;
    }
    // Types 1 and 2 (the only ones that come this way) do not start a VSYNC
    // twice from the same meeting of C4 and R7: with R4 = R7 = 0, C4 never
    // leaves R7 and there is one VSYNC, not one after the other without end
    // (16.3; Shaker "VSYNC torture": 16 lines on real chips of both types,
    // where types 3 and 4 never come out of it).
    const bool match = vcc_ == reg_[7];
    const bool fresh = match && !r7Match_ && !vsync_;
    r7Match_ = match;
    if (fresh && interlace() && !parityFrame_) {
        // An even frame with an interlace mode set: the VSYNC waits for the
        // middle of the line (19.7.2).
        midVsync_ = true;
    } else if (fresh) {
        startVsync();
        // Type 2: an HSYNC that reaches the line's last character hides the
        // VSYNC that the next line's first one brings (15.6). This is asked
        // before the line's own HSYNC is: one that starts on the first
        // character hides nothing.
        ghostVsync_ = type_ == CrtcType::MC6845 && hsync_;
    }
    // Type 2 settles there, as type 0 does, the parity the next frame will
    // have, whatever R8 holds: a frame whose C4 never meets R6 leaves it
    // where it was (19.5.4).
    if (type_ == CrtcType::MC6845 && vcc_ == reg_[6])
        parityR6_ = !parityFrame_;
}

// A new frame on type 2. R12/R13 are not looked at here: the chip took them
// where C0 met R1 on the frame's last line, or did not take them at all
// (keepAddress2).
void Crtc::startFrame()
{
    inAdjust_ = false;
    vtac_ = 0;
    vlc_ = 0;
    c9Ivm_ = 0;
    vcc_ = 0;
    vDisp_ = true;
    newFrame2_ = true;
    // The first line of a frame that did not come after the line an
    // interlace mode adds: see endOfLine2.
    firstLine2_ = !interlaceLine_;
    interlaceLine_ = false;
    parityFrame_ = parityR6_;
    startRow();
}

void Crtc::startVsync()
{
    vsync_ = true;
    vsc_ = 0;
    ghostVsync_ = false;
}

// ---- CRTC 2 ----------------------------------------------------------------

// C0 at R1 on the MC6845: where the address for the next row is kept. On a
// frame's very last line it is R12/R13 that are kept, and this is the only
// place where the chip takes them: written after it, they wait a frame, and
// with R1 above R0 they are never taken, every line showing the address kept
// last (17.4.3, 20.3.3; Shaker "R1 stories" and "CRTC 2 offset"). The very
// last line is the one found to be the last when no lines of R5 follow (by
// its state, not by C4 and C9: R9 moved away since changes nothing, 12.4.2
// note 1), and the last of the lines of R5 when they do: R12 written on
// the last line after C0 = R1, with lines of R5 to come, is in time (Shaker
// "R5 stories", whose second picture starts where it should on a real
// chip). On any other line it is, as elsewhere, the address reached at the
// end of a row's last line.
//
// What an interlace mode has a say in is only settled as the line ends
// (settleAddress2): here the address, or R12/R13, are set aside for it.
void Crtc::keepAddress2()
{
    if (hcc_ == 0) {
        // R1 = 0: the line's first character still goes by what the line
        // before was. So it is a new frame's first line that takes R12/R13,
        // and shows them at once; but only half of the loading is done
        // there: the bits that are 0 in R12/R13 are cleared in the address
        // kept, the others stay as they were.
        if (newFrame2_) {
            maRow_ &= startAddress();
            ma_ = maRow_;
        }
        return;
    }
    if (interlaceLine_) {
        held2_ = Held2::Start;
        heldAddress2_ = startAddress();
    } else if (veryLastLine2()) {
        held2_ = Held2::Nothing;
        maRow_ = startAddress();
    } else if (!interlaceVideo() && vlc_ == reg_[9]) {
        held2_ = Held2::Nothing;
        maRow_ = ma_;
    } else {
        // Out of the mode here, only the mode set by the line's end can
        // still have this address kept: R9 brought to C9 after C0 = R1
        // comes too late, as ever.
        held2_ = interlaceVideo() ? Held2::Address : Held2::ForMode;
        heldAddress2_ = ma_;
    }
}

// The end of a line, for what was set aside where C0 met R1. On the line an
// interlace mode added, R12/R13 only if the frame does end there. Elsewhere
// it is the mode as the line ends that counts, not as it was at C0 = R1: in
// "interlace sync & video" the address is kept when the display's own
// counter is at its end, and out of that mode when C9 is at R9. (Shaker
// "Stranger thing interlace", which sets and leaves the mode on every line,
// a little later each time: on a real chip the address moves on half-way
// down a row on the one line where the mode is set as the line ends, though
// it was not at C0 = R1, and does not on the one where it was set at C0 = R1
// and left since.)
void Crtc::settleAddress2()
{
    const Held2 held = held2_;
    held2_ = Held2::Nothing;
    if (held == Held2::Start) {
        if (interlace())
            maRow_ = heldAddress2_;
    } else if (held == Held2::Address || (held == Held2::ForMode && interlaceVideo())) {
        if (rowEnds2())
            maRow_ = heldAddress2_;
    }
}

// Whether the address reached on this line is kept for the lines that
// follow. Outside "interlace sync & video" that is on a row's last line.
// In that mode it goes by the display's own counter, which starts again
// half-way down each row: the address moves on twice a row when R9 is odd,
// and once only, half-way down, when R9 is even (19.4.3, 19.8.3).
bool Crtc::rowEnds2() const
{
    if (!interlaceVideo())
        return vlc_ == reg_[9];
    return ((c9Ivm_ << 1) & 0x1E) == (reg_[9] & 0x1E);
}

// Whether a new frame follows this line, as things stand.
bool Crtc::veryLastLine2() const
{
    // The line an interlace mode adds is the last of all, if the mode is
    // still set as it ends: that is settled there.
    if (interlaceLine_)
        return false;
    const bool ends = inAdjust_ ? ((vtac_ + 1) & 0x1F) == reg_[5] : lastLine_ && reg_[5] == 0;
    return ends && !(interlace() && parityR6_);
}

// What type 2 puts out as the line within the row: C9, or in "interlace
// sync & video" the display's own counter doubled, with the frame's parity
// as its lowest bit (19.5.4, 19.8.3).
void Crtc::latchC9Of2()
{
    c9Out_ = interlaceVideo() ? static_cast<uint8_t>((c9Ivm_ << 1 | parityFrame_) & 0x1F) : vlc_;
}

// The end of a line on the MC6845 (Compendium 10.3.3, 11.2.5, 12.4.1). A
// line found to be the frame's last is followed by a new frame, or by the
// lines of R5 first, whatever R4 and R9 have become since. Any other line
// counts in the plainest way, and so do the lines of R5: C9 up to R9, then
// C4 one further, R4 not being asked.
void Crtc::endOfLine2()
{
    // The VSYNC lasts 16 lines whatever R3 says.
    if (vsync_) {
        vsc_ = (vsc_ + 1) & 0x0F;
        if (vsc_ == 0)
            vsync_ = false;
    }
    // An HSYNC that runs into the next line is asked its question here, on
    // the line's last character.
    if (hsync_)
        hsyncEnds2();

    settleAddress2();

    const bool first = firstLine2_;
    firstLine2_ = false;
    if (interlaceLine_) {
        // The line an interlace mode added. The mode left during it, the
        // frame does not end there: C9 and C4 count on, C4 past R4 (19.6.3).
        if (interlace()) {
            startFrame();
        } else {
            interlaceLine_ = false;
            countLine2();
        }
    } else if (inAdjust_) {
        vtac_ = (vtac_ + 1) & 0x1F;
        if (vtac_ == reg_[5])
            endFrame2();
        else
            countLine2();
    } else if (lastLine_) {
        if (reg_[5] != 0) {
            inAdjust_ = true;
            vtac_ = 0;
            countLine2();
        } else {
            endFrame2();
        }
    } else if (first && interlace() && parityR6_) {
        // An interlace mode set during the first line of a frame that is
        // to be followed by an odd one, and that began without the added
        // line: the chip takes this line for it, and the frame begins
        // again (19.6.3).
        interlaceLine_ = true;
        startFrame();
    } else {
        countLine2();
    }
    latchC9Of2();
}

// The frame has run its lines, those of R5 included. With an interlace mode
// set, one that is to be followed by an odd frame gets one more line, on
// which C9 and C4 count as on any other (19.6.3).
void Crtc::endFrame2()
{
    if (!interlace() || !parityR6_) {
        startFrame();
        return;
    }
    inAdjust_ = false;
    interlaceLine_ = true;
    countLine2();
}

// C9 and the display's own counter, which starts again half-way down the
// row as well as at its end. It is kept all the time, to be put out as soon
// as "interlace sync & video" is set (19.8.3).
void Crtc::countLine2()
{
    if (vlc_ == reg_[9]) {
        vlc_ = 0;
        c9Ivm_ = 0;
        vcc_ = (vcc_ + 1) & 0x7F;
        startRow();
    } else {
        // The display's counter starts again when C9 is half-way down the
        // row, and whenever it has itself got to R9 (R9's lowest bit left
        // out, the counter being put out doubled): while C9 runs past R9
        // it goes round and round, and the address moves on each time
        // (19.8.3; Shaker "Interlace C4/C9 counters", R9 lowered below C9
        // as the mode is set).
        const bool again = vlc_ == reg_[9] / 2 || ((c9Ivm_ << 1) & 0x1E) == (reg_[9] & 0x1E);
        c9Ivm_ = again ? 0 : (c9Ivm_ + 1) & 0x0F;
        vlc_ = (vlc_ + 1) & 0x1F;
    }
}

void Crtc::lineStart2()
{
    r9AtStart_ = reg_[9];
    newFrame2_ = false;
    judgeLineStart2();
}

// The look the chip takes on a line's first character: R4 as it is now, R9
// as it was when the line began. The frame's end found there makes the line
// the last one, unless the HSYNC before found it too, or an HSYNC lies on
// this character (15.6). On a frame's first line, writes to R4 and R9 are
// not looked at until the HSYNC says otherwise.
void Crtc::judgeLineStart2()
{
    const bool atEnd = !inAdjust_ && !interlaceLine_ && vcc_ == reg_[4] && vlc_ == r9AtStart_;
    lastLine_ = atEnd && !previousLast_ && !hsync_;
    lastLineOpen_ = vcc_ != 0 || vlc_ != 0;
}

// R4 or R9 written. On the first character R4 still has its say in the look
// just taken, either way. Anywhere else a write can only make the line the
// frame's last, never unmake it, and it is not heard during an HSYNC.
void Crtc::frameEndWritten2(bool r4)
{
    if (inAdjust_)
        return;
    if (hcc_ == 0 && r4) {
        judgeLineStart2();
        return;
    }
    if (!lastLine_ && lastLineOpen_ && !hsync_ && vcc_ == reg_[4] && vlc_ == reg_[9])
        lastLine_ = true;
}

// The last character of an HSYNC: C4 and C9 at the frame's end there keep
// the next line from being found the last as it begins; anything else lets
// writes to R4 and R9 be looked at again.
void Crtc::hsyncEnds2()
{
    if (hsyncJudged_)
        return;
    hsyncJudged_ = true;
    if (vcc_ == reg_[4] && vlc_ == reg_[9]) {
        previousLast_ = true;
    } else {
        previousLast_ = false;
        lastLineOpen_ = true;
    }
}

// ---- CRTC 3 and 4 ----------------------------------------------------------

// The 6845 of the ASICs counts lines in the plainest way of the five
// (Compendium 10.3.4, 11.2.6, 12.5, 16.4.4, 19.8.4): a row ends when C9 has
// reached R9 or gone past it, the lines of the vertical adjustment and of
// the interlace are added to the last row without C4 moving, and R7 is only
// looked at as a row begins.
void Crtc::endOfLineAsic()
{
    ++frameLine_;
    if (vsync_) {
        if (vsyncFresh_) {
            // Started in the middle of the line: the count begins here.
            vsyncFresh_ = false;
            vsc_ = 0;
        } else {
            vsc_ = (vsc_ + 1) & 0x0F;
            if (vsc_ == reg_[3] >> 4)
                vsync_ = false;
        }
    }
    if (lateVsync_) {
        lateVsync_ = false;
        if (!vsync_)
            startVsync();
    }

    if (interlaceLine_) {
        newFrameAsic();
    } else if (inAdjust_) {
        // The adjustment ends when the next line would be number R5, or
        // further if R5 was brought down meanwhile.
        if (((vlc_ + 1) & 0x1F) >= reg_[5])
            endFrameAsic();
        else
            vlc_ = (vlc_ + 1) & 0x1F;
    } else if (vlc_ >= reg_[9]) {
        if (vcc_ == reg_[4]) {
            if (reg_[5] != 0) {
                inAdjust_ = true;
                vlc_ = 0;
            } else {
                endFrameAsic();
            }
        } else {
            vcc_ = (vcc_ + 1) & 0x7F;
            // With an odd number of lines to share between two frames, rows
            // of even lines and rows of odd lines follow each other.
            if (reg_[9] & 1)
                parityC9_ = !parityC9_;
            vlc_ = interlaceVideo() ? parityC9_ : 0;
            rowStartAsic(parityFrame_);
        }
    } else if (interlaceVideo()) {
        vlc_ = ((vlc_ + 2) | (parityC9_ ? 1 : 0)) & 0x1F;
    } else {
        vlc_ = (vlc_ + 1) & 0x1F;
    }
}

// Whether a new frame follows the line in progress, as things stand: what
// the end of the line will find, for the status that looks ahead.
bool Crtc::frameEndsAsic() const
{
    if (interlaceLine_)
        return true;
    const bool ends = inAdjust_ ? ((vlc_ + 1) & 0x1F) >= reg_[5] : vlc_ >= reg_[9] && vcc_ == reg_[4] && reg_[5] == 0;
    return ends && !(interlace() && !parityFrame_);
}

// The frame has run its lines. With an interlace mode set, an even frame
// gets one more line: C4 stays where it is and C9 is 0 (19.6.4).
void Crtc::endFrameAsic()
{
    if (interlace() && !parityFrame_) {
        inAdjust_ = false;
        interlaceLine_ = true;
        vlc_ = 0;
    } else {
        newFrameAsic();
    }
}

void Crtc::newFrameAsic()
{
    // A split on the frame's last line: the new frame starts from its
    // address, not from R12/R13.
    const bool split = splitLine_ != 0 && asicLine() == splitLine_;
    inAdjust_ = interlaceLine_ = false;
    vcc_ = 0;
    frameLine_ = 0;
    ++frames_;
    // The parity changes with every frame, whatever R8 holds. A VSYNC on
    // row 0 is dealt with before it does (19.7.3).
    const bool before = parityFrame_;
    parityFrame_ = !parityFrame_;
    parityC9_ = parityFrame_;
    vlc_ = interlaceVideo() ? parityC9_ : 0;
    vDisp_ = true;
    maRow_ = split ? splitAddress_ : startAddress();
    rowStartAsic(before);
}

void Crtc::rowStartAsic(bool oddFrame)
{
    if (vcc_ == reg_[6])
        vDisp_ = false;
    // Nothing remembers that C4 = R7 has been seen already: with a frame of
    // one row the VSYNC starts again as soon as it has ended (16.3).
    if (vcc_ != reg_[7] || vsync_)
        return;
    if (interlace() && !oddFrame)
        midVsync_ = true;
    else if (interlaceVideo() && oddFrame && (reg_[9] & 1) && (vcc_ & 1))
        lateVsync_ = true;
    else
        startVsync();
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
    // The ASICs cannot run past R9: lowering it below C9 ends the row
    // (10.3.4).
    if (asic())
        return vlc_ >= reg_[9];
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
        // A frame that starts with R6 = 0 is not border at once: on its
        // first line the border comes and goes with each character
        // (18.3.2).
        if (vcc_ == 0)
            r6Conflict_ = vDisp_;
        else
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
    endSeenOnOne_ = false;
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
            if (!rowR4AtEnd_) {
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
                // Still on row R4: C4 steps past it, once. From then on C9
                // answers to R5, and at once: the adjustment that was
                // settled on character 2 ends here if the C9 worked out for
                // the next line is R5 (11.2.2). R5 set to 0 later in the
                // line, or R9 moved away on character 1 and back with
                // R5 = 0, leaves no line to add (Shaker E, "R5 cancelation
                // on R5 upd" and "switch comparator", on a real chip).
                const uint8_t c9 = c9Match ? 0 : (vlc_ + 1) & 0x1F;
                if (c4Step && adjustRunning_ && c9 == reg_[5]) {
                    endFrame0();
                } else {
                    if (c4Step) {
                        vcc_ = (vcc_ + 1) & 0x7F;
                        rowChanged0();
                    }
                    vlc_ = c9;
                    lastLine_ = false;
                }
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
