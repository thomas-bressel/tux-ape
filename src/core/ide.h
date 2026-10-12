#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace tuxape {

// An IDE (ATA) disc whose sectors are a file: the 512 MB "disk on module"
// of ACME's X-MASS card, which the machine reaches at the SYMBiFACE II's
// addresses: &FD08 to &FD0F for the disc's eight registers, &FD06 for its
// second status and its control register, &FD07 for the drive address.
// The card has eight data lines for the disc's sixteen: a sector's 512
// bytes come and go one at a time through the data register, in order.
//
// The disc is the only one on the cable (the master). It knows the
// commands a DOS uses: who it is, read and write sectors by number (LBA) or
// by cylinder, head and sector, and the ones that only ask it to get
// ready. The file is as long as what has been written: sectors beyond its
// end read as zeros, and it is made when the first one is written.
class IdeDisc {
public:
    static constexpr int kSectorSize = 512;
    // The card's own module: 1015 cylinders of 16 heads and 63 sectors,
    // which JarvisOS shows as 0511560K.
    static constexpr uint64_t kDefaultBytes = 1015ull * 16 * 63 * 512;
    // Status bits.
    static constexpr uint8_t kBusy = 0x80, kReady = 0x40, kSeekDone = 0x10, kDataRequest = 0x08, kError = 0x01;
    // Error bits.
    static constexpr uint8_t kAborted = 0x04, kNotFound = 0x10;

    // The file, and how much the disc holds when the file is shorter than
    // that (or not there yet). A longer file is a bigger disc.
    explicit IdeDisc(std::string file, uint64_t bytes = kDefaultBytes);

    const std::string& file() const { return file_; }
    uint32_t sectors() const { return sectors_; }
    // The shape it says it has.
    int cylinders() const { return cylinders_; }
    static constexpr int kHeads = 16, kSectorsPerTrack = 63;
    // What it says it is called.
    static constexpr const char* kModel = "PQI IDE DiskOnModule";

    // The registers, 0 to 7 as the disc numbers them: data, error (read) or
    // features (written), sector count, sector number, cylinder low and
    // high, drive and head, status (read) or command (written).
    uint8_t read(int reg);
    void write(int reg, uint8_t value);
    // The second status register, which is the first read on the quiet,
    // and the control register, whose bit 2 resets the disc.
    uint8_t readStatus() const { return selected() ? status_ : 0; }
    void writeControl(uint8_t value);
    // Power on, or the machine's reset.
    void reset();

    // False once the file could not be read or written.
    bool good() const { return good_; }

private:
    bool selected() const { return !(device_ & 0x10); }
    void command(uint8_t value);
    void identify();
    bool address(uint32_t* sector) const;
    void advance();
    bool readSector(uint32_t sector);
    bool writeSector(uint32_t sector);
    void fail(uint8_t error);
    void transfer(bool in);

    std::string file_;
    std::fstream stream_;
    bool good_ = true;
    uint32_t sectors_ = 0;
    int cylinders_ = 0;
    // The shape the DOS last told it to take (Initialize Device Parameters).
    int heads_ = kHeads, perTrack_ = kSectorsPerTrack;

    uint8_t error_ = 1, features_ = 0, count_ = 1, sector_ = 1, cylinderLow_ = 0, cylinderHigh_ = 0, device_ = 0;
    uint8_t status_ = kReady | kSeekDone;
    uint8_t control_ = 0;
    uint8_t buffer_[kSectorSize] = {};
    int position_ = kSectorSize;  // the next byte of the buffer; at its end, no transfer
    enum class Transfer : uint8_t { None, Identify, Read, Write } transfer_ = Transfer::None;
};

}  // namespace tuxape
