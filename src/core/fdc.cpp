#include "core/fdc.h"

#include <algorithm>
#include <cstdlib>

namespace tuxape {

namespace {

// At 250 kbit/s a byte takes 32 microseconds and a revolution 200 ms.
constexpr uint64_t kByteTime = 32;
constexpr uint64_t kRevolution = DiscTrack::kLength * kByteTime;
constexpr int kLastCylinder = Disc::kMaxCylinders - 1;
constexpr int kRecalibrateSteps = 77;

enum : uint8_t {
    St0Abnormal = 0x40,
    St0Invalid = 0x80,
    St0SeekEnd = 0x20,
    St0EquipmentCheck = 0x10,
    St0NotReady = 0x08,
};
enum : uint8_t {
    St1EndOfCylinder = 0x80,
    St1DataError = 0x20,
    St1Overrun = 0x10,
    St1NoData = 0x04,
    St1NotWritable = 0x02,
    St1MissingAddressMark = 0x01,
};
enum : uint8_t {
    St2ControlMark = 0x40,
    St2DataError = 0x20,
    St2WrongCylinder = 0x10,
    St2ScanEqual = 0x08,
    St2ScanNotSatisfied = 0x04,
    St2BadCylinder = 0x02,
    St2MissingDataMark = 0x01,
};

enum : uint8_t {
    CmdReadTrack = 0x02,
    CmdSpecify = 0x03,
    CmdSenseDrive = 0x04,
    CmdWriteData = 0x05,
    CmdReadData = 0x06,
    CmdRecalibrate = 0x07,
    CmdSenseInterrupt = 0x08,
    CmdWriteDeleted = 0x09,
    CmdReadId = 0x0A,
    CmdReadDeleted = 0x0C,
    CmdFormat = 0x0D,
    CmdSeek = 0x0F,
    CmdScanEqual = 0x11,
    CmdScanLowOrEqual = 0x19,
    CmdScanHighOrEqual = 0x1D,
};

int commandLength(uint8_t first)
{
    switch (first & 0x1F) {
    case CmdReadTrack: case CmdWriteData: case CmdReadData: case CmdWriteDeleted: case CmdReadDeleted:
    case CmdScanEqual: case CmdScanLowOrEqual: case CmdScanHighOrEqual:
        return 9;
    case CmdFormat: return 6;
    case CmdSpecify: case CmdSeek: return 3;
    case CmdSenseDrive: case CmdRecalibrate: case CmdReadId: return 2;
    default: return 1;
    }
}

}  // namespace

void Fdc::reset()
{
    motor_ = false;
    stepRate_ = 0;
    phase_ = Phase::Command;
    exec_ = Exec::None;
    commandCount_ = 0;
    resultCount_ = resultLength_ = 0;
    byteReady_ = false;
    unit_ = head_ = 0;
    for (int u = 0; u < kDrives; ++u) {
        seeking_[u] = false;
        interruptPending_[u] = false;
        presentCylinder_[u] = 0;
    }
}

bool Fdc::ready(int unit) const
{
    return motor_ && drives_[physicalDrive(unit)].disc != nullptr;
}

DiscTrack* Fdc::currentTrack()
{
    Drive& drive = currentDrive();
    if (!drive.disc)
        return nullptr;
    DiscTrack* track = drive.disc->track(drive.cylinder, head_);
    if ((!track || !track->formatted()) && head_ == 1 && drive.singleSidedRead)
        track = drive.disc->track(drive.cylinder, 0);
    return track;
}

DiscSector* Fdc::currentSector()
{
    DiscTrack* track = ready(unit_) ? currentTrack() : nullptr;
    if (!track || sectorIndex_ < 0 || sectorIndex_ >= static_cast<int>(track->sectors.size()))
        return nullptr;
    return &track->sectors[static_cast<size_t>(sectorIndex_)];
}

void Fdc::notReady(uint64_t now)
{
    st0_ |= St0NotReady;
    finish(now, 0);
}

// Time until a position on the track next passes under the head.
uint64_t Fdc::delayTo(int position, uint64_t now) const
{
    const uint64_t angle = (now - motorOnTime_) % kRevolution;
    const uint64_t target = static_cast<uint64_t>(position) * kByteTime;
    return target > angle ? target - angle : target + kRevolution - angle;
}

uint64_t Fdc::stepTime() const
{
    // The step rate is programmed in units that are 2 ms with the CPC's
    // 4 MHz controller clock.
    return fast_ ? 400 : static_cast<uint64_t>(16 - stepRate_) * 2000;
}

void Fdc::writeMotor(uint8_t value, uint64_t now)
{
    const bool on = value & 1;
    if (on && !motor_)
        motorOnTime_ = now;
    motor_ = on;
}

uint8_t Fdc::readStatus(uint64_t now)
{
    update(now);
    uint8_t status = 0;
    for (int u = 0; u < kDrives; ++u)
        if (seeking_[u])
            status |= static_cast<uint8_t>(1 << u);
    switch (phase_) {
    case Phase::Command:
        status |= 0x80;
        if (commandCount_ > 0)
            status |= 0x10;
        break;
    case Phase::Execute:
        // Busy, and "execution mode" for the commands that move data.
        status |= 0x10;
        if (exec_ != Exec::ReadId)
            status |= 0x20;
        if (op_ == Op::Read || op_ == Op::ReadTrack)
            status |= 0x40;
        if (byteReady_)
            status |= 0x80;
        break;
    case Phase::Result:
        status |= 0xD0;
        break;
    }
    return status;
}

uint8_t Fdc::readData(uint64_t now)
{
    update(now);
    if (phase_ == Phase::Result) {
        const uint8_t value = result_[resultCount_++];
        if (resultCount_ == resultLength_)
            idle();
        return value;
    }
    if (phase_ == Phase::Execute && exec_ == Exec::Transfer && byteReady_
        && (op_ == Op::Read || op_ == Op::ReadTrack))
        return takeByte(now);
    return 0xFF;
}

void Fdc::writeData(uint8_t value, uint64_t now)
{
    update(now);
    switch (phase_) {
    case Phase::Command:
        if (commandCount_ == 0)
            commandLength_ = commandLength(value);
        command_[commandCount_++] = value;
        if (commandCount_ == commandLength_)
            startCommand(now);
        break;
    case Phase::Execute:
        if (!byteReady_)
            break;
        if (exec_ == Exec::Transfer && (op_ == Op::Write || op_ == Op::Scan)) {
            giveByte(value, now);
        } else if (exec_ == Exec::FormatIds) {
            buffer_.push_back(value);
            if (buffer_.size() == static_cast<size_t>(sectorsLeft_) * 4)
                finishFormat(now);
        }
        break;
    case Phase::Result:
        break;
    }
}

void Fdc::update(uint64_t now)
{
    for (int u = 0; u < kDrives; ++u) {
        if (seeking_[u] && now >= seekDone_[u]) {
            seeking_[u] = false;
            interruptPending_[u] = true;
        }
    }

    // One event can lead straight to the next, hence the loop.
    for (;;) {
        switch (exec_) {
        case Exec::Search:
            if (now < eventTime_)
                return;
            startTransfer(eventTime_);
            continue;
        case Exec::Transfer:
            if (!byteReady_ && now >= byteTime_)
                byteReady_ = true;
            // A byte not collected before the next one arrives is lost.
            if (byteReady_ && !fast_ && now >= byteTime_ + kByteTime) {
                st1_ |= St1Overrun;
                finish(now, 0);
            }
            return;
        case Exec::ReadId: {
            if (now < eventTime_)
                return;
            const DiscSector* sector = currentSector();
            if (!sector) {
                notReady(now);
                return;
            }
            c_ = sector->c;
            h_ = sector->h;
            r_ = sector->r;
            n_ = sector->n;
            enterResult();
            return;
        }
        case Exec::FormatWait:
            if (now < eventTime_)
                return;
            exec_ = Exec::FormatIds;
            byteReady_ = true;
            byteTime_ = eventTime_;
            return;
        case Exec::Finish:
            if (now < eventTime_)
                return;
            enterResult();
            return;
        default:
            return;
        }
    }
}

void Fdc::startCommand(uint64_t now)
{
    const uint8_t code = command_[0] & 0x1F;
    st0_ = st1_ = st2_ = 0;
    stopAfterSector_ = false;
    op_ = Op::None;
    if (commandLength_ > 1 && code != CmdSpecify) {
        unit_ = command_[1] & 3;
        head_ = (command_[1] >> 2) & 1;
    }

    switch (code) {
    case CmdSpecify:
        stepRate_ = command_[1] >> 4;
        idle();
        break;

    case CmdSenseDrive: {
        const Drive& drive = currentDrive();
        uint8_t st3 = static_cast<uint8_t>(unit_ | head_ << 2);
        if (drive.cylinder == 0)
            st3 |= 0x10;
        if (ready(unit_))
            st3 |= 0x20;
        if (!drive.disc || drive.disc->writeProtected)
            st3 |= 0x40;
        result_[0] = st3;
        setResult(1);
        break;
    }

    case CmdRecalibrate:
        startSeek(unit_, 0, true, now);
        idle();
        break;
    case CmdSeek:
        startSeek(unit_, command_[2], false, now);
        idle();
        break;

    case CmdSenseInterrupt:
        for (int u = 0; u < kDrives; ++u) {
            if (interruptPending_[u]) {
                interruptPending_[u] = false;
                result_[0] = interruptStatus_[u];
                result_[1] = presentCylinder_[u];
                setResult(2);
                return;
            }
        }
        result_[0] = St0Invalid;
        setResult(1);
        break;

    case CmdReadId:
        startReadId(now);
        break;

    case CmdReadData: case CmdReadDeleted:
        op_ = Op::Read;
        startReadWrite(now);
        break;
    case CmdWriteData: case CmdWriteDeleted:
        op_ = Op::Write;
        startReadWrite(now);
        break;
    case CmdReadTrack:
        op_ = Op::ReadTrack;
        startReadWrite(now);
        break;
    case CmdScanEqual: case CmdScanLowOrEqual: case CmdScanHighOrEqual:
        op_ = Op::Scan;
        startReadWrite(now);
        break;

    case CmdFormat:
        startFormat(now);
        break;

    default:
        result_[0] = St0Invalid;
        setResult(1);
        break;
    }
}

void Fdc::startSeek(int unit, int target, bool recalibrate, uint64_t now)
{
    Drive& drive = drives_[physicalDrive(unit)];
    uint8_t status = static_cast<uint8_t>(St0SeekEnd | unit);
    int steps = 0;

    if (!ready(unit)) {
        status |= St0Abnormal | St0NotReady;
    } else if (recalibrate) {
        // Step out until the track 0 sensor trips, but no more than 77 times.
        steps = std::min(drive.cylinder, kRecalibrateSteps);
        drive.cylinder -= steps;
        if (drive.cylinder != 0)
            status |= St0Abnormal | St0EquipmentCheck;
        presentCylinder_[unit] = 0;
    } else {
        // The controller steps by the difference with where it believes
        // the head is, which need not be where the head really is.
        const int delta = target - presentCylinder_[unit];
        steps = std::abs(delta);
        drive.cylinder = std::clamp(drive.cylinder + delta, 0, kLastCylinder);
        presentCylinder_[unit] = static_cast<uint8_t>(target);
    }

    seeking_[unit] = true;
    seekDone_[unit] = now + static_cast<uint64_t>(steps) * stepTime();
    interruptPending_[unit] = false;
    interruptStatus_[unit] = status;
}

void Fdc::startReadWrite(uint64_t now)
{
    phase_ = Phase::Execute;
    c_ = command_[2];
    h_ = command_[3];
    r_ = command_[4];
    n_ = command_[5];

    if (!ready(unit_)) {
        notReady(now);
        return;
    }
    if (op_ == Op::Write && currentDrive().disc->writeProtected) {
        st1_ |= St1NotWritable;
        finish(now, 0);
        return;
    }
    sectorIndex_ = 0;
    sectorsLeft_ = command_[6];
    searchSector(now);
}

void Fdc::searchSector(uint64_t now)
{
    Drive& drive = currentDrive();
    DiscTrack* track = currentTrack();
    if (!track || !track->formatted()) {
        // Nothing to find; the controller gives up after two index holes.
        st1_ |= St1MissingAddressMark;
        finish(now, fast_ ? 0 : delayTo(0, now) + kRevolution);
        return;
    }
    const int count = static_cast<int>(track->sectors.size());

    if (op_ == Op::ReadTrack) {
        // Sectors are taken as they come after the index hole, whatever
        // their IDs say.
        if (sectorIndex_ >= count) {
            st1_ |= St1EndOfCylinder;
            finish(now, 0);
            return;
        }
        const int position = track->dataPosition[static_cast<size_t>(sectorIndex_)];
        exec_ = Exec::Search;
        if (fast_)
            eventTime_ = now;
        else if (sectorIndex_ == 0)
            eventTime_ = now + delayTo(0, now) + static_cast<uint64_t>(position) * kByteTime;
        else
            eventTime_ = now + delayTo(position, now);
        return;
    }

    // Start with the sector whose ID comes round first.
    int first = 0;
    if (fast_) {
        first = drive.nextSector % count;
    } else {
        uint64_t soonest = UINT64_MAX;
        for (int i = 0; i < count; ++i) {
            const uint64_t delay = delayTo(track->idPosition[static_cast<size_t>(i)], now);
            if (delay < soonest) {
                soonest = delay;
                first = i;
            }
        }
    }

    for (int i = 0; i < count; ++i) {
        const int index = (first + i) % count;
        const DiscSector& sector = track->sectors[static_cast<size_t>(index)];
        // A CRC error flagged without the "in data" bit is in the ID field:
        // the controller cannot recognise that sector at all.
        const bool idError = (sector.st1 & St1DataError) && !(sector.st2 & St2DataError);
        if (sector.c == c_ && sector.h == h_ && sector.r == r_ && sector.n == n_ && !idError) {
            sectorIndex_ = index;
            drive.nextSector = index + 1;
            exec_ = Exec::Search;
            const size_t at = static_cast<size_t>(index);
            eventTime_ = fast_ ? now
                               : now + delayTo(track->idPosition[at], now)
                                     + static_cast<uint64_t>(track->dataPosition[at] - track->idPosition[at])
                                           * kByteTime;
            return;
        }
        if (sector.r == r_ && sector.c != c_) {
            st2_ |= St2WrongCylinder;
            if (sector.c == 0xFF)
                st2_ |= St2BadCylinder;
        }
    }

    st1_ |= St1NoData;
    finish(now, fast_ ? 0 : delayTo(0, now) + kRevolution);
}

int Fdc::transferLength() const
{
    // With N = 0 the length comes from the DTL byte, up to the 128 bytes
    // such a sector holds.
    if (n_ == 0)
        return std::clamp<int>(command_[8], 1, 128);
    return 128 << std::min<int>(n_, 8);
}

void Fdc::startTransfer(uint64_t now)
{
    DiscSector* found = currentSector();
    if (!found) {
        notReady(now);
        return;
    }
    DiscSector& sector = *found;

    if (op_ == Op::Read) {
        // Read Data meeting a deleted sector, or Read Deleted Data meeting
        // a normal one: skip it if asked to, otherwise read it, flag it,
        // and stop there.
        const bool wantDeleted = (command_[0] & 0x1F) == CmdReadDeleted;
        if (sector.deleted() != wantDeleted) {
            if (command_[0] & 0x20) {
                if (r_ == command_[6]) {
                    st1_ |= St1EndOfCylinder;
                    finish(now, 0);
                } else {
                    ++r_;
                    searchSector(now);
                }
                return;
            }
            st2_ |= St2ControlMark;
            stopAfterSector_ = true;
        }
    }

    buffer_.assign(static_cast<size_t>(transferLength()), 0);
    if (op_ == Op::Read || op_ == Op::ReadTrack) {
        // Asking for more than was recorded reads on into whatever follows;
        // here, the same bytes again.
        const int size = sector.copySize();
        if (size > 0) {
            const uint8_t* copy = &sector.data[static_cast<size_t>(sector.nextCopy % sector.copies()) * size];
            for (size_t i = 0; i < buffer_.size(); ++i)
                buffer_[i] = copy[i % static_cast<size_t>(size)];
        }
        sector.nextCopy = (sector.nextCopy + 1) % sector.copies();
    }
    bufferPos_ = 0;
    byteReady_ = true;
    byteTime_ = now;
    exec_ = Exec::Transfer;
}

uint8_t Fdc::takeByte(uint64_t now)
{
    const uint8_t value = buffer_[bufferPos_++];
    byteReady_ = false;
    byteTime_ = fast_ ? now : byteTime_ + kByteTime;
    if (bufferPos_ == buffer_.size())
        sectorDone(now);
    else if (now >= byteTime_)
        byteReady_ = true;
    return value;
}

void Fdc::giveByte(uint8_t value, uint64_t now)
{
    buffer_[bufferPos_++] = value;
    byteReady_ = false;
    byteTime_ = fast_ ? now : byteTime_ + kByteTime;
    if (bufferPos_ == buffer_.size())
        sectorDone(now);
    else if (now >= byteTime_)
        byteReady_ = true;
}

void Fdc::sectorDone(uint64_t now)
{
    byteReady_ = false;
    DiscSector* found = currentSector();
    if (!found) {
        notReady(now);
        return;
    }
    Drive& drive = currentDrive();
    DiscTrack* track = currentTrack();
    DiscSector& sector = *found;

    switch (op_) {
    case Op::None:
        break;
    case Op::Read:
    case Op::ReadTrack:
        // A CRC error only shows once the whole sector has been read.
        if (sector.st1 & St1DataError) {
            st1_ |= St1DataError;
            st2_ |= sector.st2 & St2DataError;
        }
        if (op_ == Op::ReadTrack && sector.r != r_)
            st1_ |= St1NoData;
        break;

    case Op::Write: {
        const size_t before = sector.data.size();
        sector.data = buffer_;
        if (sector.data.size() < static_cast<size_t>(sector.nominalSize()))
            sector.data.resize(static_cast<size_t>(sector.nominalSize()), track->filler);
        sector.st1 &= ~St1DataError;
        sector.st2 &= ~(St2DataError | St2ControlMark);
        if ((command_[0] & 0x1F) == CmdWriteDeleted)
            sector.st2 |= St2ControlMark;
        sector.nextCopy = 0;
        if (sector.data.size() != before)
            track->updateLayout();
        drive.disc->modified = true;
        break;
    }

    case Op::Scan: {
        // Compare what the CPU sent with the sector; FF from the CPU
        // matches anything.
        const uint8_t code = command_[0] & 0x1F;
        bool equal = true, satisfied = true;
        const size_t size = std::min(buffer_.size(), sector.data.size());
        for (size_t i = 0; i < size && satisfied; ++i) {
            const uint8_t cpu = buffer_[i], disc = sector.data[i];
            if (cpu == 0xFF || cpu == disc)
                continue;
            equal = false;
            satisfied = code == CmdScanLowOrEqual ? disc < cpu : code == CmdScanHighOrEqual ? disc > cpu : false;
        }
        if (satisfied) {
            if (equal)
                st2_ |= St2ScanEqual;
            finish(now, 0);
            return;
        }
        // Not this one: step on by the amount given in place of DTL.
        const int next = r_ + std::max<int>(command_[8], 1);
        if (next > command_[6]) {
            st2_ |= St2ScanNotSatisfied;
            finish(now, 0);
            return;
        }
        r_ = static_cast<uint8_t>(next);
        searchSector(now);
        return;
    }
    }

    if (st1_ & St1DataError) {
        finish(now, 0);
        return;
    }
    if (op_ == Op::ReadTrack) {
        if (--sectorsLeft_ <= 0) {
            finish(now, 0);
            return;
        }
        ++sectorIndex_;
        searchSector(now);
        return;
    }
    // The CPC never signals "terminal count", so a transfer that reaches
    // its last sector always ends with End of Cylinder; software expects it.
    if (stopAfterSector_ || r_ == command_[6]) {
        st1_ |= St1EndOfCylinder;
        finish(now, 0);
        return;
    }
    ++r_;
    searchSector(now);
}

void Fdc::startReadId(uint64_t now)
{
    phase_ = Phase::Execute;
    if (!ready(unit_)) {
        notReady(now);
        return;
    }
    Drive& drive = currentDrive();
    DiscTrack* track = currentTrack();
    if (!track || !track->formatted()) {
        st1_ |= St1MissingAddressMark;
        finish(now, fast_ ? 0 : delayTo(0, now) + kRevolution);
        return;
    }
    const int count = static_cast<int>(track->sectors.size());
    uint64_t delay = 0;
    if (fast_) {
        sectorIndex_ = drive.nextSector % count;
    } else {
        delay = UINT64_MAX;
        for (int i = 0; i < count; ++i) {
            const uint64_t d = delayTo(track->idPosition[static_cast<size_t>(i)], now);
            if (d < delay) {
                delay = d;
                sectorIndex_ = i;
            }
        }
    }
    drive.nextSector = sectorIndex_ + 1;
    exec_ = Exec::ReadId;
    eventTime_ = now + delay;
}

void Fdc::startFormat(uint64_t now)
{
    phase_ = Phase::Execute;
    op_ = Op::Write;
    n_ = command_[2];
    sectorsLeft_ = command_[3];
    c_ = h_ = r_ = 0;
    buffer_.clear();

    if (!ready(unit_)) {
        notReady(now);
        return;
    }
    if (currentDrive().disc->writeProtected) {
        st1_ |= St1NotWritable;
        finish(now, 0);
        return;
    }
    if (sectorsLeft_ == 0) {
        finishFormat(now);
        return;
    }
    // Writing starts at the index hole.
    exec_ = Exec::FormatWait;
    eventTime_ = fast_ ? now : now + delayTo(0, now);
}

void Fdc::finishFormat(uint64_t now)
{
    Drive& drive = currentDrive();
    if (!ready(unit_)) {
        notReady(now);
        return;
    }
    drive.disc->formatTrack(drive.cylinder, head_, buffer_, command_[4], command_[5]);
    if (buffer_.size() >= 4) {
        c_ = buffer_[buffer_.size() - 4];
        h_ = buffer_[buffer_.size() - 3];
        r_ = buffer_[buffer_.size() - 2];
    }
    // The track is complete when the index hole comes round again.
    finish(now, fast_ ? 0 : delayTo(0, now));
}

void Fdc::finish(uint64_t now, uint64_t delay)
{
    byteReady_ = false;
    if (delay == 0) {
        enterResult();
        return;
    }
    exec_ = Exec::Finish;
    eventTime_ = now + delay;
}

void Fdc::enterResult()
{
    uint8_t st0 = st0_;
    const bool failed = st0 || st1_
                        || (st2_ & (St2DataError | St2WrongCylinder | St2BadCylinder | St2MissingDataMark
                                    | St2ScanNotSatisfied));
    if (failed)
        st0 |= St0Abnormal;
    result_[0] = static_cast<uint8_t>(st0 | head_ << 2 | unit_);
    result_[1] = st1_;
    result_[2] = st2_;
    result_[3] = c_;
    result_[4] = h_;
    result_[5] = r_;
    result_[6] = n_;
    setResult(7);
}

void Fdc::setResult(int length)
{
    phase_ = Phase::Result;
    exec_ = Exec::None;
    byteReady_ = false;
    resultLength_ = length;
    resultCount_ = 0;
    commandCount_ = 0;
}

void Fdc::idle()
{
    phase_ = Phase::Command;
    exec_ = Exec::None;
    byteReady_ = false;
    commandCount_ = 0;
}

}  // namespace tuxape
