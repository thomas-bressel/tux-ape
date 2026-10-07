#include "core/winape_session.h"

#include <cstring>

#include "core/cpc.h"

namespace tuxape {

namespace {

constexpr char kSignature[] = "RW - SNR";
constexpr size_t kHeaderSize = 0x100;
constexpr size_t kDumpSize = 0x6B;
constexpr size_t kStateSize = 78;  // what the stream of events starts with

uint32_t quad(const uint8_t* at)
{
    return at[0] | at[1] << 8 | at[2] << 16 | static_cast<uint32_t>(at[3]) << 24;
}

// The names of a chunk, each ended by a 0.
std::vector<std::string> names(std::span<const uint8_t> data)
{
    std::vector<std::string> out;
    std::string name;
    for (const uint8_t byte : data) {
        if (byte == 0) {
            out.push_back(name);
            name.clear();
        } else {
            name += static_cast<char>(byte);
        }
    }
    if (!name.empty())
        out.push_back(name);
    return out;
}

// The events. False if they stop short.
bool readEvents(std::span<const uint8_t> data, WinApeSession& session)
{
    size_t pos = 0;
    uint32_t frame = 0;
    std::vector<uint8_t> last;
    while (pos < data.size()) {
        uint32_t wait = data[pos++];
        if (wait == 0) {
            if (pos + 2 > data.size())
                return false;
            wait = data[pos] | data[pos + 1] << 8;
            pos += 2;
        }
        frame += wait;
        // A wait with nothing after it: the end of the recording.
        if (pos >= data.size())
            break;
        uint8_t count = data[pos++];
        if (count & 0x80) {
            // The clock's and the mouse's four bytes.
            if (pos + 4 > data.size())
                return false;
            pos += 4;
            count &= 0x7F;
        }
        std::vector<uint8_t> keys;
        if (count == 0x7F) {
            keys = last;
        } else {
            if (pos + count > data.size())
                return false;
            keys.assign(data.begin() + static_cast<ptrdiff_t>(pos), data.begin() + static_cast<ptrdiff_t>(pos + count));
            pos += count;
            if (count != 0)
                last = keys;
        }
        if (!keys.empty())
            session.events.push_back({frame, std::move(keys)});
    }
    session.frames = frame;
    return true;
}

}  // namespace

bool WinApeSession::isOne(std::span<const uint8_t> file)
{
    return file.size() >= kHeaderSize && std::memcmp(file.data(), kSignature, 8) == 0;
}

std::optional<WinApeSession> WinApeSession::parse(std::span<const uint8_t> file)
{
    if (!isOne(file))
        return std::nullopt;
    WinApeSession session;
    bool events = false;
    size_t pos = kHeaderSize + static_cast<size_t>(file[kDumpSize] | file[kDumpSize + 1] << 8) * 1024;
    size_t snapshotEnd = std::min(pos, file.size());
    while (pos + 8 <= file.size()) {
        const size_t length = std::min<size_t>(quad(&file[pos + 4]), file.size() - pos - 8);
        const std::span<const uint8_t> data = file.subspan(pos + 8, length);
        const auto is = [&](const char* id) { return std::memcmp(&file[pos], id, 4) == 0; };
        if (is("ROMS") && !data.empty()) {
            // A byte of flags, then the cartridge, the lower ROM and the
            // upper ones.
            const std::vector<std::string> list = names(data.subspan(1));
            if (!list.empty())
                session.cartridge = list[0];
            if (list.size() > 1)
                session.lowerRom = list[1];
            for (size_t slot = 0; slot < session.upperRoms.size() && slot + 2 < list.size(); ++slot)
                session.upperRoms[slot] = list[slot + 2];
        } else if (is("DSCA")) {
            session.discA.assign(data.begin(), data.end());
        } else if (is("DSCB")) {
            session.discB.assign(data.begin(), data.end());
        } else if (is("SNR ")) {
            if (data.size() < kStateSize || !readEvents(data.subspan(kStateSize), session))
                return std::nullopt;
            events = true;
            break;
        } else if (!is("SNRV")) {
            // The snapshot's own: memory, the Plus's registers.
            snapshotEnd = pos + 8 + length;
        }
        pos += 8 + length;
    }
    if (!events)
        return std::nullopt;
    session.snapshot.assign(file.begin(), file.begin() + static_cast<ptrdiff_t>(snapshotEnd));
    std::memcpy(session.snapshot.data(), "MV - SNA", 8);
    if (!snapshotInfo(session.snapshot, session.machine))
        return std::nullopt;
    return session;
}

bool beginWinApeSession(Cpc& cpc, const WinApeSession& session, std::string* error)
{
    cpc.keyboard().releaseAll();
    const bool loaded = loadSnapshot(cpc, session.snapshot, error);
    cpc.keyboard().releaseAll();
    cpc.alignClocks();
    return loaded;
}

void runWinApeFrame(Cpc& cpc)
{
    // A frame of 352 lines is as far as the tube lets one go.
    cpc.runToVsync(352 * 64);
}

void WinApeSessionPlayer::start(WinApeSession session)
{
    session_ = std::move(session);
    frame_ = 0;
    next_ = 0;
    active_ = true;
}

bool WinApeSessionPlayer::frame(Keyboard& keyboard)
{
    if (!active_)
        return false;
    if (frame_ >= session_.frames) {
        active_ = false;
        keyboard.releaseAll();
        return false;
    }
    while (next_ < session_.events.size() && session_.events[next_].frame <= frame_) {
        for (const uint8_t key : session_.events[next_].keys)
            if (key < kCpcKeyCount)
                keyboard.set(static_cast<CpcKey>(key), !keyboard.pressed(static_cast<CpcKey>(key)));
        ++next_;
    }
    ++frame_;
    return true;
}

}  // namespace tuxape
