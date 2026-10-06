#pragma once

#include <optional>
#include <vector>

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
class QTreeWidget;

// One program of the library: an image file, by itself or inside a ZIP
// archive, and what its name says of it.
struct LibraryEntry {
    enum Kind { Disc, Snapshot, Tape, Cartridge, Session };
    QString path;
    QString member;  // the file's name inside the archive `path`; empty for a file of its own
    Kind kind = Disc;
    QString title;
    QString year;     // four digits, or empty
    QString details;  // the other notes of the name, e.g. "UK, CPM, Original"
    bool ai = false;  // the name carries the note "(AI)"
    QString release;  // "Original", "Crack", "Hack", "File" or "<machine> Port" if the name says so, or empty
    QString category;     // the category folder it is filed under ("Games"...), or empty
    QString subcategory;  // the folder under that ("Racing"...), or empty
};

// The categories programs are filed under: the names of the folders a
// library folder may hold ("Games", "Educational", "Utilities", "Demos",
// "Compilations", "Miscellaneous"), each with sub-categories as folders of
// its own, and "SNR", for recorded sessions. A library folder may also be
// one of them itself.
QStringList libraryCategories();
// What a category's tab says: its folder's name, but "Let's Play" for the
// sessions of "SNR".
QString libraryCategoryTitle(const QString& category);

// What a file name says. Collections name their files in the manner of
// TOSEC, "Gryzor (1987)(Ocean)(fr)[cr].dsk": a title, then notes in round
// and square brackets, the year among them. The note "(AI)" is the user's
// mark for a program made with the help of an AI: it has a column of its
// own, and is left out of the other notes. So has the kind of release:
// "Original", "Crack" (also "Cracked", or TOSEC's "cr"), "Hack" (also
// "Hacked", or TOSEC's "h"), "File", for a program that only ever came
// out as a file, never on a disc or a tape of its own, or, for one brought
// over from another machine, any note that ends in "Port": "[Atari ST
// Port]", "[Amiga Port]", "[MSX Port]"...
LibraryEntry libraryEntry(const QString& path);
// The disc images, tapes, cartridges, snapshots and recorded sessions
// (.dsk, .cdt, .cpr, .sna, .snr)
// in the folders and their sub-folders, those inside ZIP archives included,
// in the order of their titles. An archive that holds a single program gives it its name;
// the programs of one that holds several go by their own. The folders a
// file is in give its category and sub-category.
QList<LibraryEntry> scanLibrary(const QStringList& folders);
// The folder the Library looks in when the user has named none: the one
// TUXAPE_LIBRARY_DIR gives, or else a "library" folder beside the program,
// or the project's own. Empty if there is no such folder.
QString defaultLibraryFolder();
// The folders to look in: those the user has named, as long as one of them
// is there; otherwise the Library's own, if it exists.
QStringList libraryFoldersOrDefault(const QStringList& named);
// What the program's file holds. Nothing if it cannot be read.
std::optional<std::vector<uint8_t>> libraryData(const LibraryEntry& entry);
// The program's file as shown to the user: "archive.zip » file.dsk".
QString libraryDisplayPath(const LibraryEntry& entry);

// The Library window, TuxAPE's own (WinAPE has none): the programs found
// in the folders the user has named, a tab for each category with a
// column for the sub-category, a box to search them, and a double click
// to put one in the machine.
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
    // The tabs: the categories that have a folder, in their own order,
    // then "Unsorted" if some programs are in none. With that one alone
    // there is no tab to show. The window opens on the first that has
    // programs.
    QStringList categories() const;
    // The tab in front, "Unsorted" being the empty name; false if there
    // is no such tab.
    QString category() const;
    bool setCategory(const QString& name);
    // The list's order: a click on a column's heading sorts by it from A
    // to Z, a second click from Z to A. By title to start with.
    int sortColumn() const;
    Qt::SortOrder sortOrder() const;
    void setSort(int column, Qt::SortOrder order);
    QStringList listedTitles() const;
    QStringList listedSubcategories() const;
    // The Type column shows pictures; these are the names behind them:
    // "Disc", "Tape", "Cartridge" or "Snapshot".
    QStringList listedTypes() const;
    // Those of them whose AI box is ticked.
    QStringList listedAiTitles() const;
    // What the Release Type column says of each: "Original", "Crack",
    // "Hack", "File", "Amiga Port" and the like, or nothing.
    QStringList listedReleases() const;
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
    QTabBar* tabs_;
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
