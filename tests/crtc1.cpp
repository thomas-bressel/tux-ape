// What sets CRTC 1 (UM6845R) apart, as the "Amstrad CPC CRTC Compendium" by
// Longshot / Logon System describes it and as the Logon System "Shaker"
// shows it on real machines (module A, tests "R1 stories" and "VSYNC
// conditions").

#include <initializer_list>

#include "check.h"
#include "crtc_rig.h"

namespace {

using namespace tuxape;

// The address a line starts at: the refresh address on its first character.
int lineAddress(Rig& rig, int c4, int c9)
{
    rig.seek(c4, c9);
    return rig.crtc.ma();
}

// On its first row the UM6845R reads R12/R13 for every line. The address
// kept for the rows that follow only moves when C0 meets R1 on the last
// line of a row (17.4.2).
void videoPointer()
{
    Rig rig(CrtcType::UM6845R);
    rig.set(12, 0x30);
    rig.set(13, 0x00);
    CHECK_EQ(lineAddress(rig, 0, 0), 0x3000);
    CHECK_EQ(lineAddress(rig, 1, 0), 0x3000 + 40);
    CHECK_EQ(lineAddress(rig, 24, 7), 0x3000 + 24 * 40);

    // R12/R13 changed in the middle of the first row show from the next
    // line; on the other types they wait for the next frame.
    rig.seek(0, 3, 10);
    rig.set(13, 0x10);
    rig.nextLine();
    CHECK_EQ(rig.crtc.ma(), 0x3010);
    CHECK_EQ(lineAddress(rig, 1, 0), 0x3010 + 40);
    {
        Rig other(CrtcType::MC6845);
        other.set(12, 0x30);
        other.seek(0, 0);
        other.seek(0, 3, 10);
        other.set(13, 0x10);
        other.nextLine();
        CHECK_EQ(other.crtc.ma(), 0x3000);
        CHECK_EQ(lineAddress(other, 0, 0), 0x3010);
    }

    // With R1 out of C0's reach nothing is kept any more: the first row
    // shows R12/R13, every other row the address kept last ("R1 stories",
    // first screen: R1 raised on the last line of row 24, before C0 = R1).
    rig.set(13, 0x00);
    rig.seek(0, 0);
    rig.seek(24, 7, 39);
    rig.set(1, 127);
    const int kept = 0x3000 + 24 * 40;  // what row 23 left
    CHECK_EQ(lineAddress(rig, 25, 0), kept);
    CHECK_EQ(lineAddress(rig, 38, 7), kept);
    CHECK_EQ(lineAddress(rig, 0, 0), 0x3000);
    CHECK_EQ(lineAddress(rig, 0, 7), 0x3000);
    CHECK_EQ(lineAddress(rig, 1, 0), kept);
    CHECK_EQ(lineAddress(rig, 30, 0), kept);
    // Raised after C0 = R1, that line's address has been kept.
    rig.set(1, 40);
    rig.seek(0, 0);
    rig.seek(24, 7, 41);
    rig.set(1, 127);
    CHECK_EQ(lineAddress(rig, 25, 0), 0x3000 + 25 * 40);
    CHECK_EQ(lineAddress(rig, 0, 0), 0x3000);
    CHECK_EQ(lineAddress(rig, 1, 0), 0x3000 + 25 * 40);
}

// The VSYNC lasts 16 lines whatever R3 says. Lines of a single character
// (R0 = 0) do not count among them on this chip, though C9 and C4 go on
// counting; the MC6845 counts them like any other ("VSYNC conditions",
// first screen, on real chips of both kinds).
void vsyncAndShortLines()
{
    for (const CrtcType type : {CrtcType::UM6845R, CrtcType::MC6845}) {
        Rig rig(type);
        rig.seek(30, 0, 5);
        CHECK(rig.crtc.vsync());
        int lines = 0;
        while (rig.crtc.vsync()) {
            rig.nextLine();
            ++lines;
        }
        CHECK_EQ(lines, 16);

        // The Shaker's sequence: R0 = 0 on character 0 of the last line of
        // row 29, eighteen characters of it, then lines of 46 characters.
        rig.seek(29, 7);
        rig.set(0, 0);
        rig.crtc.tick();
        CHECK_EQ(rig.crtc.vcc(), 30);
        CHECK(rig.crtc.vsync());
        for (int i = 0; i < 17; ++i)
            rig.crtc.tick();
        CHECK_EQ(rig.crtc.vcc(), 32);  // 18 lines went by
        CHECK_EQ(rig.crtc.vlc(), 1);
        rig.set(0, 45);
        const bool stillUp = type == CrtcType::UM6845R;
        CHECK_EQ(rig.crtc.vsync(), stillUp);
        for (int i = 0; i < 46; ++i)
            rig.crtc.tick();
        CHECK_EQ(rig.crtc.vsync(), stillUp);
        if (stillUp) {
            // ... and it then runs its 16 lines.
            lines = 0;
            while (rig.crtc.vsync()) {
                rig.nextLine();
                ++lines;
            }
            CHECK_EQ(lines, 15);
        }
    }
}

}  // namespace

int main()
{
    videoPointer();
    vsyncAndShortLines();
    return checkSummary("crtc1");
}
