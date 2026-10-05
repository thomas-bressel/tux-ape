#include "discmanager.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "emulator.h"

using tuxape::Cpc;
using tuxape::Disc;

DiscManager::DiscManager(Emulator* emulator, QObject* parent)
    : QObject(parent)
    , emulator_(emulator)
{
}

DiscManager::Info DiscManager::info(int drive) const
{
    Info info;
    info.path = current_[drive].path;
    info.readOnly = current_[drive].readOnly;
    emulator_->withMachine([&](Cpc& cpc) {
        if (const Disc* disc = cpc.fdc().drive(drive).disc.get()) {
            info.present = true;
            info.modified = disc->modified;
            info.description = QString::fromStdString(disc->describe());
        }
    });
    return info;
}

void DiscManager::place(int drive, std::unique_ptr<Disc> disc, const QString& path, bool readOnly)
{
    if (disc)
        disc->writeProtected = readOnly && !allowTemporaryWrites_;
    current_[drive].path = path;
    current_[drive].readOnly = readOnly;
    emulator_->withMachine([&](Cpc& cpc) { cpc.fdc().drive(drive).disc = std::move(disc); });
    emit changed();
}

QString DiscManager::insert(int drive, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return tr("Cannot open %1: %2").arg(path, file.errorString());
    const QByteArray bytes = file.readAll();
    auto disc = Disc::fromDsk({reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size())});
    if (!disc)
        return tr("%1 is not a disc image TuxAPE can read.").arg(QFileInfo(path).fileName());
    place(drive, std::make_unique<Disc>(std::move(*disc)), path, !QFileInfo(path).isWritable());
    return {};
}

QString DiscManager::createBlank(int drive, const QString& path, const tuxape::DiscFormat& format)
{
    auto disc = std::make_unique<Disc>();
    disc->format(format);
    disc->modified = false;
    if (const QString error = write(*disc, path); !error.isEmpty())
        return error;
    place(drive, std::move(disc), path, false);
    return {};
}

QString DiscManager::write(const Disc& disc, const QString& path)
{
    const std::vector<uint8_t> bytes = disc.toDsk();
    // Written to a temporary file and renamed, so a failure part-way
    // cannot destroy the existing image.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) < 0
        || !file.commit())
        return tr("Cannot write %1: %2").arg(path, file.errorString());
    return {};
}

QString DiscManager::save(int drive)
{
    // Changes to a disc from a read-only file are temporary by definition.
    if (current_[drive].readOnly || current_[drive].path.isEmpty())
        return {};
    return emulator_->withMachine([&](Cpc& cpc) -> QString {
        Disc* disc = cpc.fdc().drive(drive).disc.get();
        if (!disc || !disc->modified)
            return {};
        const QString error = write(*disc, current_[drive].path);
        if (error.isEmpty())
            disc->modified = false;
        return error;
    });
}

void DiscManager::format(int drive, const tuxape::DiscFormat& format, bool clearUnused)
{
    emulator_->withMachine([&](Cpc& cpc) {
        if (Disc* disc = cpc.fdc().drive(drive).disc.get())
            disc->format(format, clearUnused);
    });
    emit changed();
}

void DiscManager::remove(int drive)
{
    place(drive, nullptr, {}, false);
}

void DiscManager::flip(int drive)
{
    emulator_->withMachine([&](Cpc& cpc) { std::swap(cpc.fdc().drive(drive).disc, spare_[drive].disc); });
    std::swap(current_[drive].path, spare_[drive].path);
    std::swap(current_[drive].readOnly, spare_[drive].readOnly);
    emit changed();
}

void DiscManager::swap()
{
    emulator_->withMachine([](Cpc& cpc) { std::swap(cpc.fdc().drive(0).disc, cpc.fdc().drive(1).disc); });
    std::swap(current_[0].path, current_[1].path);
    std::swap(current_[0].readOnly, current_[1].readOnly);
    emit changed();
}

bool DiscManager::spareModified(int drive) const
{
    return spare_[drive].disc && spare_[drive].disc->modified && !spare_[drive].readOnly;
}

QString DiscManager::saveSpare(int drive)
{
    if (!spareModified(drive) || spare_[drive].path.isEmpty())
        return {};
    const QString error = write(*spare_[drive].disc, spare_[drive].path);
    if (error.isEmpty())
        spare_[drive].disc->modified = false;
    return error;
}

void DiscManager::setAllowTemporaryWrites(bool allow)
{
    allowTemporaryWrites_ = allow;
    emulator_->withMachine([&](Cpc& cpc) {
        for (int drive = 0; drive < kDrives; ++drive)
            if (Disc* disc = cpc.fdc().drive(drive).disc.get())
                disc->writeProtected = current_[drive].readOnly && !allow;
    });
    for (int drive = 0; drive < kDrives; ++drive)
        if (spare_[drive].disc)
            spare_[drive].disc->writeProtected = spare_[drive].readOnly && !allow;
}

bool DiscManager::singleSidedRead(int drive) const
{
    return emulator_->withMachine([&](Cpc& cpc) { return cpc.fdc().drive(drive).singleSidedRead; });
}

void DiscManager::setSingleSidedRead(int drive, bool allow)
{
    emulator_->withMachine([&](Cpc& cpc) { cpc.fdc().drive(drive).singleSidedRead = allow; });
}

DiscManager::Activity DiscManager::activity() const
{
    return emulator_->withMachine([](Cpc& cpc) {
        Activity activity;
        activity.activeDrive = cpc.fdc().activeDrive();
        for (int drive = 0; drive < kDrives; ++drive)
            activity.cylinder[drive] = cpc.fdc().drive(drive).cylinder;
        return activity;
    });
}
