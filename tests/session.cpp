// Recorded sessions: the file, and above all that a session played back
// does what it did when it was recorded, on another machine too.
//
// The second part needs the ROM images; without them the test runs the
// rest and exits with code 77 (skipped).

#include <string>
#include <vector>

#include "check.h"
#include "core/autotype.h"
#include "core/cpc.h"
#include "core/screen_text.h"
#include "core/session.h"
#include "core/setup.h"
#include "core/snapshot.h"

namespace {

using namespace tuxape;

void testFile()
{
    Cpc cpc;
    Session session;
    session.snapshot = saveSnapshot(cpc);
    session.frames = 1234;
    session.events.push_back({0, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}});
    session.events.push_back({50, {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0xEF}});
    session.events.push_back({70000, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}});
    const std::vector<uint8_t> file = session.serialise();
    // Still a snapshot for whoever does not know of the recording.
    SnapshotInfo info;
    CHECK(snapshotInfo(file, info));
    CHECK(loadSnapshot(cpc, file));
    const auto again = Session::parse(file);
    CHECK(again.has_value());
    if (again) {
        CHECK(again->snapshot == session.snapshot);
        CHECK(again->events == session.events);
        CHECK_EQ(again->frames, 1234);
    }
    // A snapshot alone is not a session; nor is something else altogether.
    CHECK(!Session::parse(session.snapshot).has_value());
    CHECK(!Session::parse(std::vector<uint8_t>(300, 0)).has_value());
    CHECK(!Session::parse(std::vector<uint8_t>{}).has_value());

    // The recorder notes the matrix when it changes; the player gives it
    // back on the same frames and lets go of the keys at the end.
    Keyboard keyboard;
    SessionRecorder recorder;
    recorder.start(session.snapshot);
    for (int frame = 0; frame < 40; ++frame) {
        keyboard.set(CpcKey::A, frame >= 10 && frame < 20);
        keyboard.set(CpcKey::JoyFire1, frame >= 15 && frame < 30);
        recorder.frame(keyboard);
    }
    keyboard.set(CpcKey::Space, true);  // held when the recording ends
    recorder.frame(keyboard);
    const Session recorded = recorder.finish();
    CHECK(!recorder.active());
    CHECK_EQ(recorded.frames, 41);
    CHECK_EQ(recorded.events.size(), 5);  // four changes, and the space bar
    Keyboard played;
    SessionPlayer player;
    player.start(recorded);
    for (int frame = 0; frame < 41; ++frame) {
        CHECK(player.frame(played));
        CHECK_EQ(played.pressed(CpcKey::A), frame >= 10 && frame < 20);
        CHECK_EQ(played.pressed(CpcKey::JoyFire1), frame >= 15 && frame < 30);
        CHECK_EQ(played.pressed(CpcKey::Space), frame == 40);
    }
    CHECK(!player.frame(played));
    CHECK(!player.active());
    CHECK(!played.pressed(CpcKey::Space));
}

uint32_t stateOf(Cpc& cpc)
{
    uint32_t hash = 2166136261u;
    auto mix = [&](unsigned value) { hash = (hash ^ value) * 16777619u; };
    for (int address = 0; address < 0x10000; ++address)
        mix(cpc.memory().baseRam()[address]);
    const auto& cpu = cpc.cpu();
    for (const uint8_t r : cpu.reg)
        mix(r);
    mix(cpu.pc);
    mix(cpu.sp);
    mix(cpc.crtc().vcc());
    mix(cpc.crtc().vlc());
    mix(cpc.crtc().hcc());
    mix(cpc.gateArray().interruptCounter());
    return hash;
}

// A program that draws according to the keys and to when they come is
// recorded, then played back on the same machine and on another: the
// machines end in the same state, to the byte.
bool testDeterminism()
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr))
        return false;
    AutoType keys(cpc.keyboard());
    auto frames = [&](Cpc& machine, int count) {
        for (int i = 0; i < count; ++i)
            machine.runFrame();
    };
    frames(cpc, 150);
    // Keys make it print, and the time between them decides where.
    keys.type("10 a$=INKEY$:IF a$<>\"\" THEN PRINT a$;TIME;\n20 GOTO 10\nRUN\n");
    while (keys.active()) {
        keys.frame();
        cpc.runFrame();
    }
    frames(cpc, 37);

    // The recording, with keys pressed straight on the matrix.
    Session start;
    start.snapshot = saveSnapshot(cpc);
    CHECK(beginSession(cpc, start));
    SessionRecorder recorder;
    recorder.start(start.snapshot);
    const CpcKey pressed[] = {CpcKey::A, CpcKey::Z, CpcKey::E, CpcKey::R, CpcKey::T, CpcKey::Y};
    for (int frame = 0; frame < 400; ++frame) {
        const int which = frame / 53;
        cpc.keyboard().set(pressed[which % 6], frame % 53 >= 7 && frame % 53 < 7 + 3 + which);
        recorder.frame(cpc.keyboard());
        cpc.runFrame();
    }
    const Session session = recorder.finish();
    const uint32_t recorded = stateOf(cpc);
    const std::string screen = readScreenText(cpc);
    CHECK(screen.find('a') != std::string::npos && screen.find('y') != std::string::npos);

    // Played back where it was recorded, through its file...
    const auto loaded = Session::parse(session.serialise());
    CHECK(loaded.has_value());
    if (!loaded)
        return true;
    auto play = [&](Cpc& machine) {
        CHECK(beginSession(machine, *loaded));
        SessionPlayer player;
        player.start(*loaded);
        int played = 0;
        while (player.frame(machine.keyboard())) {
            machine.runFrame();
            ++played;
        }
        CHECK_EQ(played, 400);
        return stateOf(machine);
    };
    frames(cpc, 123);  // whatever the machine was doing meanwhile
    CHECK_EQ(play(cpc), recorded);
    CHECK(readScreenText(cpc) == screen);
    // ... and on a machine that has just been switched on.
    Cpc other;
    CHECK(setupStockMachine(other, CpcModel::Cpc6128, defaultRomDir(), nullptr));
    CHECK_EQ(play(other), recorded);
    CHECK(readScreenText(other) == screen);
    // Other keys, another end: the comparison means something.
    Session changed = *loaded;
    changed.events[2].frame += 1;
    Cpc third;
    CHECK(setupStockMachine(third, CpcModel::Cpc6128, defaultRomDir(), nullptr));
    CHECK(beginSession(third, changed));
    SessionPlayer player;
    player.start(changed);
    while (player.frame(third.keyboard()))
        third.runFrame();
    CHECK(stateOf(third) != recorded);
    return true;
}

}  // namespace

int main()
{
    testFile();
    const bool roms = testDeterminism();
    if (!roms)
        std::printf("ROM images not found; playback was not tested\n");
    const int result = checkSummary("session");
    return result != 0 ? result : roms ? 0 : 77;
}
