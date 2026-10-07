// The Library window: what file names say, the programs found in the user's
// folders, the search, and a program put into the machine from it. Runs
// without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_library [prefix]   also saves a picture of the window as
//                          <prefix>library.png

#include <cstdio>
#include <cstdlib>
#include <functional>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QToolButton>

#include "check.h"
#include "core/cpc.h"
#include "core/disc.h"
#include "core/setup.h"
#include "core/snapshot.h"
#include "discmanager.h"
#include "emulator.h"
#include "librarydialog.h"
#include "mainwindow.h"
#include "screenwidget.h"
#include "settings.h"

namespace {

// Deals with the window a click opens: `handle` is given it as it comes
// up, and has to close it.
class Modals {
public:
    explicit Modals(std::function<void(QWidget*)> handle)
    {
        QObject::connect(&timer_, &QTimer::timeout, [this, handle] {
            QWidget* modal = QApplication::activeModalWidget();
            if (modal && modal != last_) {
                last_ = modal;
                ++seen_;
                handle(modal);
            }
        });
        timer_.start(5);
    }
    int seen() const { return seen_; }

private:
    QTimer timer_;
    QWidget* last_ = nullptr;
    int seen_ = 0;
};

uint32_t crc32(const QByteArray& data)
{
    uint32_t crc = 0xFFFFFFFF;
    for (const char c : data) {
        crc ^= static_cast<uint8_t>(c);
        for (int bit = 0; bit < 8; ++bit)
            crc = crc & 1 ? crc >> 1 ^ 0xEDB88320 : crc >> 1;
    }
    return ~crc;
}

// Writes a ZIP archive of the files given (name in the archive, file on
// disc), stored without packing.
void writeZip(const QString& path, const QList<QPair<QString, QString>>& files)
{
    auto number = [](QByteArray& out, uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.append(static_cast<char>(value >> (8 * i)));
    };
    QByteArray archive, directory;
    for (const auto& [name, source] : files) {
        QFile file(source);
        CHECK(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();
        const QByteArray nameBytes = name.toUtf8();
        QByteArray fields;  // what the two headers of a file share
        number(fields, 20, 2);  // version needed
        number(fields, 0, 2);   // flags
        number(fields, 0, 2);   // stored
        number(fields, 0, 4);   // time and date
        number(fields, crc32(data), 4);
        number(fields, static_cast<uint32_t>(data.size()), 4);
        number(fields, static_cast<uint32_t>(data.size()), 4);
        number(fields, static_cast<uint32_t>(nameBytes.size()), 2);
        number(fields, 0, 2);  // no extra field
        const uint32_t offset = static_cast<uint32_t>(archive.size());
        archive += QByteArray("PK\x03\x04", 4) + fields + nameBytes + data;
        directory += QByteArray("PK\x01\x02", 4);
        number(directory, 20, 2);  // made by
        directory += fields;
        number(directory, 0, 2);  // no comment
        number(directory, 0, 2);  // disc number
        number(directory, 0, 2);  // attributes
        number(directory, 0, 4);
        number(directory, offset, 4);
        directory += nameBytes;
    }
    const uint32_t start = static_cast<uint32_t>(archive.size());
    archive += directory + QByteArray("PK\x05\x06", 4);
    number(archive, 0, 4);
    number(archive, static_cast<uint32_t>(files.size()), 2);
    number(archive, static_cast<uint32_t>(files.size()), 2);
    number(archive, static_cast<uint32_t>(directory.size()), 4);
    number(archive, start, 4);
    number(archive, 0, 2);
    QFile out(path);
    CHECK(out.open(QIODevice::WriteOnly));
    out.write(archive);
}

void testNames()
{
    LibraryEntry entry = libraryEntry("/games/Gryzor (UK) (1987) (CPM) [Original].dsk");
    CHECK(entry.title == "Gryzor");
    CHECK(entry.year == "1987");
    CHECK(entry.details == "UK, CPM" && entry.release == "Original");
    CHECK(entry.kind == LibraryEntry::Disc);
    CHECK(entry.path == "/games/Gryzor (UK) (1987) (CPM) [Original].dsk");

    CHECK(entry.side.isEmpty());
    // Which side or disc of the program it is: kept apart, and among the
    // notes as before.
    entry = libraryEntry("/games/The Simpsons (UK) (Face A) (1991) [Original].dsk");
    CHECK(entry.title == "The Simpsons" && entry.side == "Face A" && entry.year == "1991");
    CHECK(entry.details == "UK, Face A" && entry.release == "Original");
    CHECK(libraryEntry("/games/Robocop (face 2b).dsk").side == "face 2b");
    CHECK(libraryEntry("/games/Robocop (Side B).cdt").side == "Side B");
    CHECK(libraryEntry("/games/Robocop (Disc 1 of 2).dsk").side == "Disc 1 of 2");
    CHECK(libraryEntry("/games/Robocop (Disk 2) (Face A).dsk").side == "Disk 2, Face A");
    CHECK(libraryEntry("/games/Robocop (Disc and Tape Tools) (Side Arms) (Disc).dsk").side.isEmpty());

    entry = libraryEntry("/games/Last Ninja, The (1988)(System 3)(fr)[cr].DSK");
    CHECK(entry.title == "The Last Ninja");
    CHECK(entry.year == "1988");
    CHECK(entry.details == "System 3, fr" && entry.release == "Crack");
    CHECK(entry.kind == LibraryEntry::Disc);

    // The kind of release: Original, Crack or Hack, as a word or as TOSEC
    // writes it; who did it stays among the notes.
    CHECK(libraryEntry("Zub (1986) [Crack].dsk").release == "Crack" && libraryEntry("Zub (1986) [Crack].dsk").details.isEmpty());
    CHECK(libraryEntry("Zub (cracked).dsk").release == "Crack");
    CHECK(libraryEntry("Zub (HACK).dsk").release == "Hack" && libraryEntry("Zub [Hacked].dsk").release == "Hack");
    CHECK(libraryEntry("Zub (1986)[h XOR].dsk").release == "Hack" && libraryEntry("Zub (1986)[h XOR].dsk").details == "h XOR");
    CHECK(libraryEntry("Zub (1986)[cr NPS][t].dsk").release == "Crack");
    CHECK(libraryEntry("Zub (Original) (Hack).dsk").release == "Original");
    // A program that never came out on a disc or a tape of its own.
    CHECK(libraryEntry("Outrun 2026 (AI) (File) (2026).dsk").release == "File");
    CHECK(libraryEntry("Outrun 2026 (AI) (File) (2026).dsk").ai && libraryEntry("Outrun 2026 (AI) (File) (2026).dsk").details.isEmpty());
    CHECK(libraryEntry("Zub [file].dsk").release == "File" && libraryEntry("Zub (Files).dsk").release.isEmpty());
    // One brought over from another machine, which the note names.
    CHECK(libraryEntry("Flicky [Atari ST Port] (2026).dsk").release == "Atari ST Port");
    CHECK(libraryEntry("Flicky [Atari ST Port] (2026).dsk").details.isEmpty() && libraryEntry("Flicky [Atari ST Port] (2026).dsk").year == "2026");
    CHECK(libraryEntry("Flicky [Amiga Port].dsk").release == "Amiga Port" && libraryEntry("Flicky (MSX Port).dsk").release == "MSX Port");
    CHECK(libraryEntry("Flicky [Colecovision port] (AI).dsk").release == "Colecovision port" && libraryEntry("Flicky [Colecovision port] (AI).dsk").ai);
    CHECK(libraryEntry("Flicky (Portugal).dsk").release.isEmpty() && libraryEntry("Flicky (Port).dsk").release.isEmpty());
    CHECK(libraryEntry("Flicky (Airport).dsk").release.isEmpty() && libraryEntry("Flicky (Portage).dsk").release.isEmpty());
    CHECK(libraryEntry("Zub (Hackers) (Crackdown) (Originals).dsk").release.isEmpty());
    CHECK(libraryEntry("Hack.dsk").release.isEmpty() && libraryEntry("Hack.dsk").title == "Hack");
    // Where the file comes from: a column of its own too, out of the notes.
    entry = libraryEntry("Cyber Power (UK) (1992) (NVG).dsk");
    CHECK(entry.origin == "NVG" && entry.details == "UK" && entry.year == "1992" && entry.release.isEmpty());
    entry = libraryEntry("Xybots (UK) (1989) [Original] [TAPE] (CPC-Power).cdt");
    CHECK(entry.origin == "CPC-Power" && entry.release == "Original" && entry.details == "UK, TAPE");
    CHECK(libraryEntry("Zub (cpc-power).dsk").origin == "CPC-Power" && libraryEntry("Zub [CPC Power].dsk").origin == "CPC-Power");
    CHECK(libraryEntry("Zub (web-archive).dsk").origin == "Web Archive" && libraryEntry("Zub (web-archive).dsk").details.isEmpty());
    CHECK(libraryEntry("Zub (CPC) (CPC+) (Power) (NVG Tools).dsk").origin.isEmpty());
    CHECK(libraryEntry("NVG.dsk").origin.isEmpty() && libraryEntry("Zub (1986).dsk").origin.isEmpty());
    // When the image was made and by whom: a column as well.
    entry = libraryEntry("Cyber Power (UK) (1992) (NVG) (Dump 2002-01-16 by Nicholas Campbell & McSPE).dsk");
    CHECK(entry.dump == "2002-01-16 by Nicholas Campbell & McSPE" && entry.origin == "NVG" && entry.details == "UK");
    CHECK(entry.title == "Cyber Power" && entry.year == "1992");
    entry = libraryEntry("Xybots (UK) (1989) [Original] [TAPE] (CPC-Power) (dump 2016-05-02).cdt");
    CHECK(entry.dump == "2016-05-02" && entry.year == "1989" && entry.details == "UK, TAPE");
    CHECK(libraryEntry("Zub (Dumper) (Dump).dsk").dump.isEmpty() && libraryEntry("Zub (Dumper) (Dump).dsk").details == "Dumper, Dump");
    // "(AI)", in any case, is a mark of its own and not a note.
    CHECK(!entry.ai);
    entry = libraryEntry("/games/Outrun 2026 (AI) (2026) [FR].dsk");
    CHECK(entry.ai && entry.title == "Outrun 2026" && entry.year == "2026" && entry.details == "FR");
    CHECK(libraryEntry("Forest (ai).dsk").ai && libraryEntry("Forest (ai).dsk").details.isEmpty());
    CHECK(!libraryEntry("Air Raid (AIR) (Again).dsk").ai);
    CHECK(!libraryEntry("AI.dsk").ai);
    entry = libraryEntry("/games/sorcery_plus.SNA");
    CHECK(entry.title == "sorcery plus");
    CHECK(entry.year.isEmpty() && entry.details.isEmpty());
    CHECK(entry.kind == LibraryEntry::Snapshot);

    // A year not known, or with its day.
    entry = libraryEntry("Arkanoid (19xx)(Imagine).dsk");
    CHECK(entry.title == "Arkanoid" && entry.year.isEmpty() && entry.details == "19xx, Imagine");
    entry = libraryEntry("Ikari Warriors (1986-05-12)(Elite)(-).dsk");
    CHECK(entry.title == "Ikari Warriors" && entry.year == "1986" && entry.details == "Elite");
    entry = libraryEntry("Aigle d'Or, L' (1984)(Loriciels).dsk");
    CHECK(entry.title == "L'Aigle d'Or");
}

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text))
            return action;
    return nullptr;
}

