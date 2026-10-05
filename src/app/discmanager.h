#pragma once

#include <memory>

#include <QObject>
#include <QString>

#include "core/disc.h"

class Emulator;

// Looks after the discs in the emulated drives: which image file each one
// came from, loading and saving them, and the second disc each drive keeps
// ready for "Flip Disc".
class DiscManager : public QObject {
    Q_OBJECT

public:
    static constexpr int kDrives = 2;

    struct Info {
        bool present = false;
        bool modified = false;
        bool readOnly = false;  // the image file cannot be written
        QString path;
        QString description;
    };

    explicit DiscManager(Emulator* emulator, QObject* parent = nullptr);

    Info info(int drive) const;

    // Each of these returns an error message, or an empty string.
    // Whatever was in the drive is dropped: save it first if it matters.
    QString insert(int drive, const QString& path);
    QString createBlank(int drive, const QString& path, const tuxape::DiscFormat& format);
    // Writes the disc back to its image file if it has changed.
    QString save(int drive);

    void format(int drive, const tuxape::DiscFormat& format, bool clearUnused);
    void remove(int drive);
    // Exchanges the disc in the drive with the drive's spare.
    void flip(int drive);
    // Exchanges the discs in drives A and B.
    void swap();
    // True if the drive's spare disc has unsaved changes.
    bool spareModified(int drive) const;
    QString saveSpare(int drive);

    // "Allow Temporary Writes": discs from read-only files can be written
    // to in memory; the changes are thrown away.
    bool allowTemporaryWrites() const { return allowTemporaryWrites_; }
    void setAllowTemporaryWrites(bool allow);
    // "Prompt to Save Changes": ask before writing a changed disc back.
    bool promptToSave() const { return promptToSave_; }
    void setPromptToSave(bool prompt) { promptToSave_ = prompt; }
    // "Allow Single-sided read", per drive.
    bool singleSidedRead(int drive) const;
    void setSingleSidedRead(int drive, bool allow);

    // What the drive lights show: the active drive (or -1) and where each
    // head is.
    struct Activity {
        int activeDrive = -1;
        int cylinder[kDrives] = {};
    };
    Activity activity() const;

signals:
    // The contents of a drive changed.
    void changed();

private:
    struct Slot {
        std::unique_ptr<tuxape::Disc> disc;  // only used for the spare
        QString path;
        bool readOnly = false;
    };

    Emulator* emulator_;
    Slot current_[kDrives];  // the disc itself lives in the emulated drive
    Slot spare_[kDrives];
    bool allowTemporaryWrites_ = false;
    bool promptToSave_ = false;

    void place(int drive, std::unique_ptr<tuxape::Disc> disc, const QString& path, bool readOnly);
    static QString write(const tuxape::Disc& disc, const QString& path);
};
