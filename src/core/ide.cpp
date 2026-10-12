#include "core/ide.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace tuxape {

IdeDisc::IdeDisc(std::string file, uint64_t bytes)
    : file_(std::move(file))
{
    std::error_code none;
    const uint64_t there = std::filesystem::is_regular_file(file_, none) ? std::filesystem::file_size(file_, none) : 0;
    const uint64_t size = std::max(there, bytes);
    sectors_ = static_cast<uint32_t>(std::min<uint64_t>(size / kSectorSize, 0x0FFFFFFF));
    cylinders_ = static_cast<int>(std::min<uint32_t>(sectors_ / (kHeads * kSectorsPerTrack), 16383));
    if (there)
        stream_.open(file_, std::ios::in | std::ios::out | std::ios::binary);
    reset();
}

void IdeDisc::reset()
{
    // What a disc shows after its own check: all is well, and the
    // signature of a disc that is not a CD drive.
    error_ = 1;
    features_ = 0;
    count_ = 1;
    sector_ = 1;
    cylinderLow_ = cylinderHigh_ = 0;
    device_ = 0;
    status_ = kReady | kSeekDone;
    heads_ = kHeads;
    perTrack_ = kSectorsPerTrack;
    position_ = kSectorSize;
    transfer_ = Transfer::None;
}

void IdeDisc::writeControl(uint8_t value)
{
    if ((value & 0x04) && !(control_ & 0x04))
        reset();
    control_ = value;
}

uint8_t IdeDisc::read(int reg)
{
    // A drive that is not there leaves the lines alone: the slave.
    if (!selected())
        return 0;
    switch (reg & 7) {
    case 0: {
        if (transfer_ == Transfer::None || transfer_ == Transfer::Write || position_ >= kSectorSize)
            return 0;
        const uint8_t value = buffer_[position_++];
        if (position_ == kSectorSize)
            transfer(true);
        return value;
    }
    case 1: return error_;
    case 2: return count_;
    case 3: return sector_;
    case 4: return cylinderLow_;
    case 5: return cylinderHigh_;
    case 6: return device_;
    default: return status_;
    }
}

void IdeDisc::write(int reg, uint8_t value)
{
    switch (reg & 7) {
    case 0:
        if (!selected() || transfer_ != Transfer::Write || position_ >= kSectorSize)
            return;
        buffer_[position_++] = value;
        if (position_ == kSectorSize)
            transfer(false);
        break;
    case 1: features_ = value; break;
    case 2: count_ = value; break;
    case 3: sector_ = value; break;
    case 4: cylinderLow_ = value; break;
    case 5: cylinderHigh_ = value; break;
    case 6: device_ = value; break;
    default:
        if (selected())
            command(value);
        break;
    }
}

void IdeDisc::fail(uint8_t error)
{
    error_ = error;
    status_ = kReady | kSeekDone | kError;
    transfer_ = Transfer::None;
    position_ = kSectorSize;
}

// The sector the registers name, by its number or by cylinder, head and
// sector. False if the disc has no such sector.
bool IdeDisc::address(uint32_t* sector) const
{
    uint64_t at;
    if (device_ & 0x40) {
        at = static_cast<uint32_t>(device_ & 0x0F) << 24 | static_cast<uint32_t>(cylinderHigh_) << 16
             | static_cast<uint32_t>(cylinderLow_) << 8 | sector_;
    } else {
        if (sector_ == 0 || sector_ > perTrack_)
            return false;
        const uint32_t cylinder = static_cast<uint32_t>(cylinderHigh_) << 8 | cylinderLow_;
        at = (static_cast<uint64_t>(cylinder) * static_cast<unsigned>(heads_) + (device_ & 0x0Fu))
                 * static_cast<unsigned>(perTrack_)
             + (sector_ - 1u);
    }
    if (at >= sectors_)
        return false;
    *sector = static_cast<uint32_t>(at);
    return true;
}

// The registers move on to the sector after, as a disc's do.
void IdeDisc::advance()
{
    if (device_ & 0x40) {
        if (++sector_ == 0 && ++cylinderLow_ == 0 && ++cylinderHigh_ == 0)
            device_ = static_cast<uint8_t>((device_ & 0xF0) | ((device_ + 1) & 0x0F));
        return;
    }
    if (++sector_ <= perTrack_)
        return;
    sector_ = 1;
    const int head = (device_ & 0x0F) + 1;
    device_ = static_cast<uint8_t>((device_ & 0xF0) | (head < heads_ ? head : 0));
    if (head >= heads_ && ++cylinderLow_ == 0)
        ++cylinderHigh_;
}

bool IdeDisc::readSector(uint32_t sector)
{
    std::memset(buffer_, 0, sizeof buffer_);
    if (!stream_.is_open())
        return true;  // nothing written yet
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(sector) * kSectorSize);
    stream_.read(reinterpret_cast<char*>(buffer_), kSectorSize);
    // Past the file's end: zeros, for as much as was not there.
    if (stream_.gcount() < kSectorSize)
        std::memset(buffer_ + stream_.gcount(), 0, static_cast<size_t>(kSectorSize - stream_.gcount()));
    return !stream_.bad();
}

