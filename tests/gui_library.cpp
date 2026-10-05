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
#include <QTimer>
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

void testNames()
{
    LibraryEntry entry = libraryEntry("/games/Gryzor (UK) (1987) (CPM) [Original].dsk");
    CHECK(entry.title == "Gryzor");
    CHECK(entry.year == "1987");
    CHECK(entry.details == "UK, CPM, Original");
    CHECK(entry.kind == LibraryEntry::Disc);
    CHECK(entry.path == "/games/Gryzor (UK) (1987) (CPM) [Original].dsk");

    entry = libraryEntry("/games/Last Ninja, The (1988)(System 3)(fr)[cr].DSK");
    CHECK(entry.title == "The Last Ninja");
    CHECK(entry.year == "1988");
    CHECK(entry.details == "System 3, fr, cr");
    CHECK(entry.kind == LibraryEntry::Disc);

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

    // ROM images are not needed: the machine is not started.
    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();

    // Two folders of programs: discs, one of them in a sub-folder, a
    // snapshot, and a file that is neither.
    const QString games = folder.filePath("games"), more = folder.filePath("more");
    QDir().mkpath(games + "/sub");
    QDir().mkpath(more);
    const QString gryzor = games + "/Gryzor (UK) (1987) (CPM) [Original].dsk";
    const QString ninja = games + "/sub/Last Ninja, The (1988)(System 3).dsk";
    const QString boulder = more + "/boulder dash.dsk";
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
    return checkSummary("gui_library");
}
