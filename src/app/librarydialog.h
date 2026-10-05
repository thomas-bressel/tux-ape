#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

// One program of the library: an image file, and what its name says of it.
struct LibraryEntry {
    enum Kind { Disc, Snapshot, Tape };
    QString path;
    Kind kind = Disc;
    QString title;
    QString year;     // four digits, or empty
    QString details;  // the other notes of the name, e.g. "UK, CPM, Original"
};

// What a file name says. Collections name their files in the manner of
// TOSEC, "Gryzor (1987)(Ocean)(fr)[cr].dsk": a title, then notes in round
// and square brackets, the year among them.
LibraryEntry libraryEntry(const QString& path);
// The disc images, tapes and snapshots (.dsk, .cdt, .sna) in the folders and
// their sub-folders, in the order of their titles.
QList<LibraryEntry> scanLibrary(const QStringList& folders);

// The Library window, TuxAPE's own (WinAPE has none): the programs found
// in the folders the user has named, a box to search them, and a double
// click to put one in the machine.
class LibraryDialog : public QDialog {
    Q_OBJECT

public:
    explicit LibraryDialog(const QStringList& folders, QWidget* parent = nullptr);

    // The folders looked in. Setting them reads the folders again.
    QStringList folders() const { return folders_; }
    void setFolders(const QStringList& folders);

    // Only the programs whose title, year, notes or file name hold every
    // word of the search are listed.
    QString search() const;
    void setSearch(const QString& text);
    QStringList listedTitles() const;
    bool select(const QString& title);

    // Closes the window on the program selected, as a double click does
    // (a disc goes in drive A) or the button for drive B. False if there is
    // none, or if it is not a disc and drive B is asked for.
    bool choose(int drive = 0);
    // What the window was closed on, and the drive a disc is to go in.
    const LibraryEntry* chosen() const { return chosen_ >= 0 ? &entries_[chosen_] : nullptr; }
    int drive() const { return drive_; }

private:
    QStringList folders_;
    QList<LibraryEntry> entries_;
    int chosen_ = -1;
    int drive_ = 0;

    QLineEdit* search_;
    QTreeWidget* list_;
    QLabel* count_;
    QPushButton* insertA_;
    QPushButton* insertB_;

    int current() const;
    void fill();
    void filter();
    void updateButtons();
    void editFolders();
};
