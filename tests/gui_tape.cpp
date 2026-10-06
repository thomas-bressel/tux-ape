// The cassette from the application: a tape image put in the deck, the Tape
// menu, the Tape Control window, and a tape taken from the Library. Runs
// without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_tape [prefix]   also saves a picture of the Tape Control window as
//                       <prefix>tape.png

#include <algorithm>
#include <functional>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QLabel>
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
    // play; Record needs none.
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
    CHECK(!rewind->isEnabled() && !remove->isEnabled() && !play->isEnabled());
    CHECK(record->isEnabled() && record->isCheckable() && !record->isChecked());
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
    auto* recordKey = dialog->findChild<QToolButton*>("bRecord");
    auto* recordingPanel = dialog->findChild<QWidget*>("pRecording");
    CHECK(recordKey->isEnabled() && !recordKey->isChecked());
    CHECK(recordingPanel && !recordingPanel->isVisible());
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

    // Record: what the machine writes to tape goes into a new CDT file.
    // The Record key asks for the file first; no file, no recording.
    {
        Modals modals([](QWidget* modal) { modal->close(); });
        recordKey->click();
        CHECK_EQ(modals.seen(), 1);
        CHECK(!record->isChecked() && !recordKey->isChecked() && window.tapeRecording().isEmpty());
        CHECK(deckOf(emulator).playing);
    }
    const QString saved = folder.filePath("tapes/saved.cdt");
    CHECK(window.recordTape(saved));
    CHECK(window.tapeRecording() == saved);
    CHECK(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.tape().recording(); }));
    CHECK(!deckOf(emulator).playing);
    CHECK(record->isChecked() && recordKey->isChecked());
    // Record holds Play down: only Stop and the eject key answer.
    CHECK(!play->isEnabled() && !rewind->isEnabled() && remove->isEnabled());
    CHECK(!playKey->isEnabled() && playKey->isChecked() && !rewindKey->isEnabled() && stopKey->isEnabled());
    CHECK(recordingPanel->isVisible());
    CHECK(dialog->findChild<QLineEdit*>("edRecord")->text() == "saved.cdt");
    // A program that moves the write line with the motor running, for a
    // second and a half of the machine's time.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        // 8000 DI / LD BC,#F782 / OUT (C),C / LD B,#F6
        // 8008 LD A,#10 / OUT (C),A / LD A,#30 / OUT (C),A / JR 8008
        const uint8_t program[] = {0xF3, 0x01, 0x82, 0xF7, 0xED, 0x49, 0x06, 0xF6, 0x3E, 0x10,
                                   0xED, 0x79, 0x3E, 0x30, 0xED, 0x79, 0x18, 0xF6};
        cpc.out(0x7F00, 0x8C);
        for (size_t i = 0; i < sizeof program; ++i)
            cpc.memory().write(static_cast<uint16_t>(0x8000 + i), program[i]);
        cpc.cpu().pc = 0x8000;
        cpc.run(1500000);
    });
    dialog->refresh();
    CHECK(led->property("lit").toBool());
    CHECK(dialog->findChild<QLabel*>("lRecordTime")->text() == "0:01");
    if (!prefix.isEmpty()) {
        QTest::qWait(50);
        dialog->grab().save(prefix + "tape_recording.png");
    }
    // Stop writes the file, and the deck holds that tape, ready to load.
    stopKey->click();
    CHECK(window.tapeRecording().isEmpty());
    CHECK(!record->isChecked() && !recordKey->isChecked() && !recordingPanel->isVisible());
    CHECK(!emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.tape().recording(); }));
    CHECK(QFile::exists(saved));
    CHECK(name->text() == "saved.cdt");
    CHECK(deckOf(emulator).loaded && deckOf(emulator).playing);
    CHECK(blocks->count() > 100 && blocks->itemText(1).endsWith("Sequence of Pulses"));
    CHECK(play->isEnabled() && playKey->isEnabled());
    // Pulses of 6 and 9 microseconds, as the program made them.
    {
        const std::vector<uint32_t> pulses =
            emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.tape().tape().pulses(); });
        size_t at = 0;
        while (at < pulses.size() && (pulses[at] & tuxape::Tape::kLength) > 1000)
            ++at;
        CHECK(pulses.size() > 100000 && at + 4 < pulses.size());
        if (at + 4 < pulses.size()) {
            const uint32_t first = pulses[at + 1] & tuxape::Tape::kLength, second = pulses[at + 2] & tuxape::Tape::kLength;
            CHECK_EQ(std::min(first, second), 21);
            CHECK_EQ(std::max(first, second), 32);
        }
    }
    // Nothing written: no file, and a word to the user; the tape in the
    // deck stays.
    const QString empty = folder.filePath("tapes/empty.cdt");
    // The program is left for a loop that writes nothing, the motor off.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.memory().write(0x9000, 0x18);
        cpc.memory().write(0x9001, 0xFE);
        cpc.cpu().pc = 0x9000;
        cpc.out(0xF600, 0x00);
    });
    CHECK(window.recordTape(empty));
    emulator.withMachine([](tuxape::Cpc& cpc) { cpc.run(100000); });
    {
        Modals modals([&](QWidget* modal) {
            auto* box = qobject_cast<QMessageBox*>(modal);
            CHECK(box && box->text().contains("empty.cdt"));
            modal->close();
        });
        CHECK(!window.stopTapeRecording());
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(!QFile::exists(empty));
    CHECK(name->text() == "saved.cdt" && deckOf(emulator).loaded);
    // A tape put in the deck ends a recording, whose file is still made.
    const QString third = folder.filePath("tapes/third.cdt");
    CHECK(window.recordTape(third));
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.cpu().pc = 0x8000;
        cpc.run(20000);
    });
    CHECK(window.insertTapeFile(path));
    CHECK(window.tapeRecording().isEmpty() && !record->isChecked());
    CHECK(QFile::exists(third));
    CHECK(name->text() == "Harrier Attack (1984)(Durell).cdt");
    return checkSummary("gui_tape");
}
