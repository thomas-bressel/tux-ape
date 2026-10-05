#include "core/session.h"

#include <cstring>

#include "core/cpc.h"
#include "core/snapshot.h"

namespace tuxape {

namespace {

// The chunk: a version byte, the number of frames, the number of events,
// then each event as its frame and the ten lines of the matrix.
constexpr char kChunk[] = "TXSN";
constexpr size_t kEventSize = 14;

void put(std::vector<uint8_t>& out, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}

uint32_t quad(const uint8_t* at)
{
    return static_cast<uint32_t>(at[0] | at[1] << 8 | at[2] << 16) | static_cast<uint32_t>(at[3]) << 24;
}

}  // namespace

std::vector<uint8_t> Session::serialise() const
{
    std::vector<uint8_t> out = snapshot;
    out.insert(out.end(), kChunk, kChunk + 4);
    put(out, static_cast<uint32_t>(9 + events.size() * kEventSize));
    out.push_back(1);
    put(out, frames);
    put(out, static_cast<uint32_t>(events.size()));
    for (const Event& event : events) {
        put(out, event.frame);
        out.insert(out.end(), event.lines.begin(), event.lines.end());
    }
    return out;
}

std::optional<Session> Session::parse(std::span<const uint8_t> file)
{
    SnapshotInfo info;
    if (!snapshotInfo(file, info) || info.version < 3)
        return std::nullopt;
    // The chunks follow the header and the memory the header says is
    // there.
    size_t pos = 0x100 + static_cast<size_t>(file[0x6B] | file[0x6C] << 8) * 1024;
    while (pos + 8 <= file.size()) {
        const size_t length = quad(&file[pos + 4]);
        if (std::memcmp(&file[pos], kChunk, 4) == 0) {
            const std::span<const uint8_t> data = file.subspan(pos + 8, std::min(length, file.size() - pos - 8));
            if (data.size() < 9 || data[0] != 1)
                return std::nullopt;
            Session session;
            session.snapshot.assign(file.begin(), file.begin() + static_cast<ptrdiff_t>(pos));
            session.frames = quad(&data[1]);
            const size_t count = std::min<size_t>(quad(&data[5]), (data.size() - 9) / kEventSize);
            for (size_t i = 0; i < count; ++i) {
                const uint8_t* at = &data[9 + i * kEventSize];
                Event event;
                event.frame = quad(at);
                std::memcpy(event.lines.data(), at + 4, 10);
                session.events.push_back(event);
            }
            return session;
        }
        pos += 8 + length;
    }
    return std::nullopt;
}

bool beginSession(Cpc& cpc, const Session& session, std::string* error)
{
    cpc.coldReset();
    cpc.keyboard().releaseAll();
    const bool loaded = loadSnapshot(cpc, session.snapshot, error);
    cpc.alignClocks();
    return loaded;
}

void SessionRecorder::start(std::vector<uint8_t> snapshot)
{
    session_ = Session();
    session_.snapshot = std::move(snapshot);
    last_.fill(0xFF);
    active_ = true;
}

void SessionRecorder::frame(const Keyboard& keyboard)
{
    if (!active_)
        return;
    std::array<uint8_t, 10> lines;
    for (int n = 0; n < 10; ++n)
        lines[static_cast<size_t>(n)] = keyboard.line(n);
    if (lines != last_) {
        session_.events.push_back({session_.frames, lines});
        last_ = lines;
    }
    ++session_.frames;
}

Session SessionRecorder::finish()
{
    active_ = false;
    return std::move(session_);
}

void SessionPlayer::start(Session session)
{
    session_ = std::move(session);
    frame_ = 0;
    next_ = 0;
    active_ = true;
}

bool SessionPlayer::frame(Keyboard& keyboard)
{
    if (!active_)
        return false;
    if (frame_ >= session_.frames) {
        active_ = false;
        keyboard.releaseAll();
        return false;
    }
    while (next_ < session_.events.size() && session_.events[next_].frame <= frame_) {
        for (int n = 0; n < 10; ++n)
            keyboard.setLine(n, session_.events[next_].lines[static_cast<size_t>(n)]);
        ++next_;
    }
    ++frame_;
    return true;
}

}  // namespace tuxape
