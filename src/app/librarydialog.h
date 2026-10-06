#pragma once

#include <optional>
#include <vector>

#include <QCache>
#include <QDialog>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTabBar;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

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
    QString origin;   // where the file comes from, "CPC-Power", "NVG"..., if the name says so, or empty
    QString dump;     // when the image was made and by whom, "1996-07-25 by Nicholas Campbell", or empty
    QString category;     // the category folder it is filed under ("Games"...), or empty
    QString subcategory;  // the folder under that ("Racing"...), or empty
    QString root;         // the library folder it was found in
    QString side;         // "Face A", "Disc 2"... if the name says which side or disc it is, or empty
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
// Port]", "[Amiga Port]", "[MSX Port]"... And so has the collection the file
// was taken from, which tells two dumps of one program apart: "(CPC-Power)",
// "(NVG)", "(Web-Archive)", "(CPCRulez)", "(TOSEC)" or "(CPCWiki)", with or
// without the hyphen. A note that begins with "Dump" says when the image
// was made, and by whom: "(Dump 1996-07-25 by Nicholas Campbell)", or the
// day alone. It has a column too, which the day first puts in order.
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

// Thumbnails: the pictures of the programs, kept in a "thumbnails" folder
// of the library folder, each under a name made of what is known of its
// program: the title, the year, the kind of program (its sub-category, or
// its category), what it is on, which side of it and the kind of release,
// as in "Gryzor (1987) (Run and Gun) (Disc) (Face A) [Original].png". What
// is not known is left out.
QString libraryThumbnailName(const LibraryEntry& entry);  // without the folder and the suffix
QString libraryThumbnailFolder(const LibraryEntry& entry);
// The program's picture: the file of that name; failing that one of the
// same title, year and side, so that a program keeps its picture when it
// is filed elsewhere; failing that one of the same title and year that
// names no side. Empty if it has none.
QString libraryThumbnail(const LibraryEntry& entry);
// Gives the program a picture: a copy of the file, under the program's
// name, in place of the one it had. What went wrong, or nothing.
QString setLibraryThumbnail(const LibraryEntry& entry, const QString& picture);
// The same for a picture that is no file yet, such as one of the screen:
// written as a PNG.
QString setLibraryThumbnail(const LibraryEntry& entry, const QImage& picture);
// Pictures kept beside a program's thumbnail, without taking its place:
// in a "snapshots" folder of the library folder, under the program's name
// and a number, "Gryzor (1987) (Run and Gun) (Disc) 01.png". Gives the
// file written; if it cannot be, nothing, and why.
QString libraryScreenshotFolder(const LibraryEntry& entry);
QString addLibraryScreenshot(const LibraryEntry& entry, const QImage& picture, QString* error = nullptr);
// The program a file is, as the Library lists it: with the library folder
// it is in, among those given, its category and its sub-category. A file
// outside them all is a program too, with no category.
LibraryEntry libraryEntryIn(const QString& path, const QStringList& folders);

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
    QStringList listedOrigins() const;
    QStringList listedDumps() const;
    bool select(const QString& title);

    // Several programs may be selected, with the mouse or the keyboard
    // (Ctrl, Shift), to give them a picture: the Thumbnail button asks for
    // the file, and each of them gets a copy under its own name. The
    // Thumbnail column says which programs have one, and the picture shows
    // beside the pointer while it is over such a program.
    QStringList selectedTitles() const;
    // False, with a word to the user, if a program could not be given it.
    bool setThumbnail(const QString& picture);
    QStringList listedThumbnailTitles() const;
    // The picture beside the pointer; hidden when there is none to show.
    QLabel* preview() const { return preview_; }

    // The other way to look at a tab: the pictures, side by side, instead
    // of the list; a program without one is not shown there. The name and
    // what the columns say come up when the pointer is over a picture.
    // The tabs, the search, the order and the buttons work as in the list.
    bool thumbnailView() const;
    void setThumbnailView(bool on);
    // The side of a picture's box there, in pixels, which a slider beside
    // the box to tick sets: from 120 to 600.
    int thumbnailSize() const;
    void setThumbnailSize(int size);
    // A picture at the size the thumbnail view shows it; nothing while it
    // has yet to be read, which is done when the window has a moment.
    const QPixmap* tile(const QString& file);

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
    QPushButton* thumbnail_;
    QCheckBox* viewBox_;
    QSlider* sizeSlider_;
    int tileSize_ = 360;
    QStackedWidget* views_;
    QListWidget* grid_;
    QCache<QString, QPixmap> tiles_;  // the pictures of the thumbnail view, the last used
    QStringList wanted_;              // those still to read
    int filed_ = 0;                   // how many programs the tab in front has
    QTimer* loader_;
    QLabel* preview_;
    QString previewed_;         // the file the picture shown was read from
    QStringList thumbnails_;    // each program's picture, or nothing

    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    QList<int> selection() const;
    void chooseThumbnail();
    void updateThumbnails();
    void showPreview(const QPoint& at);
    void fillGrid();
    void loadTile();
    void setSelection(const QList<int>& rows);
    QString summary(const QTreeWidgetItem* item) const;
    int current() const;
    void fill();
    void filter();
    void updateButtons();
    void editFolders();
};
