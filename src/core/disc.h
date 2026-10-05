#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tuxape {

// A disc layout that can be formatted, with the CP/M parameters needed to
// read its directory. This is WinAPE's list of formats, in its order.
struct DiscFormat {
    const char* name;
    uint8_t sectorsPerTrack;
    uint8_t firstSectorId;
    uint8_t interleave;       // 1: IDs in sequence; 5: 1,6,2,7,3... as AMSDOS formats
    uint8_t tracks;
    uint8_t sides;
    uint8_t blockShift;       // CP/M BSH: block size is 128 << blockShift
    uint16_t lastBlock;       // CP/M DSM
    uint8_t reservedTracks;   // CP/M OFF
    uint16_t directoryEntries;
    uint8_t extentMask;       // CP/M EXM
    uint8_t gap3;
    uint8_t filler;
};

std::span<const DiscFormat> discFormats();
// "DATA (SS 40)", the format of a blank disc as AMSDOS expects it.
const DiscFormat& defaultDiscFormat();

// One sector as recorded on the disc.
struct DiscSector {
    // The ID field.
    uint8_t c = 0, h = 0, r = 0, n = 0;
    // Error and mark flags, as the FDC reports them in ST1 and ST2: CRC
    // errors (0x20 in either), deleted data (0x40 in st2).
    uint8_t st1 = 0, st2 = 0;
    // Recorded bytes. Sectors that read differently each time ("weak"
    // sectors) hold several copies one after the other.
    std::vector<uint8_t> data;

    // Size the ID field announces.
    int nominalSize() const { return 128 << (n > 8 ? 8 : n); }
    int copies() const;
    int copySize() const { return static_cast<int>(data.size()) / copies(); }
    bool deleted() const { return st2 & 0x40; }

    int nextCopy = 0;  // which copy the next read returns
};

struct DiscTrack {
    std::vector<DiscSector> sectors;  // in the order they pass under the head
    uint8_t gap3 = 0x4E;
    uint8_t filler = 0xE5;

    bool formatted() const { return !sectors.empty(); }

    // Where each sector sits around the track, in bytes from the index
    // hole, on a track of kLength bytes. Call updateLayout() after changing
    // the sector list.
    static constexpr int kLength = 6250;
    std::vector<int> idPosition;    // end of the sector's ID field
    std::vector<int> dataPosition;  // first data byte
    void updateLayout();
};

// The contents of a floppy disc, independent of any file format.
class Disc {
public:
    static constexpr int kMaxCylinders = 102;

    // An unformatted disc.
    Disc() = default;

    int cylinders() const { return cylinders_; }
    int sides() const { return sides_; }

    // Track under the given head, or nullptr beyond the recorded area.
    DiscTrack* track(int cylinder, int side);
    const DiscTrack* track(int cylinder, int side) const;
    // Same, creating unformatted tracks as needed so the track can be written.
    DiscTrack& ensureTrack(int cylinder, int side);

    // Lays down a whole disc. With `clearUnused` the tracks and sides the
    // format does not use are erased; otherwise they are left as they are.
    void format(const DiscFormat& format, bool clearUnused = true);
    // Lays down one track, as the FDC's format command does. `ids` holds
    // C, H, R, N for each sector.
    void formatTrack(int cylinder, int side, std::span<const uint8_t> ids, uint8_t gap3, uint8_t filler);

    // CPCEMU .DSK images, standard and extended.
    static std::optional<Disc> fromDsk(std::span<const uint8_t> file);
    std::vector<uint8_t> toDsk() const;

    // Short description for the user: tracks, sides, and the format if it
    // is one of the known ones.
    std::string describe() const;

    bool modified = false;
    bool writeProtected = false;

private:
    int cylinders_ = 0;
    int sides_ = 1;
    std::vector<DiscTrack> tracks_;  // cylinder * sides_ + side

    void resize(int cylinders, int sides);
};

}  // namespace tuxape
