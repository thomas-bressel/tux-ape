#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/memory.h"
#include "core/snapshot.h"

namespace tuxape {

class Cpc;
class Keyboard;

// A session recorded by WinAPE (an ".snr" file, which starts with the
// words "RW - SNR"): a type 3 snapshot, the names of the ROMs, of the
// cartridge and of the discs that were in, and the keys that were pressed
// and let go from then on, counted in frames.
//
// The file was read from WinAPE's own recordings, there being no account
// of it: after the snapshot's chunks come "ROMS" (a byte of flags, then
// the names of the cartridge, of the lower ROM and of the upper ROMs, each
// ended by a 0), "DSCA" and "DSCB" (the file in each drive), "SNRV" (a
// version: 1) and "SNR ", which runs to the end of the file. That last one
// starts with 78 bytes of state (among them the Symbiface clock) and goes
// on with events: a number of frames since the event before (a 0 says the
// number follows in two bytes), then a count of keys and their numbers,
// the firmware's own. Each key named changes state; a count of #7F stands
// for the same keys as the event before. Bit 7 of the count says four
// bytes come before the keys: they are there every 2^24 microseconds and
// belong to the clock and the mouse, which are not played back here.
//
// The disc images themselves are not in the file: a session that reads a
// disc needs the same one in the drive.
struct WinApeSession {
    struct Event {
        uint32_t frame = 0;         // counted from the start
        std::vector<uint8_t> keys;  // the keys that change state as that frame begins
        bool operator==(const Event&) const = default;
    };
    std::vector<uint8_t> snapshot;  // as any snapshot reader takes it
    SnapshotInfo machine;
    std::string cartridge;          // file names, as WinAPE had them; empty: none
    std::string lowerRom;
    std::array<std::string, Memory::kRomSlots> upperRoms;
    std::string discA, discB;
    std::vector<Event> events;
    uint32_t frames = 0;  // how long it lasts

    // Nothing for a file that is not one of WinAPE's sessions.
    static std::optional<WinApeSession> parse(std::span<const uint8_t> file);
    static bool isOne(std::span<const uint8_t> file);
};

// Loads the session's snapshot, the keyboard let go. The ROMs and the
// discs are the caller's to put in first. False if the snapshot cannot be
// loaded.
bool beginWinApeSession(Cpc& cpc, const WinApeSession& session, std::string* error = nullptr);

// Runs the machine for one of WinAPE's frames. They end where the CRTC's
// VSYNC begins, with the instruction in progress there: every snapshot of
// its recordings was taken at that place. Without a VSYNC the frame ends
// when the tube would have gone back up on its own.
void runWinApeFrame(Cpc& cpc);

// Gives the keys back, frame by frame.
class WinApeSessionPlayer {
public:
    void start(WinApeSession session);
    void stop() { active_ = false; }
    bool active() const { return active_; }
    // Sets the keyboard for the frame about to be run. False once the
    // session is over: the keyboard is then let go.
    bool frame(Keyboard& keyboard);
    uint32_t position() const { return frame_; }
    uint32_t frames() const { return session_.frames; }

private:
    bool active_ = false;
    WinApeSession session_;
    uint32_t frame_ = 0;
    size_t next_ = 0;
};

}  // namespace tuxape
