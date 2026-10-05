// The cassette from the application: a tape image put in the deck, the Tape
// menu, the Tape Control window, and a tape taken from the Library. Runs
// without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_tape [prefix]   also saves a picture of the Tape Control window as
//                       <prefix>tape.png

#include <functional>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
#include "emulator.h"
#include "librarydialog.h"
#include "mainwindow.h"
#include "settings.h"
#include "tapedialog.h"

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

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text))
            return action;
    return nullptr;
}

struct Deck {
    bool loaded, playing;
    int block;
};

Deck deckOf(Emulator& emulator)
{
    return emulator.withMachine([](tuxape::Cpc& cpc) {
        return Deck{cpc.tape().loaded(), cpc.tape().playing(), static_cast<int>(cpc.tape().block())};
    });
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    // ROM images are not needed: the machine is not started.
    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();

    // A tape of four blocks: a silence, a tone, a note, another tone.
    QDir().mkpath(folder.filePath("tapes"));
    const QString path = folder.filePath("tapes/Harrier Attack (1984)(Durell).cdt");
    {
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("ZXTape!\x1A\x01\x14", 10));
        file.write(QByteArray("\x20\xF4\x01", 3));                   // half a second
        file.write(QByteArray("\x12\xE8\x03\xD0\x07", 5));           // 2000 pulses of 1000
        file.write(QByteArray("\x30\x05hello", 7));
        file.write(QByteArray("\x12\xE8\x03\xD0\x07", 5));
    }
    const QString other = folder.filePath("tapes/notes.txt");
    {
        QFile file(other);
        CHECK(file.open(QIODevice::WriteOnly));
        file.write("not a tape");
    }

    // The Tape menu: without a tape there is nothing to rewind, remove or
    // play. Writing to tape is to come.
    QAction* control = actionNamed(window, "Show Tape Control");
    QAction* insert = actionNamed(window, "Insert Tape Image...");
    QAction* rewind = actionNamed(window, "Rewind Tape");
    QAction* remove = actionNamed(window, "Remove Tape");
    QAction* play = actionNamed(window, "Press Play");
    QAction* record = actionNamed(window, "Press Record");
    CHECK(control && insert && rewind && remove && play && record);
    if (!(control && insert && rewind && remove && play && record))
        return checkSummary("gui_tape");
    CHECK(control->isEnabled() && insert->isEnabled());
    CHECK(insert->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F4));
    CHECK(!rewind->isEnabled() && !remove->isEnabled() && !play->isEnabled() && !record->isEnabled());
    QToolButton* toolButton = nullptr;
    for (QToolButton* candidate : window.findChildren<QToolButton*>())
        if (candidate->toolTip() == "Tape Control")
            toolButton = candidate;
    CHECK(toolButton && toolButton->isEnabled());

    // A file that is not a tape is refused, with a word to the user.
    {
        Modals modals([](QWidget* modal) {
            CHECK(qobject_cast<QMessageBox*>(modal) != nullptr);
            modal->close();
        });
        CHECK(!window.insertTapeFile(other));
        CHECK_EQ(modals.seen(), 1);
        CHECK(!deckOf(emulator).loaded);
    }
    // A tape goes in rewound, with Play pressed.
    CHECK(window.insertTapeFile(path));
    CHECK(deckOf(emulator).loaded && deckOf(emulator).playing);
    CHECK_EQ(deckOf(emulator).block, 0);
    CHECK(rewind->isEnabled() && remove->isEnabled() && play->isEnabled());

    // The Tape Control window, from the toolbar.
    CHECK(window.findChild<TapeDialog*>() == nullptr);
    if (toolButton)
        toolButton->click();
    auto* dialog = window.findChild<TapeDialog*>();
    CHECK(dialog && dialog->isVisible());
    if (!dialog)
        return checkSummary("gui_tape");
    auto* name = dialog->findChild<QLineEdit*>("edTape");
    auto* blocks = dialog->findChild<QComboBox*>("cbBlock");
    auto* playKey = dialog->findChild<QToolButton*>("bPlay");
    auto* stopKey = dialog->findChild<QToolButton*>("bStop");
    auto* rewindKey = dialog->findChild<QToolButton*>("bRewind");
    auto* ejectKey = dialog->findChild<QToolButton*>("bEject");
    auto* led = dialog->findChild<QFrame*>("ledMotor");
    CHECK(name->text() == "Harrier Attack (1984)(Durell).cdt");
    QStringList listed;
    for (int i = 0; i < blocks->count(); ++i)
        listed << blocks->itemText(i);
    CHECK(listed == (QStringList{"1 - Pause", "2 - Pure Tone", "3 - Description", "4 - Pure Tone"}));
    CHECK_EQ(blocks->currentIndex(), 0);
    CHECK(playKey->isEnabled() && playKey->isChecked());
    CHECK(stopKey->isEnabled() && rewindKey->isEnabled() && ejectKey->isEnabled());
    CHECK(!dialog->findChild<QToolButton*>("bRecord")->isEnabled());
    CHECK(!led->property("lit").toBool());
    if (!prefix.isEmpty()) {
        QTest::qWait(50);
        dialog->grab().save(prefix + "tape.png");
    }

    // The keys.
    stopKey->click();
    CHECK(!deckOf(emulator).playing && !playKey->isChecked());
    playKey->click();
    CHECK(deckOf(emulator).playing && playKey->isChecked());
    // A block chosen in the list is where the tape goes.
    emit blocks->activated(3);
    CHECK_EQ(deckOf(emulator).block, 3);
    CHECK_EQ(blocks->currentIndex(), 3);
    rewindKey->click();
    CHECK_EQ(deckOf(emulator).block, 0);
    CHECK_EQ(blocks->currentIndex(), 0);
    // The light is on while the motor turns the tape; the window follows
    // the tape by itself. Here the tape is moved on by six tenths of a
    // second, past its silence.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.tape().setMotor(true, cpc.microseconds());
        cpc.tape().level(cpc.microseconds() + 600000);
    });
    CHECK(QTest::qWaitFor([&] { return led->property("lit").toBool() && blocks->currentIndex() == 1; }, 2000));
    emulator.withMachine([](tuxape::Cpc& cpc) { cpc.tape().setMotor(false, cpc.microseconds()); });
    // The menu's entries do the same as the keys.
    rewind->trigger();
    CHECK_EQ(deckOf(emulator).block, 0);
    stopKey->click();
    play->trigger();
    CHECK(deckOf(emulator).playing);

    // Eject, and the menu's Remove Tape.
    ejectKey->click();
    CHECK(!deckOf(emulator).loaded);
    CHECK(name->text().isEmpty());
    CHECK_EQ(blocks->count(), 0);
    CHECK(!playKey->isEnabled() && !ejectKey->isEnabled());
    CHECK(!rewind->isEnabled() && !remove->isEnabled() && !play->isEnabled());
    CHECK(window.insertTapeFile(path));
    CHECK(name->text() == "Harrier Attack (1984)(Durell).cdt");
    CHECK_EQ(blocks->count(), 4);
    remove->trigger();
    CHECK(!deckOf(emulator).loaded && name->text().isEmpty());

    // The Library lists tapes, and puts them in the deck.
    const LibraryEntry entry = libraryEntry(path);
    CHECK(entry.kind == LibraryEntry::Tape && entry.title == "Harrier Attack" && entry.year == "1984");
    Settings settings = window.settings();
    settings.libraryFolders = QStringList{folder.filePath("tapes")};
    window.applySettings(settings);
    {
        Modals modals([&](QWidget* modal) {
            auto* library = qobject_cast<LibraryDialog*>(modal);
            CHECK(library && library->listedTitles() == QStringList{"Harrier Attack"});
            if (!library)
                return modal->close(), void();
            CHECK(library->findChild<QPushButton*>("bInsertA")->text() == "&Insert");
            CHECK(!library->findChild<QPushButton*>("bInsertB")->isEnabled());
            CHECK(!library->choose(1));
            CHECK(library->choose());
        });
        actionNamed(window, "Library...")->trigger();
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(deckOf(emulator).loaded && deckOf(emulator).playing);
    CHECK(name->text() == "Harrier Attack (1984)(Durell).cdt");
    return checkSummary("gui_tape");
}
