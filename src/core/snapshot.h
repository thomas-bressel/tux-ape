#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tuxape {

class Cpc;

// SNA snapshots, the common format for the state of a CPC: a 256-byte
// header with the registers of every chip, then the memory. Versions 1 to 3
// are read. Version 3 is written, as WinAPE does: with the memory after the
// header when the machine has 64K or 128K, in compressed "MEMn" chunks when
// it has more.

// Machine named in the snapshot, as the format numbers them.
enum class SnapshotMachine : uint8_t {
    Cpc464 = 0,
    Cpc664 = 1,
    Cpc6128 = 2,
    Unknown = 3,
    Plus6128 = 4,
    Plus464 = 5,
    Gx4000 = 6,
};

struct SnapshotInfo {
    int version = 0;
    int ramKb = 0;
    SnapshotMachine machine = SnapshotMachine::Unknown;  // from version 2 on
};

// Reads the header only. Returns false if `data` is not a snapshot.
bool snapshotInfo(std::span<const uint8_t> data, SnapshotInfo& info);

// Puts the machine in the state the snapshot describes. RAM is added if the
// snapshot holds more than is fitted; the ROMs are left as they are. On
// failure the machine is untouched and `error`, if given, says why.
bool loadSnapshot(Cpc& cpc, std::span<const uint8_t> data, std::string* error = nullptr);

// Writes the state of the machine as a version 3 snapshot.
std::vector<uint8_t> saveSnapshot(Cpc& cpc, SnapshotMachine machine = SnapshotMachine::Cpc6128);

}  // namespace tuxape
