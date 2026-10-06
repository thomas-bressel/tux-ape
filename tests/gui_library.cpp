// The Library window: what file names say, the programs found in the user's
// folders, the search, and a program put into the machine from it. Runs
// without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_library [prefix]   also saves a picture of the window as
//                          <prefix>library.png

#include <functional>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
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
#include "discmanager.h"
#include "emulator.h"
#include "librarydialog.h"
#include "mainwindow.h"
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
        CHECK(programs && programs->columnCount() == 7 && programs->headerItem()->text(5) == "AI");
        CHECK(programs && programs->topLevelItem(0)->text(6).isEmpty());  // not among the notes as well
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
        CHECK(programs && programs->topLevelItem(1)->text(6) == "UK, CPM");
        dialog.setSearch("original");
        CHECK(dialog.listedTitles() == QStringList{"Gryzor"});
        dialog.setSearch(QString());
        CHECK(programs && !(programs->topLevelItem(0)->flags() & Qt::ItemIsUserCheckable));
        CHECK(count->text() == "4 of 4");
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
        CHECK(libraryCategories() == (QStringList{"Games", "Educational", "Utilities", "Demos", "Compilations", "Miscellaneous"}));
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
    return checkSummary("gui_library");
}
