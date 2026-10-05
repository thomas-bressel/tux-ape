#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/disc.h"

namespace tuxape {

// NEC uPD765A floppy disc controller with its drives.
//
// The discs turn in real time: a sector can only be read when it comes
// round, bytes arrive every 32 microseconds and are lost if the CPU does not
// collect them, and the head takes time to move. Copy protections and
// loaders that measure these things need it. "Fast" mode removes the waits.
//
// All times are in microseconds of emulated time.
class Fdc {
public:
    static constexpr int kDrives = 4;

    struct Drive {
        std::unique_ptr<Disc> disc;
        int cylinder = 0;  // where the head physically is
        // Read side 0 when asked for side 1 of a track that has none, as a
        // single-headed drive would.
        bool singleSidedRead = false;
        int nextSector = 0;  // rotation stand-in for fast mode
    };

    Fdc() { reset(); }
    void reset();

    void setFast(bool fast) { fast_ = fast; }
    bool fast() const { return fast_; }
    // The CPC wires only one of the two unit-select lines, so units 2 and 3
    // are drives A and B again unless four drives are enabled.
    void setFourDrives(bool enabled) { fourDrives_ = enabled; }

    // Bus side.
    void writeMotor(uint8_t value, uint64_t now);
    uint8_t readStatus(uint64_t now);
    uint8_t readData(uint64_t now);
    void writeData(uint8_t value, uint64_t now);

    Drive& drive(int n) { return drives_[n & 3]; }
    const Drive& drive(int n) const { return drives_[n & 3]; }
    bool motor() const { return motor_; }
    // Drive whose activity light is on, or -1.
    int activeDrive() const { return motor_ ? physicalDrive(unit_) : -1; }

private:
    enum class Phase : uint8_t { Command, Execute, Result };
    enum class Exec : uint8_t {
        None,
        Search,      // waiting for the wanted sector to come round
        Transfer,    // exchanging a sector's bytes with the CPU
        ReadId,      // waiting for the next ID field
        FormatWait,  // waiting for the index hole
        FormatIds,   // receiving the sector IDs to write
        Finish,      // waiting before the result is reported
    };
    enum class Op : uint8_t { None, Read, Write, Scan, ReadTrack };

    Drive drives_[kDrives];
    bool fast_ = false;
    bool fourDrives_ = false;
    bool motor_ = false;
    uint64_t motorOnTime_ = 0;
    uint8_t stepRate_ = 0;

    Phase phase_ = Phase::Command;
    Exec exec_ = Exec::None;
    Op op_ = Op::None;
    uint8_t command_[9] = {};
    int commandLength_ = 0;
    int commandCount_ = 0;
    uint8_t result_[7] = {};
    int resultLength_ = 0;
    int resultCount_ = 0;

    // The command being executed.
    int unit_ = 0;
    int head_ = 0;
    uint8_t c_ = 0, h_ = 0, r_ = 0, n_ = 0;
    uint8_t st0_ = 0, st1_ = 0, st2_ = 0;
    bool stopAfterSector_ = false;
    int sectorIndex_ = 0;
    int sectorsLeft_ = 0;  // read track, format

    uint64_t eventTime_ = 0;
    std::vector<uint8_t> buffer_;
    size_t bufferPos_ = 0;
    bool byteReady_ = false;
    uint64_t byteTime_ = 0;  // when the current byte became (or becomes) available

    // Seeks run in the background, one per unit.
    uint8_t presentCylinder_[kDrives] = {};
    bool seeking_[kDrives] = {};
    uint64_t seekDone_[kDrives] = {};
    bool interruptPending_[kDrives] = {};
    uint8_t interruptStatus_[kDrives] = {};

    int physicalDrive(int unit) const { return fourDrives_ ? unit : unit & 1; }
    Drive& currentDrive() { return drives_[physicalDrive(unit_)]; }
    bool ready(int unit) const;
    DiscTrack* currentTrack();
    // Sector the command in progress is working on, or nullptr if the disc
    // was taken out from under it.
    DiscSector* currentSector();
    void notReady(uint64_t now);
    uint64_t delayTo(int position, uint64_t now) const;
    uint64_t stepTime() const;

    void update(uint64_t now);
    void startCommand(uint64_t now);
    void startSeek(int unit, int target, bool recalibrate, uint64_t now);
    void startReadWrite(uint64_t now);
    void searchSector(uint64_t now);
    void startTransfer(uint64_t now);
    void sectorDone(uint64_t now);
    uint8_t takeByte(uint64_t now);
    void giveByte(uint8_t value, uint64_t now);
    void startReadId(uint64_t now);
    void startFormat(uint64_t now);
    void finishFormat(uint64_t now);
    void finish(uint64_t now, uint64_t delay);
    void enterResult();
    void setResult(int length);
    void idle();
    int transferLength() const;
};

}  // namespace tuxape
