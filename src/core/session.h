#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tuxape {

class Cpc;
class Keyboard;

// A recorded session: a snapshot to start from, and what the keyboard and
// the joysticks did from then on, frame by frame. Kept in one file, the
// snapshot first (any program that reads snapshots can load it) and the
// rest as a chunk of TuxAPE's own after it.
//
// Discs and tapes are not in it: a session that reads one needs the same
// one in the drive when it is played back.
struct Session {
    struct Event {
        uint32_t frame = 0;                // counted from the start
        std::array<uint8_t, 10> lines{};   // the keyboard matrix from that frame on
        bool operator==(const Event&) const = default;
    };
    std::vector<uint8_t> snapshot;
    std::vector<Event> events;
    uint32_t frames = 0;  // how long it lasts

    std::vector<uint8_t> serialise() const;
    // Nothing for a file that is not a snapshot, or holds no recording.
    static std::optional<Session> parse(std::span<const uint8_t> file);
};

// Puts the machine at the session's start: reset to a known state, the
// snapshot loaded, the keyboard let go. Recording and playback both start
// here, so that the same keys bring the same session. False, with the
// machine reset, if the snapshot cannot be loaded.
bool beginSession(Cpc& cpc, const Session& session, std::string* error = nullptr);

// Notes the keyboard matrix once a frame, before the frame is run.
class SessionRecorder {
public:
    void start(std::vector<uint8_t> snapshot);
    bool active() const { return active_; }
    void frame(const Keyboard& keyboard);
    uint32_t frames() const { return session_.frames; }
    Session finish();

private:
    bool active_ = false;
    Session session_;
    std::array<uint8_t, 10> last_{};
};

// Gives the keyboard matrix back, frame by frame.
class SessionPlayer {
public:
    void start(Session session);
    void stop() { active_ = false; }
    bool active() const { return active_; }
    // Sets the keyboard for the frame about to be run. False once the
    // session is over: the keyboard is then let go.
    bool frame(Keyboard& keyboard);
    uint32_t position() const { return frame_; }
    uint32_t frames() const { return session_.frames; }

private:
    bool active_ = false;
    Session session_;
    uint32_t frame_ = 0;
    size_t next_ = 0;
};

}  // namespace tuxape