bool IdeDisc::writeSector(uint32_t sector)
{
    if (!stream_.is_open()) {
        // The first sector written makes the file.
        std::error_code none;
        const std::filesystem::path path(file_);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), none);
        std::ofstream(file_, std::ios::binary | std::ios::app).flush();
        stream_.open(file_, std::ios::in | std::ios::out | std::ios::binary);
        if (!stream_.is_open())
            return good_ = false;
    }
    stream_.clear();
    stream_.seekp(static_cast<std::streamoff>(sector) * kSectorSize);
    stream_.write(reinterpret_cast<const char*>(buffer_), kSectorSize);
    stream_.flush();
    if (!stream_)
        good_ = false;
    return static_cast<bool>(stream_);
}

// A sector's last byte has gone through the data register: the next one,
// if the command asked for more.
void IdeDisc::transfer(bool in)
{
    const Transfer kind = transfer_;
    transfer_ = Transfer::None;
    status_ = kReady | kSeekDone;
    if (kind == Transfer::Identify)
        return;
    uint32_t sector = 0;
    if (!in) {
        if (!address(&sector))
            return fail(kNotFound);
        if (!writeSector(sector))
            return fail(kAborted);
    }
    count_ = static_cast<uint8_t>(count_ - 1);
    if (count_ == 0)
        return;
    advance();
    if (!address(&sector))
        return fail(kNotFound);
    if (in && !readSector(sector))
        return fail(kAborted);
    transfer_ = kind;
    position_ = 0;
    status_ = kReady | kSeekDone | kDataRequest;
}

void IdeDisc::identify()
{
    std::memset(buffer_, 0, sizeof buffer_);
    const auto word = [&](int n, unsigned value) {
        buffer_[n * 2] = static_cast<uint8_t>(value);
        buffer_[n * 2 + 1] = static_cast<uint8_t>(value >> 8);
    };
    // Text is two characters to a word, the first in the high byte.
    const auto text = [&](int first, int words, const char* value) {
        const size_t length = std::strlen(value);
        for (int i = 0; i < words * 2; ++i)
            buffer_[first * 2 + (i ^ 1)] = static_cast<uint8_t>(static_cast<size_t>(i) < length ? value[i] : ' ');
    };
    const uint32_t byShape = static_cast<uint32_t>(cylinders_) * kHeads * kSectorsPerTrack;
    word(0, 0x0040);  // a fixed disc
    word(1, static_cast<unsigned>(cylinders_));
    word(3, kHeads);
    word(6, kSectorsPerTrack);
    text(10, 10, "TUXAPE00000001");
    text(23, 4, "1.0");
    text(27, 20, "X-MASS DOM (TuxAPE)");
    word(49, 0x0200);  // it can be asked for sectors by their number
    word(53, 0x0001);
    word(54, static_cast<unsigned>(cylinders_));
    word(55, static_cast<unsigned>(heads_));
    word(56, static_cast<unsigned>(perTrack_));
    word(57, byShape & 0xFFFF);
    word(58, byShape >> 16);
    word(60, sectors_ & 0xFFFF);
    word(61, sectors_ >> 16);
    transfer_ = Transfer::Identify;
    position_ = 0;
    status_ = kReady | kSeekDone | kDataRequest;
}

void IdeDisc::command(uint8_t value)
{
    error_ = 0;
    status_ = kReady | kSeekDone;
    transfer_ = Transfer::None;
    position_ = kSectorSize;
    uint32_t sector = 0;

    if (value == 0xEC)
        return identify();
    if (value == 0x20 || value == 0x21) {  // read sectors; a count of 0 is 256
        if (!address(&sector))
            return fail(kNotFound);
        if (!readSector(sector))
            return fail(kAborted);
        transfer_ = Transfer::Read;
        position_ = 0;
        status_ = kReady | kSeekDone | kDataRequest;
        return;
    }
    if (value == 0x30 || value == 0x31) {  // write sectors
        if (!address(&sector))
            return fail(kNotFound);
        transfer_ = Transfer::Write;
        position_ = 0;
        status_ = kReady | kSeekDone | kDataRequest;
        return;
    }
    if (value == 0x40 || value == 0x41 || (value & 0xF0) == 0x70) {  // verify, seek
        if (!address(&sector))
            return fail(kNotFound);
        return;
    }
    if (value == 0x91) {  // the shape to count cylinders, heads and sectors by
        if (count_ == 0)
            return fail(kAborted);
        perTrack_ = count_;
        heads_ = (device_ & 0x0F) + 1;
        return;
    }
    if (value == 0x90) {  // its own check: all is well
        error_ = 1;
        return;
    }
    // Asked to get ready, to rest, to hand over what it has kept back, to
    // take bytes eight at a time: done, or nothing to do.
    if ((value & 0xF0) == 0x10 || value == 0xEF || value == 0xE7 || value == 0xC6 || (value >= 0xE0 && value <= 0xE6)
        || (value >= 0x94 && value <= 0x99))
        return;
    fail(kAborted);
}

}  // namespace tuxape
