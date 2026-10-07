// Sessions recorded by WinAPE (".snr"): the file as WinAPE lays it out,
// read here from a file made the same way, and the playback: keys that
// change state frame by frame, with frames that end where the CRTC's VSYNC
// begins.
//
// The playback needs the ROM images; without them the test runs the rest
// and exits with code 77 (skipped).

#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
#include "core/snapshot.h"
#include "core/winape_session.h"

namespace {

using namespace tuxape;

void chunk(std::vector<uint8_t>& file, const char* id, const std::vector<uint8_t>& data, bool toTheEnd = false)
{
    file.insert(file.end(), id, id + 4);
    const uint32_t length = toTheEnd ? 0xFFFFFFFFu : static_cast<uint32_t>(data.size());
    for (int n = 0; n < 4; ++n)
        file.push_back(static_cast<uint8_t>(length >> (8 * n)));
    file.insert(file.end(), data.begin(), data.end());
}

std::vector<uint8_t> text(const std::string& s)
{
    return {s.begin(), s.end()};
}

// A session as WinAPE writes one, from this machine's state.
std::vector<uint8_t> recording(Cpc& cpc, const std::vector<uint8_t>& events)
{
    std::vector<uint8_t> file = saveSnapshot(cpc);
    std::memcpy(file.data(), "RW - SNR", 8);
    // A byte of flags, no cartridge, the lower ROM, sixteen upper ROMs.
    std::vector<uint8_t> roms{'A', 0};
    for (const char* name : {"OS6128", "BASIC1-1", "", "", "", "", "", "", "AMSDOS", "", "", "", "", "", "", "", ""}) {
        roms.insert(roms.end(), name, name + std::strlen(name));
        roms.push_back(0);
    }
    chunk(file, "ROMS", roms);
    chunk(file, "DSCA", text("Dragon's Lair (UK) (1985) [Original].dsk"));
    chunk(file, "DSCB", {});
    chunk(file, "SNRV", {1});
    std::vector<uint8_t> stream(78, 0);  // the state the events come after
    stream.insert(stream.end(), events.begin(), events.end());
    chunk(file, "SNR ", stream, true);
    return file;
}

void testFile()
{
    Cpc cpc;
    const std::vector<uint8_t> file = recording(
        cpc, {
                 0x60, 0x01, 0x41,                          // after 96 frames: key 65
                 0x05, 0x7F,                                // 5 later: the same again
                 0x10, 0x02, 0x1B, 0x43,                    // two keys at once
                 0x03, 0x80, 0xDE, 0x00, 0x00, 0x00,        // the clock's four bytes, no key
                 0x02, 0xFF, 0x52, 0x00, 0x01, 0x01,        // four bytes, and the two keys again
                 0x00, 0x2C, 0x01, 0x81, 0x11, 0, 2, 2, 0x2F,  // 300 frames later, four bytes and a key
                 0x07,                                      // and seven frames to the end
             });
    CHECK(WinApeSession::isOne(file));
    CHECK(!WinApeSession::isOne(saveSnapshot(cpc)));
    const auto session = WinApeSession::parse(file);
    CHECK(session.has_value());
    if (!session)
        return;
    CHECK(session->lowerRom == "OS6128");
    CHECK(session->upperRoms[0] == "BASIC1-1" && session->upperRoms[7] == "AMSDOS" && session->upperRoms[1].empty());
    CHECK(session->cartridge.empty());
    CHECK(session->discA == "Dragon's Lair (UK) (1985) [Original].dsk" && session->discB.empty());
    CHECK(session->machine.version == 3 && session->machine.machine == SnapshotMachine::Cpc6128);
    // The snapshot is one any reader takes.
    SnapshotInfo info;
    CHECK(snapshotInfo(session->snapshot, info));
    CHECK(session->snapshot == saveSnapshot(cpc));
    using Event = WinApeSession::Event;
    const std::vector<Event> expected{{96, {0x41}}, {101, {0x41}}, {117, {0x1B, 0x43}}, {122, {0x1B, 0x43}}, {422, {0x2F}}};
    CHECK(session->events == expected);
    CHECK_EQ(session->frames, 429);

    // Cut short, it is no session.
    std::vector<uint8_t> cut = file;
    cut.resize(cut.size() - 3);
    CHECK(!WinApeSession::parse(cut).has_value());
    // A cartridge's name comes first, after the flags.
    std::vector<uint8_t> plus = saveSnapshot(cpc);
    std::memcpy(plus.data(), "RW - SNR", 8);
    chunk(plus, "ROMS", text(std::string("D") + "C:\\Games\\Navy Seals.cpr" + '\0' + '\0'));
    chunk(plus, "SNRV", {1});
    chunk(plus, "SNR ", std::vector<uint8_t>(78 + 1, 0x09), true);
    const auto onPlus = WinApeSession::parse(plus);
    CHECK(onPlus && onPlus->cartridge == "C:\\Games\\Navy Seals.cpr" && onPlus->lowerRom.empty());
}

void testPlayer()
{
    Cpc cpc;
    const auto session =
        WinApeSession::parse(recording(cpc, {0x02, 0x01, 0x2F, 0x03, 0x7F, 0x01, 0x02, 0x00, 0x48, 0x02, 0x7F, 0x02}));
    CHECK(session.has_value());
    if (!session)
        return;
    Keyboard keys;
    WinApeSessionPlayer player;
    player.start(*session);
    CHECK(player.active() && player.frames() == 10);
    std::string seen;
    while (player.frame(keys))
        seen += keys.pressed(CpcKey::Space) ? 'S' : keys.pressed(CpcKey::CursorUp) && keys.pressed(CpcKey::JoyUp) ? 'U' : '.';
    // Frames 0-1 nothing, 2-4 the space bar, 5 nothing, 6-7 two keys, 8-9
    // nothing; the keyboard is let go at the end.
    CHECK(seen == "..SSS.UU..");
    CHECK(!player.active() && !keys.pressed(CpcKey::Space));
}

// WinAPE's frames: from one start of the CRTC's VSYNC to the next, the
// instruction in progress finished.
bool testFrames()
{
    Cpc cpc;
    std::string error;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), &error)) {
        std::printf("no ROM images (%s): the playback is not tested\n", error.c_str());
        return false;
    }
    for (int f = 0; f < 100; ++f)
        cpc.runFrame();
    for (int frame = 0; frame < 5; ++frame) {
        const uint64_t before = cpc.microseconds();
        runWinApeFrame(cpc);
        const Crtc& crtc = cpc.crtc();
        CHECK(crtc.vsync());
        CHECK(crtc.vcc() == crtc.reg(7) && crtc.vlc() == 0 && crtc.hcc() < 8);
        if (frame > 0) {
            const uint64_t took = cpc.microseconds() - before;
            CHECK(took > 19968 - 8 && took < 19968 + 8);
        }
    }
    // Without a VSYNC the frame still ends.
    cpc.crtc().select(7);
    cpc.crtc().write(127);
    runWinApeFrame(cpc);
    const uint64_t before = cpc.microseconds();
    runWinApeFrame(cpc);
    const uint64_t took = cpc.microseconds() - before;
    CHECK(took >= 352 * 64 && took < 352 * 64 + 8);

    // A session, from its snapshot: the keys reach the program on the frame
    // they are due. Here BASIC, which is typed a line.
    cpc.crtc().select(7);
    cpc.crtc().write(30);
    for (int f = 0; f < 5; ++f)
        runWinApeFrame(cpc);
    // 10 frames in, P down for 5 frames; then RETURN (key 18).
    const auto session = WinApeSession::parse(recording(cpc, {0x0A, 0x01, 0x1B, 0x05, 0x7F, 0x05, 0x01, 0x12, 0x05, 0x7F, 0x20}));
    CHECK(session.has_value());
    if (!session)
        return true;
    Cpc other;
    CHECK(setupStockMachine(other, CpcModel::Cpc6128, defaultRomDir(), &error));
    CHECK_EQ(other.gateArray().interruptDelay(), GateArray::kInterruptDelay);
    CHECK(beginWinApeSession(other, *session, &error));
    // WinAPE's way with interrupts while its session plays.
    CHECK_EQ(other.gateArray().interruptDelay(), 0);
    WinApeSessionPlayer player;
    player.start(*session);
    int frames = 0;
    bool pDown = false;
    while (player.frame(other.keyboard())) {
        pDown = pDown || other.keyboard().pressed(CpcKey::P);
        runWinApeFrame(other);
        ++frames;
    }
    CHECK(pDown);
    CHECK_EQ(frames, 57);
    endWinApeSession(other);
    CHECK_EQ(other.gateArray().interruptDelay(), GateArray::kInterruptDelay);
    // "p" was typed and RETURN pressed: BASIC answers "Syntax error".
    bool found = false;
    for (int address = 0xC000; address < 0x10000 && !found; ++address)
        found = other.memory().readRam(static_cast<uint16_t>(address)) != 0;
    CHECK(found);
    return true;
}

}  // namespace

int main()
{
    testFile();
    testPlayer();
    const bool played = testFrames();
    const int result = checkSummary("winape_session");
    return result != 0 ? result : played ? 0 : 77;
}