// Whether the Library shows its tabs.
bool tabs_shown(const LibraryDialog& dialog)
{
    const auto* tabs = dialog.findChild<QTabBar*>("tabCategories");
    return tabs && tabs->isVisible();
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    testNames();
    // No folder of the Library's own, unless a test names one: the
    // project's must not come into these.
    qputenv("TUXAPE_LIBRARY_DIR", folder.filePath("no library").toLocal8Bit());

    // ROM images are not needed: the machine is not started.
    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();

    // Two folders of programs: discs, one of them in a sub-folder, a
    // snapshot, and a file that is neither.
    const QString games = folder.filePath("discs"), more = folder.filePath("more");
    QDir().mkpath(games + "/sub");
    QDir().mkpath(more);
    const QString gryzor = games + "/Gryzor (UK) (1987) (CPM) [Original].dsk";
    const QString ninja = games + "/sub/Last Ninja, The (1988)(System 3).dsk";
    const QString boulder = more + "/boulder dash (AI).dsk";
    const QString sorcery = more + "/Sorcery+ (1985)(Amsoft).sna";
    CHECK(window.discs()->createBlank(0, gryzor, tuxape::defaultDiscFormat()).isEmpty());
    window.discs()->remove(0);
    CHECK(QFile::copy(gryzor, ninja));
    CHECK(QFile::copy(gryzor, boulder));
    emulator.withMachine([](tuxape::Cpc& cpc) { cpc.memory().write(0x8000, 0x42); });
    CHECK(window.saveSnapshotFile(sorcery));
    emulator.withMachine([](tuxape::Cpc& cpc) { cpc.memory().write(0x8000, 0x00); });
    {
        QFile notes(games + "/readme.txt");
        CHECK(notes.open(QIODevice::WriteOnly));
        notes.write("not a program");
    }
    const QStringList folders = {games, more};
    CHECK_EQ(scanLibrary(folders).size(), 4);
    // A folder named twice, or one inside another, gives each file once.
    CHECK_EQ(scanLibrary({games, games + "/sub", games}).size(), 2);
    CHECK(scanLibrary({folder.filePath("missing")}).isEmpty());

    {
        LibraryDialog dialog(folders);
        dialog.show();
        auto* insertA = dialog.findChild<QPushButton*>("bInsertA");
        auto* insertB = dialog.findChild<QPushButton*>("bInsertB");
        auto* count = dialog.findChild<QLabel*>("lCount");
        CHECK(dialog.listedTitles() == (QStringList{"boulder dash", "Gryzor", "Sorcery+", "The Last Ninja"}));
        // The AI column: ticked for the files with "(AI)" in their name.
        CHECK(dialog.listedAiTitles() == QStringList{"boulder dash"});
        auto* programs = dialog.findChild<QTreeWidget*>("lvLibrary");
        CHECK(programs && programs->columnCount() == 10 && programs->headerItem()->text(7) == "AI");
        CHECK(programs && programs->headerItem()->text(9) == "Notes");
        CHECK(programs && programs->topLevelItem(0)->text(9).isEmpty());  // not among the notes as well
        // No category folder here: no tab, and nothing in the column.
        CHECK(dialog.categories() == QStringList{"Unsorted"} && dialog.category().isEmpty());
        auto* tabs = dialog.findChild<QTabBar*>("tabCategories");
        CHECK(tabs && tabs->isHidden());
        CHECK(programs && programs->headerItem()->text(1) == "Sub-category");
        // The type is a picture, with its name for whoever points at it.
        CHECK(programs && programs->headerItem()->text(3) == "Type");
        CHECK(dialog.listedTypes() == (QStringList{"Disc", "Disc", "Snapshot", "Disc"}));
        CHECK(programs && programs->topLevelItem(0)->text(3).isEmpty() && !programs->topLevelItem(0)->icon(3).isNull());
        CHECK(dialog.listedSubcategories() == (QStringList{"", "", "", ""}));
        // The Release Type column: Original, Crack or Hack, from the name.
        CHECK(programs && programs->headerItem()->text(4) == "Release Type");
        CHECK(dialog.listedReleases() == (QStringList{"", "Original", "", ""}));
        CHECK(programs && programs->topLevelItem(1)->text(9) == "UK, CPM");
        // The Origin column: the collection the file was taken from.
        CHECK(programs && programs->headerItem()->text(5) == "Origin");
        CHECK(dialog.listedOrigins() == (QStringList{"", "", "", ""}));
        // The Dump column: when the image was made, and by whom.
        CHECK(programs && programs->headerItem()->text(6) == "Dump");
        CHECK(dialog.listedDumps() == (QStringList{"", "", "", ""}));
        dialog.setSearch("original");
        CHECK(dialog.listedTitles() == QStringList{"Gryzor"});
        dialog.setSearch(QString());
        CHECK(programs && !(programs->topLevelItem(0)->flags() & Qt::ItemIsUserCheckable));
        CHECK(count->text() == "4 of 4");

        // A click on a column's heading sorts by it, from A to Z; a second
        // click, from Z to A. By title to start with.
        if (programs) {
            QHeaderView* header = programs->header();
            const auto click = [&](int column) {
                QTest::mouseClick(header->viewport(), Qt::LeftButton, {},
                                  QPoint(header->sectionViewportPosition(column) + header->sectionSize(column) / 2,
                                         header->height() / 2));
            };
            const auto sorted = [&](int column, Qt::SortOrder order, const QStringList& titles) {
                const bool same = dialog.sortColumn() == column && dialog.sortOrder() == order &&
                                  dialog.listedTitles() == titles;
                if (!same)
                    std::printf("column %d, order %d: %s\n", dialog.sortColumn(), int(dialog.sortOrder()),
                                qPrintable(dialog.listedTitles().join(", ")));
                return same;
            };
            CHECK(header->isSortIndicatorShown() && header->sectionsClickable());
            CHECK(sorted(0, Qt::AscendingOrder, {"boulder dash", "Gryzor", "Sorcery+", "The Last Ninja"}));
            click(0);
            CHECK(sorted(0, Qt::DescendingOrder, {"The Last Ninja", "Sorcery+", "Gryzor", "boulder dash"}));
            // The rows still stand for their programs.
            CHECK(dialog.select("Sorcery+") && insertA->text() == "&Load");
            CHECK(dialog.select("Gryzor") && insertA->text() == "Insert in &A:");
            click(0);
            CHECK(sorted(0, Qt::AscendingOrder, {"boulder dash", "Gryzor", "Sorcery+", "The Last Ninja"}));
            // The year: none, 1985, 1987, 1988.
            click(2);
            CHECK(sorted(2, Qt::AscendingOrder, {"boulder dash", "Sorcery+", "Gryzor", "The Last Ninja"}));
            click(2);
            CHECK(sorted(2, Qt::DescendingOrder, {"The Last Ninja", "Gryzor", "Sorcery+", "boulder dash"}));
            // The type, by its name; programs of one type stay in the order
            // of their titles, whichever way the column goes.
            click(3);
            CHECK(sorted(3, Qt::AscendingOrder, {"boulder dash", "Gryzor", "The Last Ninja", "Sorcery+"}));
            click(3);
            CHECK(sorted(3, Qt::DescendingOrder, {"Sorcery+", "boulder dash", "Gryzor", "The Last Ninja"}));
            // The release type, and the AI boxes: unticked first.
            click(4);
            CHECK(sorted(4, Qt::AscendingOrder, {"boulder dash", "Sorcery+", "The Last Ninja", "Gryzor"}));
            click(4);
            CHECK(sorted(4, Qt::DescendingOrder, {"Gryzor", "boulder dash", "Sorcery+", "The Last Ninja"}));
            click(7);
            CHECK(sorted(7, Qt::AscendingOrder, {"Gryzor", "Sorcery+", "The Last Ninja", "boulder dash"}));
            click(7);
            CHECK(sorted(7, Qt::DescendingOrder, {"boulder dash", "Gryzor", "Sorcery+", "The Last Ninja"}));
            // A search keeps the order.
            dialog.setSort(0, Qt::DescendingOrder);
            dialog.setSearch("o");
            CHECK(sorted(0, Qt::DescendingOrder, {"Sorcery+", "Gryzor", "boulder dash"}));
            dialog.setSearch(QString());
            dialog.setSort(0, Qt::AscendingOrder);
            CHECK(sorted(0, Qt::AscendingOrder, {"boulder dash", "Gryzor", "Sorcery+", "The Last Ninja"}));
        }
        CHECK(insertA->isEnabled() && insertA->text() == "Insert in &A:");
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            dialog.grab().save(prefix + "library.png");
        }

        // The search: every word, in the title, the year, the notes or the
        // file name, capitals or not.
        dialog.setSearch("gry");
        CHECK(dialog.listedTitles() == QStringList{"Gryzor"});
        CHECK(count->text() == "1 of 4");
        dialog.setSearch("ninja 1988 SYSTEM");
        CHECK(dialog.listedTitles() == QStringList{"The Last Ninja"});
        dialog.setSearch("198");
        CHECK(dialog.listedTitles() == (QStringList{"Gryzor", "Sorcery+", "The Last Ninja"}));
        dialog.setSearch(".sna");
        CHECK(dialog.listedTitles() == QStringList{"Sorcery+"});
        // A snapshot is loaded, and has no drive to go in.
        CHECK(insertA->text() == "&Load" && insertA->isEnabled());
        CHECK(!insertB->isEnabled());
        CHECK(!dialog.choose(1));
        dialog.setSearch("zzz");
        CHECK(dialog.listedTitles().isEmpty());
        CHECK(!insertA->isEnabled() && !insertB->isEnabled());
        CHECK(!dialog.choose());
        CHECK(dialog.chosen() == nullptr);
        dialog.setSearch("");
        CHECK_EQ(dialog.listedTitles().size(), 4);

        // The button for drive B.
        CHECK(dialog.select("The Last Ninja"));
        CHECK(!dialog.select("Nothing"));
        insertB->click();
        CHECK_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
        CHECK(dialog.chosen() && dialog.chosen()->path == ninja);
        CHECK_EQ(dialog.drive(), 1);

        // Other folders, or none yet.
        dialog.setFolders({more});
        CHECK(dialog.listedTitles() == (QStringList{"boulder dash", "Sorcery+"}));
        CHECK(dialog.chosen() == nullptr);
        dialog.setFolders({});
        CHECK(dialog.listedTitles().isEmpty());
        CHECK(count->text().contains("Folders..."));
    }

    // The folders are kept with the settings.
    {
        Settings settings;
        settings.libraryFolders = folders;
        CHECK(settings.save());
        Settings loaded;
        loaded.load();
        CHECK(loaded.libraryFolders == folders);
        settings.libraryFolders = QStringList{more};
        CHECK(settings.save());
        loaded.load();
        CHECK(loaded.libraryFolders == QStringList{more});
        settings.libraryFolders.clear();
        CHECK(settings.save());
        loaded.load();
        CHECK(loaded.libraryFolders.isEmpty());
    }

    // From the main window: the menu entry and the toolbar's button.
    QAction* action = actionNamed(window, "Library...");
    CHECK(action && action->isEnabled());
    CHECK(action && action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_L));
    QToolButton* toolButton = nullptr;
    for (QToolButton* candidate : window.findChildren<QToolButton*>())
        if (candidate->toolTip() == "Library (CTRL+L)")
            toolButton = candidate;
    CHECK(toolButton && toolButton->isEnabled());
    if (!action || !toolButton)
        return checkSummary("gui_library");

    // With no folder named, the Library's own is looked in, if there is
    // one: it is used, and not written down as the user's choice.
    {
        CHECK(defaultLibraryFolder().isEmpty());
        CHECK(libraryFoldersOrDefault({}).isEmpty());
        const QString own = folder.filePath("own library");
        QDir().mkpath(own + "/Games/Maze");
        CHECK(QFile::copy(gryzor, own + "/Games/Maze/Pac-Man (1983).dsk"));
        qputenv("TUXAPE_LIBRARY_DIR", own.toLocal8Bit());
        CHECK(defaultLibraryFolder() == own);
        CHECK(libraryFoldersOrDefault({}) == QStringList{own});
        // Folders named and still there are the ones; named and gone, not.
        CHECK(libraryFoldersOrDefault({more}) == QStringList{more});
        CHECK(libraryFoldersOrDefault({folder.filePath("gone")}) == QStringList{own});
        CHECK(libraryFoldersOrDefault({folder.filePath("gone"), more}) == (QStringList{folder.filePath("gone"), more}));
        CHECK(window.settings().libraryFolders.isEmpty());
        Modals modals([&](QWidget* modal) {
            auto* dialog = qobject_cast<LibraryDialog*>(modal);
            CHECK(dialog != nullptr);
            if (!dialog)
                return modal->close(), void();
            CHECK(dialog->folders() == QStringList{own});
            CHECK(dialog->listedTitles() == QStringList{"Pac-Man"} && dialog->category() == "Games");
            dialog->reject();
        });
        action->trigger();
        CHECK_EQ(modals.seen(), 1);
        CHECK(window.settings().libraryFolders.isEmpty());
        qputenv("TUXAPE_LIBRARY_DIR", folder.filePath("no library").toLocal8Bit());
        CHECK(defaultLibraryFolder().isEmpty());
    }

    // Folders named in the window are remembered, even when it is closed
    // without a program.
    {
        Modals modals([&](QWidget* modal) {
            auto* dialog = qobject_cast<LibraryDialog*>(modal);
            CHECK(dialog != nullptr);
            if (!dialog)
                return modal->close(), void();
            CHECK(dialog->folders().isEmpty());
            dialog->setFolders(folders);
            dialog->reject();
        });
        action->trigger();
        CHECK_EQ(modals.seen(), 1);
        CHECK(window.settings().libraryFolders == folders);
        Settings loaded;
        loaded.load();
        CHECK(loaded.libraryFolders == folders);
        CHECK(!window.discs()->info(0).present);
    }
    // A disc chosen goes into drive A...
    {
        Modals modals([&](QWidget* modal) {
            auto* dialog = qobject_cast<LibraryDialog*>(modal);
            CHECK(dialog && dialog->listedTitles().size() == 4);
            if (!dialog)
                return modal->close(), void();
            dialog->setSearch("gryzor");
            CHECK(dialog->choose());
        });
        toolButton->click();
        CHECK_EQ(modals.seen(), 1);
        CHECK(window.discs()->info(0).present);
        CHECK(window.discs()->info(0).path == gryzor);
        CHECK(!window.discs()->info(1).present);
    }
    // ... or drive B, and a snapshot into the machine. The window comes
    // back with the search it was left on.
    {
        Modals modals([&](QWidget* modal) {
            auto* dialog = qobject_cast<LibraryDialog*>(modal);
            CHECK(dialog && dialog->search() == "gryzor");
            if (!dialog)
                return modal->close(), void();
            dialog->setSearch("boulder");
            CHECK(dialog->choose(1));
        });
        action->trigger();
        CHECK_EQ(modals.seen(), 1);
        CHECK(window.discs()->info(1).path == boulder);
        CHECK(window.discs()->info(0).path == gryzor);
    }
    {
        Modals modals([&](QWidget* modal) {
            auto* dialog = qobject_cast<LibraryDialog*>(modal);
            if (!dialog)
                return modal->close(), void();
            dialog->setSearch("sorcery");
            CHECK(dialog->choose());
        });
        action->trigger();
        CHECK_EQ(modals.seen(), 1);
        const int marker = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().read(0x8000); });
        CHECK_EQ(marker, 0x42);
    }
    // ---- Programs inside ZIP archives ---------------------------------------
    const QString zipped = folder.filePath("zipped");
    QDir().mkpath(zipped);
    const QString tape = folder.filePath("harrier.cdt");
    {
        QFile file(tape);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("ZXTape!\x1A\x01\x14\x12\xE8\x03\xD0\x07", 15));
    }
    // One program to an archive: the archive's name is the one that tells.
    const QString single = zipped + "/Barbarian (1987)(Palace)(fr).zip";
    writeZip(single, {{"BARBAR.DSK", gryzor}, {"readme.txt", games + "/readme.txt"}});
    // Several: each goes by its own.
    const QString several = zipped + "/Compilation.ZIP";
    writeZip(several, {{"disks/Zub (1986)(Mastertronic).dsk", gryzor},
                       {"Harrier Attack (1984)(Durell).cdt", tape},
                       {"Thing on a Spring (1986).sna", sorcery}});
    // No program in it, and not an archive at all: nothing listed.
    writeZip(zipped + "/documents.zip", {{"readme.txt", games + "/readme.txt"}});
    CHECK(QFile::copy(games + "/readme.txt", zipped + "/broken.zip"));

    // Categories: the folders "Games", "Demos"... of a library folder are
    // tabs, and the folders inside them the sub-categories.
    {
        const QString filed = folder.filePath("filed");
        for (const char* path : {"Games/Racing", "Games/Platform/Wonder Boy", "Demos/Sound", "Educational", "Extras"})
            QDir().mkpath(filed + '/' + path);
        CHECK(QFile::copy(gryzor, filed + "/Games/Racing/Outrun (AI) [Hack].dsk"));
        CHECK(QFile::copy(gryzor, filed + "/Games/Platform/Wonder Boy/Wonder Boy (1987).dsk"));
        CHECK(QFile::copy(gryzor, filed + "/Games/Loose.dsk"));
        CHECK(QFile::copy(gryzor, filed + "/Demos/Sound/Jukebox 4.dsk"));
        CHECK(QFile::copy(gryzor, filed + "/Extras/Thing.dsk"));
        CHECK(QFile::copy(gryzor, filed + "/Odd One.dsk"));
        writeZip(filed + "/Games/Racing/Pack.zip", {{"Crazy Cars.dsk", gryzor}});
        const QList<LibraryEntry> programs = scanLibrary({filed});
        CHECK_EQ(programs.size(), 7);
        for (const LibraryEntry& program : programs) {
            if (program.title == "Outrun" || program.title == "Pack")
                CHECK(program.category == "Games" && program.subcategory == "Racing");
            else if (program.title == "Wonder Boy")
                CHECK(program.category == "Games" && program.subcategory == "Platform");
            else if (program.title == "Loose")
                CHECK(program.category == "Games" && program.subcategory.isEmpty());
            else if (program.title == "Jukebox 4")
                CHECK(program.category == "Demos" && program.subcategory == "Sound");
            else
                CHECK(program.category.isEmpty() && program.subcategory.isEmpty());
        }
        LibraryDialog dialog({filed});
        dialog.show();
        auto* tabs = dialog.findChild<QTabBar*>("tabCategories");
        CHECK(tabs && tabs->isVisible());
        // In their own order; Educational has its folder, and nothing in it yet.
        CHECK(dialog.categories() == (QStringList{"Games", "Educational", "Demos", "Unsorted"}));
        CHECK(dialog.category() == "Games");
        CHECK(dialog.listedTitles() == (QStringList{"Loose", "Outrun", "Pack", "Wonder Boy"}));
        CHECK(dialog.listedSubcategories() == (QStringList{"", "Racing", "Racing", "Platform"}));
        CHECK(dialog.listedAiTitles() == QStringList{"Outrun"} && dialog.listedReleases().value(1) == "Hack");
        auto* count = dialog.findChild<QLabel*>("lCount");
        CHECK(count && count->text() == "4 of 4");
        // The search is within the tab, sub-category included.
        dialog.setSearch("racing");
        CHECK(dialog.listedTitles() == (QStringList{"Outrun", "Pack"}));
        CHECK(count && count->text() == "2 of 4");
        dialog.setSearch("jukebox");
        CHECK(dialog.listedTitles().isEmpty());
        CHECK(dialog.setCategory("Demos"));
        CHECK(dialog.listedTitles() == QStringList{"Jukebox 4"} && dialog.listedSubcategories() == QStringList{"Sound"});
        dialog.setSearch(QString());
        CHECK(dialog.setCategory("Educational"));
        CHECK(dialog.listedTitles().isEmpty());
        CHECK(!dialog.choose(0));
        CHECK(dialog.setCategory(QString()));
        CHECK(dialog.category().isEmpty());
        CHECK(dialog.listedTitles() == (QStringList{"Odd One", "Thing"}));
        CHECK(!dialog.setCategory("Utilities"));
        // A click on a tab.
        if (tabs)
            tabs->setCurrentIndex(0);
        CHECK(dialog.category() == "Games" && dialog.listedTitles().size() == 4);
        CHECK(dialog.select("Wonder Boy") && dialog.choose(0));
        CHECK(dialog.chosen() && dialog.chosen()->subcategory == "Platform");
        if (!prefix.isEmpty()) {
            dialog.show();
            dialog.grab().save(prefix + "categories.png");
        }
        // Nothing filed yet: the window opens on what there is.
        const QString fresh = folder.filePath("fresh");
        QDir().mkpath(fresh + "/Games/Racing");
        CHECK(QFile::copy(gryzor, fresh + "/Somewhere.dsk"));
        LibraryDialog unsorted({fresh});
        CHECK(unsorted.categories() == (QStringList{"Games", "Unsorted"}) && unsorted.category().isEmpty());
        CHECK(unsorted.listedTitles() == QStringList{"Somewhere"});
        // A library folder that is a category's own.
        LibraryDialog games({filed + "/Games"});
        CHECK(games.categories() == QStringList{"Games"});
        CHECK(games.listedSubcategories() == (QStringList{"", "Racing", "Racing", "Platform"}));
        // The names of the folders, in any case.
        CHECK(libraryCategories()
              == (QStringList{"Games", "Educational", "Utilities", "Demos", "Compilations", "Miscellaneous", "SNR"}));
    }

    // An archive marked "(AI)" marks the programs it holds.
    {
        const QString marked = folder.filePath("marked");
        QDir().mkpath(marked);
        writeZip(marked + "/Pack (AI) [Crack].zip", {{"One (2026).dsk", gryzor}, {"Two (Hack).dsk", gryzor}});
        writeZip(marked + "/Other.zip", {{"Three [ai].dsk", gryzor}, {"Four.dsk", gryzor}});
        const QList<LibraryEntry> programs = scanLibrary({marked});
        CHECK_EQ(programs.size(), 4);
        if (programs.size() == 4) {
            CHECK(programs[0].title == "Four" && !programs[0].ai && programs[0].release.isEmpty());
            CHECK(programs[1].title == "One" && programs[1].ai && programs[1].year == "2026" && programs[1].release == "Crack");
            CHECK(programs[2].title == "Three" && programs[2].ai && programs[2].details.isEmpty());
            CHECK(programs[3].title == "Two" && programs[3].ai && programs[3].release == "Hack");
        }
    }

    const QList<LibraryEntry> inside = scanLibrary({zipped});
    CHECK_EQ(inside.size(), 4);
    if (inside.size() == 4) {
        CHECK(inside[0].title == "Barbarian" && inside[0].year == "1987" && inside[0].details == "Palace, fr");
        CHECK(inside[0].path == single && inside[0].member == "BARBAR.DSK" && inside[0].kind == LibraryEntry::Disc);
        CHECK(inside[1].title == "Harrier Attack" && inside[1].kind == LibraryEntry::Tape && inside[1].path == several);
        CHECK(inside[2].title == "Thing on a Spring" && inside[2].kind == LibraryEntry::Snapshot);
        CHECK(inside[3].title == "Zub" && inside[3].member == "disks/Zub (1986)(Mastertronic).dsk");
        CHECK(libraryDisplayPath(inside[3]).endsWith(QString::fromUtf8("Compilation.ZIP \u00BB disks/Zub (1986)(Mastertronic).dsk")));
        const auto data = libraryData(inside[0]);
        QFile original(gryzor);
        CHECK(original.open(QIODevice::ReadOnly));
        const QByteArray bytes = original.readAll();
        CHECK(data && QByteArray(reinterpret_cast<const char*>(data->data()), static_cast<qsizetype>(data->size())) == bytes);
        LibraryEntry gone = inside[0];
        gone.member = "OTHER.DSK";
        CHECK(!libraryData(gone));
    }
    CHECK(libraryData(libraryEntry(gryzor)).has_value());

    // From the window: a search finds a program by its archive's name too,
    // and each kind goes where it belongs.
    Settings zippedSettings = window.settings();
    zippedSettings.libraryFolders = QStringList{zipped};
    window.applySettings(zippedSettings);
    window.discs()->remove(0);
    emulator.withMachine([](tuxape::Cpc& cpc) { cpc.memory().write(0x8000, 0x00); });
    auto pick = [&](const QString& search, const QString& title, int drive) {
        Modals modals([&](QWidget* modal) {
            auto* dialog = qobject_cast<LibraryDialog*>(modal);
            if (!dialog)
                return modal->close(), void();
            dialog->setSearch(search);
            CHECK(dialog->listedTitles() == QStringList{title});
            CHECK(dialog->choose(drive));
        });
        action->trigger();
        CHECK_EQ(modals.seen(), 1);
    };
    pick("compilation zub", "Zub", 1);
    CHECK(window.discs()->info(1).present && window.discs()->info(1).readOnly);
    CHECK(window.discs()->info(1).path.endsWith("Zub (1986)(Mastertronic).dsk"));
    pick("barbar", "Barbarian", 0);
    CHECK(window.discs()->info(0).present && window.discs()->info(0).readOnly);
    pick("harrier", "Harrier Attack", 0);
    CHECK(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.tape().loaded() && cpc.tape().playing(); }));
    pick("spring", "Thing on a Spring", 0);
    const int zippedMarker = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().read(0x8000); });
    CHECK_EQ(zippedMarker, 0x42);

    // The filter buttons: by kind, each with its picture, and by machine.
    // A program is for the Plus when its name says so, or when it is a
    // cartridge; for the CPC otherwise.
    {
        CHECK(!libraryEntry("/g/Alpha (UK) (1987) [Original].dsk").plus);
        CHECK(libraryEntry("/g/Beta (UK) (1990) [CPC+] [DEMO].dsk").plus);
        CHECK(libraryEntry("/g/Gamma (F) (1996) [CPC CPC+] [DEMO].dsk").plus);
        CHECK(libraryEntry("/g/Eta (UK) (128K) (2001) (Version 6128+) [DEMO].dsk").plus);
        CHECK(libraryEntry("/g/Theta (UK) (1990) (GX 4000) [SESSION].snr").plus);
        CHECK(libraryEntry("/g/Epsilon (UK) (1990) [Original].cpr").plus);
        CHECK(!libraryEntry("/g/Sorcery+ (1985)(Amsoft).sna").plus);
        CHECK(!libraryEntry("/g/Iota (UK) (1986) (CPC 6128) [Original].dsk").plus);

        const QString sorted = folder.filePath("sorted");
        QDir().mkpath(sorted);
        for (const char* name : {"Alpha (UK) (1987) [Original].dsk", "Beta (UK) (1990) [CPC+] [DEMO].dsk",
                                 "Gamma (F) (1996) [CPC CPC+] [DEMO].dsk", "Delta (UK) (1988) [Original] [TAPE].cdt",
                                 "Epsilon (UK) (1990) [Original] [CARTOUCHE].cpr", "Zeta (1985).sna"}) {
            QFile file(sorted + '/' + name);
            CHECK(file.open(QIODevice::WriteOnly));
        }
        LibraryDialog dialog({sorted});
        dialog.show();
        using Titles = QStringList;
        CHECK_EQ(dialog.filters(), 0);
        CHECK(dialog.listedTitles() == (Titles{"Alpha", "Beta", "Delta", "Epsilon", "Gamma", "Zeta"}));
        const char* const names[] = {"bDiscs", "bTapes", "bCartridges", "bCpc", "bPlus"};
        QToolButton* buttons[5] = {};
        for (int n = 0; n < 5; ++n) {
            buttons[n] = dialog.findChild<QToolButton*>(names[n]);
            CHECK(buttons[n] && buttons[n]->isCheckable() && !buttons[n]->isChecked() && !buttons[n]->toolTip().isEmpty());
            if (!buttons[n])
                return checkSummary("gui_library");
        }
        // The kinds have their picture, the machines their name.
        CHECK(!buttons[0]->icon().isNull() && !buttons[1]->icon().isNull() && !buttons[2]->icon().isNull());
        CHECK(buttons[3]->text() == "CPC" && buttons[4]->text() == "CPC+");

        using Filter = LibraryDialog::Filter;
        buttons[0]->click();
        CHECK_EQ(dialog.filters(), int(Filter::Discs));
        CHECK(dialog.listedTitles() == (Titles{"Alpha", "Beta", "Gamma"}));
        auto* count = dialog.findChild<QLabel*>("lCount");
        CHECK(count && count->text() == "3 of 6");
        // Discs for the Plus, and discs that are not.
        buttons[4]->click();
        CHECK(dialog.listedTitles() == (Titles{"Beta", "Gamma"}));
        buttons[4]->click();
        buttons[3]->click();
        CHECK_EQ(dialog.filters(), Filter::Discs | Filter::ForCpc);
        CHECK(dialog.listedTitles() == Titles{"Alpha"});
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            dialog.grab().save(prefix + "filters.png");
        }
        dialog.setFilters(Filter::Tapes);
        CHECK(dialog.listedTitles() == Titles{"Delta"} && buttons[1]->isChecked() && !buttons[0]->isChecked());
        dialog.setFilters(Filter::Cartridges);
        CHECK(dialog.listedTitles() == Titles{"Epsilon"});
        dialog.setFilters(Filter::Discs | Filter::Tapes);
        CHECK(dialog.listedTitles() == (Titles{"Alpha", "Beta", "Delta", "Gamma"}));
        // The machine alone: a cartridge is the Plus's.
        dialog.setFilters(Filter::ForPlus);
        CHECK(dialog.listedTitles() == (Titles{"Beta", "Epsilon", "Gamma"}));
        dialog.setFilters(Filter::ForCpc);
        CHECK(dialog.listedTitles() == (Titles{"Alpha", "Delta", "Zeta"}));
        dialog.setFilters(Filter::ForCpc | Filter::ForPlus);
        CHECK(dialog.listedTitles() == (Titles{"Alpha", "Beta", "Delta", "Epsilon", "Gamma", "Zeta"}));
        // With the search.
        dialog.setFilters(Filter::ForPlus);
        dialog.setSearch("demo");
        CHECK(dialog.listedTitles() == (Titles{"Beta", "Gamma"}));
        dialog.setSearch(QString());
        dialog.setFilters(0);
        CHECK(dialog.listedTitles().size() == 6);
    }

    // Recorded sessions: the files of the "SNR" folder have a tab of their
    // own, "Let's Play", and a double click plays one back. Those WinAPE
    // recorded are played too, with the disc they name found in the
    // Library, however its name is spelt there.
    {
        const QString played = folder.filePath("played");
        QDir().mkpath(played + "/SNR");
        QDir().mkpath(played + "/Games");
        const QString session = played + "/SNR/Gryzor (UK) (1987) (5mn19s) [Somebody] [SESSION].snr";
        window.startSessionRecording(session, false);
        CHECK(window.stopSessionRecording());
        {
            // As WinAPE lays one out: its snapshot under its own words, the
            // ROMs, the discs, a version, and the keys to the file's end.
            const std::vector<uint8_t> snapshot = emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::saveSnapshot(cpc); });
            QByteArray file(reinterpret_cast<const char*>(snapshot.data()), static_cast<qsizetype>(snapshot.size()));
            file.replace(0, 8, "RW - SNR");
            const auto chunk = [&](const char* id, const QByteArray& data, bool toTheEnd = false) {
                const quint32 length = toTheEnd ? 0xFFFFFFFFu : static_cast<quint32>(data.size());
                file += QByteArray(id, 4);
                for (int n = 0; n < 4; ++n)
                    file += static_cast<char>(length >> (8 * n));
                file += data;
            };
            chunk("ROMS", QByteArray("A\0OS6128\0BASIC1-1\0\0\0\0\0\0\0AMSDOS\0\0\0\0\0\0\0\0\0", 39));
            chunk("DSCA", "GRYZOR!.DSK");
            chunk("DSCB", QByteArray());
            chunk("SNRV", QByteArray(1, '\1'));
            // After a second the space bar, for a tenth of one; then 200 frames.
            chunk("SNR ", QByteArray(78, '\0') + QByteArray("\x32\x01\x2F\x05\x7F\xC8", 6), true);
            QFile winApe(played + "/SNR/1943 (UK) (1988) (WinApe 2.0 Alpha 18) [SESSION].snr");
            CHECK(winApe.open(QIODevice::WriteOnly));
            winApe.write(file);
        }
        CHECK(QFile::copy(gryzor, played + "/Games/Gryzor.dsk"));
        const QList<LibraryEntry> entries = scanLibrary({played});
        CHECK_EQ(entries.size(), 3);
        for (const LibraryEntry& entry : entries) {
            const bool disc = entry.path.endsWith(".dsk");
            CHECK(entry.kind == (disc ? LibraryEntry::Disc : LibraryEntry::Session));
            CHECK(entry.category == (disc ? "Games" : "SNR"));
        }
        CHECK(libraryCategoryTitle("SNR") == "Let's Play" && libraryCategoryTitle("Games") == "Games");
        // The window, the tab, and a session chosen in it.
        const auto play = [&](const QString& title, QString* said) {
            Modals modals([&](QWidget* modal) {
                auto* dialog = qobject_cast<LibraryDialog*>(modal);
                if (!dialog) {
                    // What the main window has to say of the session.
                    if (auto* box = qobject_cast<QMessageBox*>(modal))
                        *said = box->text();
                    return modal->close(), void();
                }
                dialog->setFolders({played});
                dialog->setSearch(QString());  // the window remembers the last one
                CHECK(dialog->categories() == (QStringList{"Games", "Let's Play"}));
                CHECK(dialog->setCategory("SNR") && dialog->category() == "SNR");
                CHECK(dialog->listedTitles() == (QStringList{"1943", "Gryzor"}));
                CHECK(dialog->listedTypes() == (QStringList{"Session", "Session"}));
                auto* insertA = dialog->findChild<QPushButton*>("bInsertA");
                auto* insertB = dialog->findChild<QPushButton*>("bInsertB");
                CHECK(insertA && insertA->text() == "&Play" && insertB && !insertB->isEnabled());
                CHECK(dialog->select(title) && dialog->choose(0));
            });
            action->trigger();
        };
        QString said;
        play("Gryzor", &said);
        CHECK(emulator.playingSession() && said.isEmpty());
        emulator.stopPlayback();
        window.discs()->remove(0);
        play("1943", &said);
        CHECK(emulator.playingSession() && said.isEmpty());
        CHECK_EQ(emulator.playbackPosition().second, 255u);
        CHECK(window.discs()->info(0).present && window.discs()->info(0).path.endsWith("/Games/Gryzor.dsk"));
        emulator.stopPlayback();
        CHECK(!emulator.playingSession());
        // Without its disc it is played all the same, and that is said; one
        // that stops short is not played.
        CHECK(QFile::remove(played + "/Games/Gryzor.dsk"));
        play("1943", &said);
        CHECK(emulator.playingSession() && said.contains("GRYZOR!.DSK") && said.contains("could not be found"));
        emulator.stopPlayback();
        {
            QFile winApe(played + "/SNR/1943 (UK) (1988) (WinApe 2.0 Alpha 18) [SESSION].snr");
            CHECK(winApe.open(QIODevice::ReadWrite) && winApe.resize(winApe.size() - 4));
        }
        said.clear();
        play("1943", &said);
        CHECK(!emulator.playingSession() && said.contains("damaged"));
        CHECK(QFile::copy(gryzor, played + "/Games/Gryzor.dsk"));

        // The window comes back sorted as it was left.
        const auto withLibrary = [&](const std::function<void(LibraryDialog*)>& act) {
            Modals modals([&](QWidget* modal) {
                if (auto* dialog = qobject_cast<LibraryDialog*>(modal))
                    act(dialog);
                modal->close();
            });
            action->trigger();
        };
        withLibrary([&](LibraryDialog* dialog) {
            CHECK(dialog->sortColumn() == 0 && dialog->sortOrder() == Qt::AscendingOrder);
            dialog->setSort(2, Qt::DescendingOrder);
            // The thumbnail view is a setting, kept from one day to the next.
            CHECK(!dialog->thumbnailView() && !window.settings().libraryThumbnailView);
            CHECK(dialog->thumbnailSize() == 360 && window.settings().libraryThumbnailSize == 360);
            dialog->setThumbnailView(true);
            dialog->setThumbnailSize(480);
        });
        CHECK(window.settings().libraryThumbnailView && window.settings().libraryThumbnailSize == 480);
        {
            Settings kept;
            kept.load();
            CHECK(kept.libraryThumbnailView && kept.libraryThumbnailSize == 480);
        }
        withLibrary([](LibraryDialog* dialog) {
            CHECK(dialog->sortColumn() == 2 && dialog->sortOrder() == Qt::DescendingOrder);
            CHECK(dialog->setCategory("SNR") && dialog->listedTitles() == (QStringList{"1943", "Gryzor"}));
            dialog->setSort(0, Qt::AscendingOrder);
            CHECK(dialog->thumbnailView() && dialog->thumbnailSize() == 480);
            dialog->setThumbnailView(false);
            dialog->setThumbnailSize(360);
        });
        CHECK(!window.settings().libraryThumbnailView);
    }
    // Thumbnails: several programs selected, with the mouse or the
    // keyboard, are given a picture, which is copied for each under a name
    // made of what is known of the program, into the library's
    // "thumbnails" folder. A column says who has one, and the picture
    // shows beside the pointer.
    {
        const QString shelf = folder.filePath("shelf");
        QDir().mkpath(shelf + "/Games/Run and Gun");
        QDir().mkpath(shelf + "/Games/Platform");
        const QString disc = shelf + "/Games/Run and Gun/Gryzor (UK) (1987) [Original].dsk";
        const QString crack = shelf + "/Games/Run and Gun/Gryzor (UK) (1987) [cr].dsk";
        const QString tape = shelf + "/Games/Run and Gun/Gryzor (1987).cdt";
        const QString other = shelf + "/Games/Platform/Sorcery+ (1985).dsk";
        const QString loose = shelf + "/Zub.dsk";
        for (const QString& file : {disc, crack, tape, other, loose})
            CHECK(QFile::copy(gryzor, file));
        const QString cover = folder.filePath("cover.png"), photo = folder.filePath("photo.bmp");
        {
            QImage small(64, 48, QImage::Format_RGB32), large(800, 400, QImage::Format_RGB32);
            small.fill(Qt::red);
            large.fill(Qt::blue);
            CHECK(small.save(cover) && large.save(photo));
        }
        const auto entryOf = [&](const QString& path) {
            for (const LibraryEntry& entry : scanLibrary({shelf}))
                if (entry.path == path)
                    return entry;
            return LibraryEntry();
        };
        CHECK(entryOf(disc).root == shelf);
        CHECK(libraryThumbnailName(entryOf(disc)) == "Gryzor (1987) (Run and Gun) (Disc) [Original]");
        CHECK(libraryThumbnailName(entryOf(crack)) == "Gryzor (1987) (Run and Gun) (Disc) [Crack]");
        CHECK(libraryThumbnailName(entryOf(tape)) == "Gryzor (1987) (Run and Gun) (Tape)");
        CHECK(libraryThumbnailName(entryOf(loose)) == "Zub (Disc)");
        CHECK(libraryThumbnailFolder(entryOf(disc)) == shelf + "/thumbnails");
        // With no sub-category the category stands for the kind of program;
        // what a file's name cannot hold is left out of it.
        LibraryEntry odd;
        odd.title = "What? Where: A/B";
        odd.kind = LibraryEntry::Cartridge;
        odd.category = "Demos";
        CHECK(libraryThumbnailName(odd) == "What_ Where_ A_B (Demos) (Cartridge)");
        CHECK(libraryThumbnail(entryOf(disc)).isEmpty());

        LibraryDialog dialog({shelf});
        dialog.show();
        auto* programs = dialog.findChild<QTreeWidget*>("lvLibrary");
        auto* button = dialog.findChild<QPushButton*>("bThumbnail");
        auto* insertA = dialog.findChild<QPushButton*>("bInsertA");
        QLabel* preview = dialog.preview();
        CHECK(programs && button && insertA && preview);
        if (!(programs && button && insertA && preview))
            return checkSummary("gui_library");
        CHECK(programs->headerItem()->text(8) == "Thumbnail");
        // It shows right after the titles, whatever its number.
        CHECK(programs->header()->visualIndex(0) == 0 && programs->header()->visualIndex(8) == 1);
        CHECK(programs->header()->visualIndex(1) == 2 && programs->header()->visualIndex(9) == 9);
        CHECK(dialog.setCategory("Games"));
        CHECK(dialog.listedTitles() == (QStringList{"Gryzor", "Gryzor", "Gryzor", "Sorcery+"}));
        CHECK(dialog.listedTypes() == (QStringList{"Tape", "Disc", "Disc", "Disc"}));
        CHECK(dialog.listedReleases() == (QStringList{"", "Original", "Crack", ""}));
        CHECK(dialog.listedThumbnailTitles().isEmpty());
        QTest::qWait(50);
        const auto at = [&](int row) { return programs->visualItemRect(programs->topLevelItem(row)).center(); };

        // One program selected is the one to put in the machine; several
        // are for the Thumbnail button only. The keyboard: Shift and the
        // arrows. The mouse: Ctrl and a click.
        QTest::mouseClick(programs->viewport(), Qt::LeftButton, {}, at(0));
        CHECK(dialog.selectedTitles() == QStringList{"Gryzor"});
        CHECK(insertA->isEnabled() && button->isEnabled());
        QTest::keyClick(programs, Qt::Key_Down, Qt::ShiftModifier);
        CHECK_EQ(dialog.selectedTitles().size(), 2);
        CHECK(!insertA->isEnabled() && button->isEnabled());
        CHECK(!dialog.choose());
        QTest::mouseClick(programs->viewport(), Qt::LeftButton, Qt::ControlModifier, at(3));
        CHECK(dialog.selectedTitles() == (QStringList{"Gryzor", "Gryzor", "Sorcery+"}));
        QTest::mouseClick(programs->viewport(), Qt::LeftButton, Qt::ControlModifier, at(3));
        QTest::mouseClick(programs->viewport(), Qt::LeftButton, Qt::ControlModifier, at(2));
        CHECK(dialog.selectedTitles() == (QStringList{"Gryzor", "Gryzor", "Gryzor"}));

        // The picture goes to each of them, under its own name.
        CHECK(dialog.setThumbnail(cover));
        const QDir thumbnails(shelf + "/thumbnails");
        CHECK(thumbnails.entryList(QDir::Files, QDir::Name) ==
              (QStringList{"Gryzor (1987) (Run and Gun) (Disc) [Crack].png",
                           "Gryzor (1987) (Run and Gun) (Disc) [Original].png", "Gryzor (1987) (Run and Gun) (Tape).png"}));
        CHECK(QFile::exists(cover));  // a copy: the file given stays where it was
        CHECK(dialog.listedThumbnailTitles() == (QStringList{"Gryzor", "Gryzor", "Gryzor"}));
        CHECK_EQ(dialog.selectedTitles().size(), 3);
        CHECK(libraryThumbnail(entryOf(disc)) == shelf + "/thumbnails/Gryzor (1987) (Run and Gun) (Disc) [Original].png");
        CHECK(programs->topLevelItem(0)->toolTip(8) == "Gryzor (1987) (Run and Gun) (Tape).png");
        // The column shows a camera for those that have one, and nothing,
        // not a box, for the others.
        CHECK(!programs->topLevelItem(0)->icon(8).isNull() && programs->topLevelItem(3)->icon(8).isNull());
        CHECK(programs->topLevelItem(3)->text(0) == "Sorcery+" && programs->topLevelItem(3)->toolTip(8).isEmpty());
        for (int row = 0; row < 4; ++row)
            CHECK(!programs->topLevelItem(row)->data(8, Qt::CheckStateRole).isValid());
        // The column sorts as the others do: those without first.
        dialog.setSort(8, Qt::AscendingOrder);
        CHECK(dialog.listedTitles() == (QStringList{"Sorcery+", "Gryzor", "Gryzor", "Gryzor"}));
        dialog.setSort(0, Qt::AscendingOrder);

        // The button asks for the file. No file, no change.
        CHECK(dialog.select("Sorcery+") && dialog.selectedTitles() == QStringList{"Sorcery+"});
        {
            Modals modals([](QWidget* modal) {
                CHECK(qobject_cast<QFileDialog*>(modal) != nullptr && modal->windowTitle() == "Thumbnail");
                modal->close();
            });
            button->click();
            CHECK_EQ(modals.seen(), 1);
            CHECK_EQ(dialog.listedThumbnailTitles().size(), 3);
        }
        {
            Modals modals([&](QWidget* modal) {
                auto* files = qobject_cast<QFileDialog*>(modal);
                if (!files)
                    return modal->close(), void();
                // The name is typed: selectFile() leaves the box alone when
                // it has the keyboard, which it may or may not have yet.
                auto* name = files->findChild<QLineEdit*>("fileNameEdit");
                CHECK(name != nullptr);
                if (!name)
                    return modal->close(), void();
                name->setText(photo);
                static_cast<QDialog*>(files)->accept();
            });
            button->click();
            CHECK_EQ(modals.seen(), 1);
        }
        CHECK(QFile::exists(shelf + "/thumbnails/Sorcery+ (1985) (Platform) (Disc).bmp"));
        CHECK_EQ(dialog.listedThumbnailTitles().size(), 4);

        // The picture shows beside the pointer while it is over the camera
        // of a program that has one, and nowhere else along the row: as it
        // is when small, made smaller when large.
        const auto hover = [&](const QPoint& where) {
            QMouseEvent move(QEvent::MouseMove, where, programs->viewport()->mapToGlobal(where), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QApplication::sendEvent(programs->viewport(), &move);
        };
        int thumbnailColumn = -1;
        for (int column = 0; column < programs->columnCount(); ++column)
            if (programs->headerItem()->text(column) == "Thumbnail")
                thumbnailColumn = column;
        CHECK(thumbnailColumn > 0);
        const auto camera = [&](int row) {
            return QPoint(programs->columnViewportPosition(thumbnailColumn) + 9, at(row).y());
        };
        CHECK(!preview->isVisible());
        hover(QPoint(40, at(1).y()));  // on the title
        CHECK(!preview->isVisible());
        hover(camera(1));
        CHECK(preview->isVisible());
        CHECK(preview->pixmap().size() == QSize(64, 48));
        // Off the camera, on the same row: gone. Further along the same
        // column, past the camera: gone too.
        hover(QPoint(40, at(1).y()));
        CHECK(!preview->isVisible());
        hover(camera(1) + QPoint(40, 0));
        CHECK(!preview->isVisible());
        hover(QPoint(programs->columnViewportPosition(thumbnailColumn) - 6, at(1).y()));
        CHECK(!preview->isVisible());
        hover(camera(1));
        CHECK(preview->isVisible());
        const QPoint pointer = programs->viewport()->mapToGlobal(camera(1));
        CHECK(!preview->geometry().contains(pointer));
        CHECK(std::abs(preview->geometry().left() - pointer.x()) < 40 || std::abs(preview->geometry().right() - pointer.x()) < 40);
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            dialog.grab().save(prefix + "thumbnails.png");
        }
        hover(camera(3));
        // No larger than 720 pixels, nor than four fifths of the screen.
        const QSize largest = QSize(720, 720).boundedTo(dialog.screen()->availableGeometry().size() * 4 / 5);
        CHECK(preview->isVisible() && preview->pixmap().size() == QSize(800, 400).scaled(largest, Qt::KeepAspectRatio));
        CHECK(preview->pixmap().width() > 500);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(programs->viewport(), &leave);
        CHECK(!preview->isVisible());
        // A program without one shows none. All selected: those listed.
        CHECK(dialog.setCategory(QString()));
        CHECK(dialog.listedTitles() == QStringList{"Zub"} && dialog.listedThumbnailTitles().isEmpty());
        QTest::qWait(50);
        hover(camera(4));
        CHECK(!preview->isVisible());
        programs->selectAll();
        CHECK(dialog.selectedTitles() == QStringList{"Zub"});
        // What is not a picture is refused, with a word to the user.
        {
            Modals modals([](QWidget* modal) {
                CHECK(qobject_cast<QMessageBox*>(modal) != nullptr);
                modal->close();
            });
            CHECK(!dialog.setThumbnail(disc));
            CHECK_EQ(modals.seen(), 1);
            CHECK(dialog.listedThumbnailTitles().isEmpty());
        }

        // The right button's menu: the Thumbnail button's doing, and the
        // taking away of a picture, which deletes its file and is asked
        // about first. Only a program's own picture goes.
        {
            CHECK(dialog.setCategory("Games") && dialog.select("Sorcery+"));
            CHECK_EQ(dialog.selectedOwnThumbnails(), 1);
            const QString own = shelf + "/thumbnails/Sorcery+ (1985) (Platform) (Disc).bmp";
            CHECK(QFile::exists(own));
            QStringList entries;
            bool removeEnabled = false;
            QTimer::singleShot(50, &dialog, [&] {
                if (auto* menu = dialog.findChild<QMenu*>("LibraryMenu")) {
                    for (const QAction* entry : menu->actions()) {
                        entries << entry->text();
                        removeEnabled = removeEnabled || (entry->text() == "&Remove Thumbnail" && entry->isEnabled());
                    }
                    menu->close();
                }
            });
            emit programs->customContextMenuRequested(programs->visualItemRect(programs->currentItem()).center());
            CHECK(entries == (QStringList{"&Thumbnail...", "&Remove Thumbnail"}) && removeEnabled);
            CHECK(QFile::exists(own));  // the menu closed without a choice
            CHECK_EQ(dialog.removeThumbnails(), 1);
            CHECK(!QFile::exists(own));
            CHECK_EQ(dialog.listedThumbnailTitles().size(), 3);
            CHECK(dialog.selectedOwnThumbnails() == 0 && dialog.removeThumbnails() == 0);
            // Back, for what follows.
            CHECK(dialog.setThumbnail(photo));
            CHECK(QFile::exists(own));
        }

        // A new picture takes the place of the old one, of whatever kind.
        CHECK(dialog.setCategory("Games") && dialog.select("Sorcery+"));
        CHECK(dialog.setThumbnail(cover));
        CHECK(!QFile::exists(shelf + "/thumbnails/Sorcery+ (1985) (Platform) (Disc).bmp"));
        CHECK(QFile::exists(shelf + "/thumbnails/Sorcery+ (1985) (Platform) (Disc).png"));
        CHECK_EQ(thumbnails.entryList(QDir::Files).size(), 4);
        // The side is part of the name: one side's levels are not the
        // other's. A picture that names no side stands for all of them
        // until a side has its own.
        {
            const QString faceA = shelf + "/Games/Platform/The Simpsons (UK) (Face A) (1991) [Original].dsk";
            const QString faceB = shelf + "/Games/Platform/The Simpsons (UK) (Face B) (1991) [Original].dsk";
            CHECK(QFile::copy(gryzor, faceA) && QFile::copy(gryzor, faceB));
            CHECK(libraryThumbnailName(entryOf(faceA)) == "The Simpsons (1991) (Platform) (Disc) (Face A) [Original]");
            CHECK(libraryThumbnailName(entryOf(faceB)) == "The Simpsons (1991) (Platform) (Disc) (Face B) [Original]");
            const QString whole = shelf + "/thumbnails/The Simpsons (1991) (Platform) (Disc) [Original].png";
            CHECK(QFile::copy(cover, whole));
            CHECK(libraryThumbnail(entryOf(faceA)) == whole && libraryThumbnail(entryOf(faceB)) == whole);
            CHECK(setLibraryThumbnail(entryOf(faceB), photo).isEmpty());
            const QString ownB = shelf + "/thumbnails/The Simpsons (1991) (Platform) (Disc) (Face B) [Original].bmp";
            CHECK(QFile::exists(ownB));
            CHECK(libraryThumbnail(entryOf(faceA)) == whole && libraryThumbnail(entryOf(faceB)) == ownB);
            // A side's picture is not another side's; it is the same
            // side's, filed elsewhere or released otherwise.
            CHECK(QFile::remove(whole));
            CHECK(libraryThumbnail(entryOf(faceA)).isEmpty());
            LibraryEntry crackB = entryOf(faceB);
            crackB.release = "Crack";
            crackB.subcategory = "Action";
            CHECK(libraryThumbnailName(crackB) == "The Simpsons (1991) (Action) (Disc) (Face B) [Crack]");
            CHECK(libraryThumbnail(crackB) == ownB);
            CHECK(QFile::remove(ownB) && QFile::remove(faceA) && QFile::remove(faceB));
        }

        // The thumbnail view: a box to tick shows the tab's programs as
        // their pictures instead of the list, in the same order, and back.
        {
            auto* view = dialog.findChild<QCheckBox*>("ckThumbnailView");
            auto* grid = dialog.findChild<QListWidget*>("lvThumbnails");
            auto* count = dialog.findChild<QLabel*>("lCount");
            auto* size = dialog.findChild<QSlider*>("slThumbnailSize");
            CHECK(view && grid && count && size);
            if (!view || !grid || !count || !size)
                return checkSummary("gui_library");
            const auto tiles = [&] {
                QStringList titles;
                for (int i = 0; i < grid->count(); ++i)
                    titles << grid->item(i)->text();
                return titles;
            };
            const auto colourOf = [&](int tile) {
                return grid->viewport()->grab().toImage().pixelColor(grid->visualItemRect(grid->item(tile)).center());
            };
            CHECK(!dialog.thumbnailView() && !view->isChecked());
            CHECK(programs->isVisible() && !grid->isVisible());
            dialog.resize(1100, 760);  // room for the tiles
            CHECK(!size->isVisible());
            CHECK(dialog.setCategory("Games") && dialog.select("Sorcery+"));
            view->click();
            CHECK(dialog.thumbnailView() && view->isChecked());
            CHECK(grid->isVisible() && !programs->isVisible());
            CHECK(tabs_shown(dialog));
            // A slider sets how large the pictures are: boxes of 360
            // pixels to start with, a margin around each.
            CHECK(size->isVisible() && size->value() == 360 && dialog.thumbnailSize() == 360);
            CHECK(size->minimum() == 120 && size->maximum() == 600);
            QTest::qWait(50);
            CHECK(grid->visualItemRect(grid->item(0)).size() == QSize(372, 372));
            size->setValue(240);
            CHECK_EQ(dialog.thumbnailSize(), 240);
            dialog.setThumbnailSize(5000);
            CHECK(dialog.thumbnailSize() == 600 && size->value() == 600);
            dialog.setThumbnailSize(240);
            CHECK(size->value() == 240);
            CHECK(tiles() == (QStringList{"Gryzor", "Gryzor", "Gryzor", "Sorcery+"}));
            CHECK(tiles() == dialog.listedTitles());  // each of them has a picture
            // What was selected in the list is selected here.
            CHECK(dialog.selectedTitles() == QStringList{"Sorcery+"} && grid->item(3)->isSelected());
            CHECK(insertA->isEnabled() && button->isEnabled());
            // The pointer over a picture brings up the name and what the
            // columns said.
            const QString said = grid->item(1)->toolTip();
            CHECK(said.contains("<b>Gryzor</b>"));
            for (const char* part : {"Sub-category:", "Run and Gun", "Year:", "1987", "Type:", "Disc", "Release Type:",
                                     "Original", "Notes:", "UK"})
                CHECK(said.contains(part));
            CHECK(!said.contains("AI:") && !said.contains("Thumbnail"));
            CHECK(grid->item(0)->toolTip().contains("Tape") && !grid->item(0)->toolTip().contains("Release Type"));
            // The pictures come as they are read: here all red.
            QTest::qWait(50);
            CHECK(QTest::qWaitFor([&] { return colourOf(0) == QColor(Qt::red) && colourOf(3) == QColor(Qt::red); }, 3000));
            // Each in a box of the 240 pixels asked for.
            CHECK(grid->visualItemRect(grid->item(0)).size() == QSize(252, 252));
            CHECK(grid->viewport()->rect().contains(grid->visualItemRect(grid->item(3))));
            if (!prefix.isEmpty())
                dialog.grab().save(prefix + "thumbnail_view.png");
            // The search and the order are the list's.
            dialog.setSearch("gry");
            CHECK(tiles() == (QStringList{"Gryzor", "Gryzor", "Gryzor"}));
            dialog.setSearch(QString());
            dialog.setSort(0, Qt::DescendingOrder);
            CHECK(tiles() == (QStringList{"Sorcery+", "Gryzor", "Gryzor", "Gryzor"}));
            dialog.setSort(0, Qt::AscendingOrder);
            CHECK(tiles() == (QStringList{"Gryzor", "Gryzor", "Gryzor", "Sorcery+"}));
            // The tabs too. A program without a picture is not shown: no
            // tile stands in for it.
            CHECK(count->text() == "4 of 4");
            CHECK(dialog.setCategory(QString()));
            CHECK(dialog.listedTitles() == QStringList{"Zub"} && tiles().isEmpty());
            CHECK(dialog.selectedTitles().isEmpty() && !insertA->isEnabled() && !button->isEnabled());
            CHECK(count->text() == "0 of 1");
            // A program whose picture is taken away leaves the view.
            CHECK(dialog.setCategory("Games"));
            {
                const QString taken = shelf + "/thumbnails/Sorcery+ (1985) (Platform) (Disc).png";
                const QString aside = folder.filePath("aside.png");
                CHECK(QFile::rename(taken, aside));
                dialog.setFolders({shelf});
                CHECK(dialog.setCategory("Games"));
                CHECK(tiles() == (QStringList{"Gryzor", "Gryzor", "Gryzor"}) && dialog.listedTitles().size() == 4);
                CHECK(count->text() == "3 of 4");
                CHECK(QFile::rename(aside, taken));
                dialog.setFolders({shelf});
                CHECK(dialog.setCategory("Games"));
                CHECK(tiles() == (QStringList{"Gryzor", "Gryzor", "Gryzor", "Sorcery+"}));
            }
            // Several tiles selected, with the mouse, are given a picture.
            CHECK(dialog.setCategory("Games"));
            QTest::qWait(50);
            QTest::mouseClick(grid->viewport(), Qt::LeftButton, {}, grid->visualItemRect(grid->item(0)).center());
            QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::ControlModifier,
                              grid->visualItemRect(grid->item(3)).center());
            CHECK(dialog.selectedTitles() == (QStringList{"Gryzor", "Sorcery+"}));
            CHECK(!insertA->isEnabled() && button->isEnabled());
            CHECK(dialog.setThumbnail(photo));
            CHECK(dialog.selectedTitles() == (QStringList{"Gryzor", "Sorcery+"}));
            CHECK(QTest::qWaitFor([&] { return colourOf(0) == QColor(Qt::blue) && colourOf(3) == QColor(Qt::blue); }, 3000));
            CHECK(colourOf(1) == QColor(Qt::red));
            // Back to the list, with what was selected; and the pictures
            // as they were, for what follows.
            view->click();
            CHECK(!dialog.thumbnailView() && programs->isVisible() && !grid->isVisible());
            CHECK(dialog.selectedTitles() == (QStringList{"Gryzor", "Sorcery+"}));
            CHECK(dialog.setThumbnail(cover));
            dialog.setThumbnailView(true);
            CHECK(view->isChecked() && dialog.selectedTitles() == (QStringList{"Gryzor", "Sorcery+"}));
            // A double click on a tile, or Enter, puts the program in the
            // machine, as in the list.
            CHECK(dialog.select("Sorcery+") && dialog.selectedTitles() == QStringList{"Sorcery+"});
            dialog.setThumbnailView(false);
            CHECK(!view->isChecked() && !size->isVisible());
        }

        // A program filed elsewhere, or released otherwise, keeps the
        // picture of its title: within what it is on, and for the very
        // same year. A disc's picture is not a snapshot's, a tape's or a
        // cartridge's, nor that of the game of another year.
        const QString snapshot = shelf + "/Gryzor (1987).sna";
        CHECK(QFile::copy(sorcery, snapshot));
        CHECK(libraryThumbnailName(entryOf(snapshot)) == "Gryzor (1987) (Snapshot)");
        CHECK(libraryThumbnail(entryOf(snapshot)).isEmpty());
        {
            const QString crack = shelf + "/thumbnails/Gryzor (1987) (Run and Gun) (Disc) [Crack].png";
            const QString original = shelf + "/thumbnails/Gryzor (1987) (Run and Gun) (Disc) [Original].png";
            const QString onTape = shelf + "/thumbnails/Gryzor (1987) (Run and Gun) (Tape).png";
            CHECK(QFile::exists(crack) && QFile::exists(original) && QFile::exists(onTape));
            // Another release of the disc, filed elsewhere: a disc's.
            LibraryEntry other = entryOf(snapshot);
            other.kind = LibraryEntry::Disc;
            other.release = "Hack";
            other.subcategory = "Action";
            CHECK(libraryThumbnailName(other) == "Gryzor (1987) (Action) (Disc) [Hack]");
            CHECK(libraryThumbnail(other) == crack);
            // The tape's is the tape's own, and a cartridge has none.
            other.kind = LibraryEntry::Tape;
            CHECK(libraryThumbnail(other) == onTape);
            other.kind = LibraryEntry::Cartridge;
            CHECK(libraryThumbnail(other).isEmpty());
            // With the tape's picture gone, the tape does not take the disc's.
            CHECK(QFile::rename(onTape, onTape + ".kept"));
            other.kind = LibraryEntry::Tape;
            CHECK(libraryThumbnail(other).isEmpty());
            CHECK(libraryThumbnail(entryOf(disc)) == original);
            CHECK(QFile::rename(onTape + ".kept", onTape));
            // The year has to be the same, on the same medium too.
            other.kind = LibraryEntry::Disc;
            other.year = "1989";
            CHECK(libraryThumbnail(other).isEmpty());
            other.year.clear();
            CHECK(libraryThumbnail(other).isEmpty());
            // A picture named by hand, which says nothing of what it is on,
            // stands for them all, of that year.
            const QString byHand = shelf + "/thumbnails/Gryzor (1989).png";
            CHECK(QFile::copy(cover, byHand));
            other.year = "1989";
            CHECK(libraryThumbnail(other) == byHand);
            other.kind = LibraryEntry::Cartridge;
            CHECK(libraryThumbnail(other) == byHand);
            other.year = "1987";
            CHECK(libraryThumbnail(other).isEmpty());
            CHECK(QFile::remove(byHand));
        }
        LibraryEntry sequel = entryOf(snapshot);
        sequel.title = "Gryzor 2";
        CHECK(libraryThumbnail(sequel).isEmpty());
        sequel.title = "Gryzor";
        sequel.year = "1989";
        CHECK(libraryThumbnail(sequel).isEmpty());
    }
    // The Print Screen key: the picture on the screen becomes the Library's
    // thumbnail of the program in the machine. If it has one already, a
    // question: replace it, or keep this picture beside it in the
    // "snapshots" folder.
    {
        const QString shots = folder.filePath("shots");
        QDir().mkpath(shots + "/Games/Platform");
        const QString zub = shots + "/Games/Platform/Zub (UK) (Face A) (1986) [Original].dsk";
        CHECK(QFile::copy(gryzor, zub));
        Settings own = window.settings();
        own.libraryFolders = QStringList{shots};
        window.applySettings(own);
        QAction* shot = nullptr;
        for (QAction* each : window.findChildren<QAction*>()) {
            if (each->text() == "Screen to &Thumbnail")
                shot = each;
            else if (each->text() == "R&emove Tape" && each->isEnabled())
                each->trigger();
        }
        CHECK(shot != nullptr);
        if (!shot)
            return checkSummary("gui_library");
        // Ctrl+T, the Print Screen key itself and with Ctrl, for what a
        // desktop lets through; and a button of the control panel.
        CHECK(shot->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_T)));
        CHECK(shot->shortcuts().contains(QKeySequence(Qt::Key_Print)));
        CHECK(shot->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Print)));
        QToolButton* camera = nullptr;
        for (QToolButton* each : window.findChildren<QToolButton*>())
            if (each->toolTip().startsWith("Screen to Thumbnail"))
                camera = each;
        CHECK(camera && camera->isEnabled());
        const auto press = [&](const char* button, bool* paused = nullptr) {
            Modals modals([&](QWidget* modal) {
                auto* box = qobject_cast<QMessageBox*>(modal);
                if (!box || !button || box->objectName() != "ThumbnailQuestion")
                    return modal->close(), void();
                if (paused)
                    *paused = emulator.isPaused();
                if (auto* chosen = box->findChild<QPushButton*>(button))
                    chosen->click();
                else
                    box->reject();
            });
            shot->trigger();
            return modals.seen();
        };
        // Nothing in the machine: said, and nothing made.
        window.discs()->remove(0);
        window.discs()->remove(1);
        CHECK(window.programInMachine().isEmpty());
        CHECK_EQ(press(nullptr), 1);
        CHECK(!QDir(shots + "/thumbnails").exists());
        // A disc in drive A: with no thumbnail yet it gets one at once,
        // under its name, the side with it.
        CHECK(window.insertDiscFile(0, zub));
        CHECK(window.programInMachine() == zub);
        const QString thumbnail = shots + "/thumbnails/Zub (1986) (Platform) (Disc) (Face A) [Original].png";
        // By the key, as a key pressed in the window; then by the button.
        // (The window has to be the one in front for its keys to count:
        // other windows have come and gone in this test.)
        window.activateWindow();
        window.screen()->setFocus();
        CHECK(QTest::qWaitForWindowActive(&window, 3000));
        QTest::keyClick(window.screen(), Qt::Key_T, Qt::ControlModifier);
        CHECK(QFile::exists(thumbnail));
        // The Print Screen key does the same when the desktop lets it
        // through, with Ctrl or without.
        CHECK(QFile::remove(thumbnail));
        QTest::keyClick(window.screen(), Qt::Key_Print, Qt::ControlModifier);
        CHECK(QFile::exists(thumbnail));
        CHECK(QFile::remove(thumbnail));
        QTest::keyClick(window.screen(), Qt::Key_Print);
        CHECK(QFile::exists(thumbnail));
        CHECK(QFile::remove(thumbnail));
        if (camera)
            camera->click();
        CHECK(QFile::exists(thumbnail));
        const QImage made(thumbnail);
        CHECK(!made.isNull() && made.width() >= 384 && made.width() <= 960 && made.height() > 200);
        CHECK(libraryThumbnail(libraryEntryIn(zub, {shots})) == thumbnail);
        // It has one now: the question, the machine standing still
        // meanwhile. Kept beside it: in "snapshots", numbered.
        bool paused = false;
        CHECK_EQ(press("bAdd", &paused), 1);
        CHECK(paused && !emulator.isPaused());
        const QString beside = shots + "/snapshots/Zub (1986) (Platform) (Disc) (Face A) [Original] %1.png";
        CHECK(QFile::exists(beside.arg("01")) && !QFile::exists(beside.arg("02")));
        CHECK_EQ(press("bAdd"), 1);
        CHECK(QFile::exists(beside.arg("02")));
        CHECK_EQ(QDir(shots + "/thumbnails").entryList(QDir::Files).size(), 1);
        // In its place: a small picture put there by hand is gone, the
        // screen's is there, and there is still only one.
        CHECK(QImage(8, 8, QImage::Format_RGB32).save(thumbnail));
        CHECK_EQ(press("bReplace"), 1);
        CHECK(QImage(thumbnail).width() >= 384);
        CHECK_EQ(QDir(shots + "/thumbnails").entryList(QDir::Files).size(), 1);
        CHECK_EQ(QDir(shots + "/snapshots").entryList(QDir::Files).size(), 2);
        // Neither: nothing changes.
        CHECK(QImage(8, 8, QImage::Format_RGB32).save(thumbnail));
        CHECK_EQ(press("bNeither"), 1);
        CHECK_EQ(QImage(thumbnail).width(), 8);
        CHECK_EQ(QDir(shots + "/snapshots").entryList(QDir::Files).size(), 2);
        // A program that is not in the library has its pictures beside it.
        const QString loose = folder.filePath("elsewhere/Lone (1985).dsk");
        QDir().mkpath(folder.filePath("elsewhere"));
        CHECK(QFile::copy(gryzor, loose));
        CHECK(window.insertDiscFile(0, loose));
        CHECK_EQ(press("bReplace"), 0);
        CHECK(QFile::exists(folder.filePath("elsewhere/thumbnails/Lone (1985) (Disc).png")));

        // A cartridge is a program too. Of several in the machine, the one
        // put in last is meant; when it goes, the one that is left.
        QDir().mkpath(shots + "/Games/Action");
        const QString cartridge = shots + "/Games/Action/Burnin Rubber (1990).cpr";
        {
            // One block of sixteen kilobytes, in a RIFF file of the kind.
            QByteArray image("RIFF");
            const auto number = [](quint32 value) {
                return QByteArray(1, char(value)) + char(value >> 8) + char(value >> 16) + char(value >> 24);
            };
            image += number(4 + 8 + 0x4000) + "AMS!" + "cb00" + number(0x4000) + QByteArray(0x4000, '\0');
            QFile file(cartridge);
            CHECK(file.open(QIODevice::WriteOnly) && file.write(image) == image.size());
        }
        CHECK(window.insertDiscFile(0, zub));
        CHECK(window.programInMachine() == zub);
        {
            // Whatever the machine has to say of its ROMs is not the point.
            Modals modals([](QWidget* modal) { modal->close(); });
            window.insertCartridgeFile(cartridge);
        }
        CHECK(window.programInMachine() == cartridge);
        CHECK_EQ(press("bReplace"), 0);
        CHECK(QFile::exists(shots + "/thumbnails/Burnin Rubber (1990) (Action) (Cartridge).png"));
        CHECK(libraryThumbnailName(libraryEntryIn(cartridge, {shots})) == "Burnin Rubber (1990) (Action) (Cartridge)");
        CHECK(window.insertDiscFile(0, zub));
        CHECK(window.programInMachine() == zub);
        window.discs()->remove(0);
        CHECK(window.programInMachine() == cartridge);
    }
    return checkSummary("gui_library");
}
