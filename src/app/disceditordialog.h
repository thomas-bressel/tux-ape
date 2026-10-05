#pragma once

#include <memory>

#include <QDialog>

#include "debuggerdialog.h"

#include "core/discfiles.h"

class Emulator;
class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QTabWidget;
class QTreeWidget;

// WinAPE's disc editor, for the disc in a drive. The File Editor lists the
// disc's files, copies files to it from the host and from it to the host
// (with or without AMSDOS's header), renames and deletes them and sets
// their attributes. The Sector Editor shows any sector of any track, to be
// changed byte by byte.
class DiscEditorDialog : public QDialog {
    Q_OBJECT

public:
    DiscEditorDialog(Emulator* emulator, int drive, QWidget* parent = nullptr);

    // ---- The File Editor ----
    // The files listed, system files among them only when asked for.
    QStringList listedFiles() const;
    QString freeText() const;
    void setShowSystem(bool show);
    // "Add/Remove AMSDOS headers": a file brought from the host is given a
    // header if it has none, and one taken to the host loses its own.
    void setHeaders(bool on);
    // Each returns false, with a message for the user in `error`, if it
    // cannot be done.
    bool addFile(const QString& hostPath, QString* error = nullptr);
    bool extractFile(const QString& name, const QString& hostPath, QString* error = nullptr);
    bool renameFile(const QString& name, const QString& newName);
    bool deleteFile(const QString& name);
    bool setFileAttributes(const QString& name, bool readOnly, bool system);

    // ---- The Sector Editor ----
    // A sector by its track, side and place on the track. False if there
    // is none.
    bool selectSector(int track, int side, int index);
    QStringList sectorNames() const;  // of the track chosen: "C1", "C6"...
    QString sectorInfo() const;
    MemoryDumpView* sectorView() const { return sectorView_; }

private:
    Emulator* emulator_;
    int drive_;
    bool writable_ = false;
    bool showSystem_ = false;
    bool headers_ = true;
    DebugMemory sectorBytes_ = {};

    QTabWidget* tabs_;
    QTreeWidget* files_;
    QLabel* free_;
    QCheckBox* showSystemBox_;
    QCheckBox* headersBox_;
    QSpinBox* track_;
    QSpinBox* side_;
    QComboBox* sector_;
    QLabel* sectorInfo_;
    MemoryDumpView* sectorView_;

    // Runs `work` on the disc's files with the machine held. False if the
    // drive is empty or the disc's format is not known.
    template <class Work>
    bool withFiles(Work&& work) const;
    std::optional<tuxape::DiscFile> fileNamed(const QString& name) const;
    void fillFiles();
    void fillSectors();
    void showSector();
    void addClicked();
    void extractClicked();
    void renameClicked();
    void deleteClicked();
    void propertiesClicked();
    QStringList selectedFiles() const;
};
