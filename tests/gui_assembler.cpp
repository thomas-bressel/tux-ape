// The assembler's window: sources in tabs, assembled into the machine and
// run there, errors listed and shown in their source, breakpoints set on
// lines, symbols, find and replace, options kept in the settings. Runs
// without a display (QT_QPA_PLATFORM=offscreen); no ROM is needed.
//
//   gui_assembler [prefix]   also saves pictures of the window and of the
//                            symbols as <prefix>assembler.png and
//                            <prefix>symbols.png

#include <functional>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include "assemblerdialog.h"
#include "check.h"
#include "core/cpc.h"
#include "core/disc.h"
#include "core/discfiles.h"
#include "core/setup.h"
#include "debuggerdialog.h"
#include "discmanager.h"
#include "emulator.h"
#include "mainwindow.h"
#include "settings.h"

namespace {

// Deals with the window a call opens: `handle` is given it as it comes up,
// and has to close it.
class Modals {
public:
    explicit Modals(std::function<void(QWidget*)> handle)
    {
        QObject::connect(&timer_, &QTimer::timeout, [this, handle] {
            // Two windows made one after the other may sit at the same
            // address: only one that is still up is "the same".
            QWidget* modal = QApplication::activeModalWidget();
            if (!modal)
                last_ = nullptr;
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

void closeBox(QWidget* modal)
{
    static_cast<QDialog*>(modal)->accept();
}

// Answers a question with one of its buttons.
std::function<void(QWidget*)> answer(QMessageBox::StandardButton button)
{
    return [button](QWidget* modal) {
        auto* box = qobject_cast<QMessageBox*>(modal);
        CHECK(box && box->button(button));
        if (box && box->button(button))
            box->button(button)->click();
        else
            static_cast<QDialog*>(modal)->reject();
    };
}

void writeHost(const QString& path, const QByteArray& data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QByteArray readHost(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("?");
}

QAction* actionNamed(QWidget& window, const char* text, int which = 0)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text) && which-- == 0)
            return action;
    return nullptr;
}

// Puts a text in the place of an editor's, as typing it would: the file
// then has changes to save.
void type(CodeEditor* editor, const QString& text)
{
    QTextCursor cursor(editor->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();
    emulator.setSpeedPercent(1000);
    emulator.start();
    // The ROMs out of the way and no interrupts: only what is assembled runs.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.out(0x7F00, 0x8C);
        cpc.cpu().iff1 = cpc.cpu().iff2 = false;
        cpc.cpu().sp = 0xBFF0;
    });
    const auto peek = [&](uint16_t address) {
        return emulator.withMachine([&](tuxape::Cpc& cpc) { return cpc.memory().readRam(address); });
    };
    const auto bytes = [&](uint16_t address, int count) {
        QByteArray out;
        for (int i = 0; i < count; ++i)
            out += static_cast<char>(peek(static_cast<uint16_t>(address + i)));
        return out.toHex();
    };
    const auto pc = [&] { return emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.cpu().pc; }); };
    const auto sp = [&] { return emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.cpu().sp; }); };

    // Assembling, or running, when it is meant to go well: if it does not,
    // the output window is closed and the errors are shown here.
    AssemblerDialog* assembler = nullptr;
    const auto went = [&](bool run) {
        Modals modals(closeBox);
        const bool ok = run ? assembler->run() : assembler->assemble();
        if (!ok)
            std::printf("  errors: %s\n", qPrintable(assembler->errorTexts().join("; ")));
        return ok;
    };
    const auto assembles = [&] { return went(false); };
    const auto runs = [&] { return went(true); };

    // The menu entry and the toolbar button open the window.
    QAction* show = actionNamed(window, "Show Assembler");
    CHECK(show && show->isEnabled() && show->shortcut() == QKeySequence(Qt::Key_F3));
    QToolButton* button = nullptr;
    for (QToolButton* candidate : window.findChildren<QToolButton*>())
        if (candidate->toolTip() == "Assembler (F3)")
            button = candidate;
    CHECK(button && button->isEnabled());
    CHECK(window.assembler() == nullptr);
    if (!show)
        return checkSummary("gui_assembler");
    show->trigger();
    assembler = window.assembler();
    CHECK(assembler && assembler->isVisible());
    if (!assembler)
        return checkSummary("gui_assembler");
    CHECK_EQ(assembler->fileCount(), 1);
    CHECK(assembler->tabText(0) == "Untitled");
    auto* position = assembler->findChild<QLabel*>("lPos");
    CHECK(position && position->text() == "1 : 1");
    QAction* assembleAction = actionNamed(*assembler, "Assemble", 1);  // after the menu itself
    QAction* runAction = actionNamed(*assembler, "Run");
    CHECK(assembleAction && assembleAction->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F9));
    CHECK(runAction && runAction->shortcut() == QKeySequence(Qt::Key_F9));
    CHECK(actionNamed(*assembler, "Read BASIC") && !actionNamed(*assembler, "Read BASIC")->isEnabled());
    for (const char* name : {"New", "Open", "Save", "Save As", "Save All", "Close", "Exit", "Undo", "Redo", "Find",
                             "Find Again", "Replace", "Goto Line number", "Clear all breakpoints",
                             "Goto next breakpoint", "Symbols", "Options"})
        CHECK(actionNamed(*assembler, name) && actionNamed(*assembler, name)->isEnabled());

    // A source with a mistake: the output window counts it, the list names
    // its line, and the line shows in the source.
    CodeEditor* editor = assembler->editor();
    type(editor, "org #8000\nld a,\nnop\n");
    CHECK(assembler->tabText(0) == "Untitled*");
    {
        QString outputText, errorsText, titleText;
        Modals modals([&](QWidget* modal) {
            titleText = modal->windowTitle();
            if (auto* output = modal->findChild<QPlainTextEdit*>("mOutput"))
                outputText = output->toPlainText();
            if (auto* label = modal->findChild<QLabel*>("lErrors"))
                errorsText = label->text();
            closeBox(modal);
        });
        CHECK(!assembler->assemble());
        CHECK_EQ(modals.seen(), 1);
        CHECK(titleText == "Assembling");
        CHECK(errorsText == "Errors : 1");
        CHECK(outputText.startsWith("TuxAPE Z80 Assembler\n\n000001  0000  (8000)        org #8000\n"));
        CHECK(outputText.contains("Untitled: Line 2 - Bad Expression"));
    }
    CHECK(assembler->errorTexts() == QStringList{"Untitled: Line 2 - Bad Expression"});
    CHECK_EQ(editor->errorLine(), 2);
    editor->gotoLine(3);
    CHECK(position && position->text() == "3 : 1");
    assembler->gotoError(0);
    CHECK_EQ(editor->line(), 2);
    CHECK_EQ(peek(0x8000), 0);  // nothing reaches the machine

    // Put right, it goes into memory; the output window stays away if the
    // options say so.
    assembler->setOptions(QString(), true, true);
    type(editor, "org #8000\nrun start\ncount equ 3\n.start\nld a,#42\nld (#9000),a\n.loop jr loop\n");
    CHECK_EQ(editor->errorLine(), 0);
    CHECK(assembles());
    CHECK(assembler->errorTexts().isEmpty());
    CHECK(bytes(0x8000, 7) == "3e4232009018fe");
    CHECK_EQ(peek(0x9000), 0);

    // Run starts it where its `run` says, the old program counter pushed.
    CHECK(runs());
    CHECK(QTest::qWaitFor([&] { return peek(0x9000) == 0x42; }, 5000));
    CHECK(pc() == 0x8005);
    CHECK_EQ(sp(), 0xBFEE);
    CHECK(!emulator.isPaused());

    // A breakpoint on a line is one at the address the line went to.
    editor->toggleBreakpoint(6);
    CHECK(editor->breakpoints() == QList<int>{6});
    CHECK_EQ(emulator.breakpoints().count(0x8002), 1);
    emulator.withMachine([](tuxape::Cpc& cpc) { cpc.memory().write(0x9000, 0); });
    CHECK(runs());
    CHECK(QTest::qWaitFor([&] { return emulator.isPaused(); }, 5000));
    CHECK_EQ(pc(), 0x8002);
    CHECK_EQ(peek(0x9000), 0);
    // The machine says so from its own thread: the debugger comes up a moment later.
    CHECK(QTest::qWaitFor([&] { return window.debugger() && window.debugger()->isVisible(); }, 5000));
    editor->toggleBreakpoint(6);
    CHECK_EQ(emulator.breakpoints().count(0x8002), 0);
    // On a line without code, it stands for the next line with some.
    editor->toggleBreakpoint(4);
    CHECK_EQ(emulator.breakpoints().count(0x8000), 1);
    editor->gotoLine(1);
    assembler->gotoNextBreakpoint();
    CHECK_EQ(editor->line(), 4);
    editor->clearBreakpoints();
    CHECK(emulator.breakpoints().empty());

    // `run` may name a breakpoint of its own.
    type(editor, "org #8000\nrun start,loop\n.start\nld a,#43\nld (#9000),a\n.loop jr loop\n");
    CHECK(runs());
    CHECK(QTest::qWaitFor([&] { return emulator.isPaused(); }, 5000));
    CHECK_EQ(pc(), 0x8005);
    CHECK_EQ(peek(0x9000), 0x43);
    // Without the directive there is nothing to run.
    type(editor, "org #8000\nnop\n");
    CHECK(!assembler->run());
    CHECK(!assembler->errorTexts().isEmpty() && assembler->errorTexts().last().startsWith("Nothing to run"));
    CHECK(emulator.breakpoints().empty());

    // Saved, the file gives its tab its name. Files it reads are looked
    // for beside it and in the library path, whatever the case of names.
    const QString mainPath = folder.filePath("prog.asm");
    CHECK(assembler->saveFileAs(0, mainPath));
    CHECK(assembler->tabText(0) == "Prog");
    CHECK(assembler->filePath(0) == QFileInfo(mainPath).absoluteFilePath());
    CHECK(readHost(mainPath).startsWith("org #8000"));
    CHECK(QDir(folder.path()).mkdir("lib"));
    writeHost(folder.filePath("lib/Defs.asm"), "value equ #55\r\nmacro put v\r\n db v\r\nmend\r\n");
    writeHost(folder.filePath("data.bin"), QByteArray("\x01\x02\x03", 3));
    assembler->setOptions(folder.filePath("nowhere") + ";" + folder.filePath("lib"), true, true);
    type(editor, "org #8100\nread \"defs.asm\"\nput value\nincbin \"data.bin\"\n"
                         "save \"mem.bin\",#8100,4\nwrite \"out.bin\"\ndb 7,8\n");
    CHECK(assembler->tabText(0) == "Prog*");
    CHECK(assembles());
    CHECK(assembler->errorTexts().isEmpty());
    CHECK(bytes(0x8100, 4) == "55010203");
    CHECK(readHost(folder.filePath("mem.bin")).toHex() == "55010203");
    CHECK(readHost(folder.filePath("out.bin")).toHex() == "0708");

    // An error in a file read in opens that file at its line.
    writeHost(folder.filePath("lib/bad.asm"), "nop\n ld a,oops\n");
    type(editor, "read \"bad.asm\"\n");
    {
        Modals modals(closeBox);
        CHECK(!assembler->assemble());
    }
    CHECK_EQ(assembler->errorTexts().size(), 1);
    CHECK(assembler->errorTexts().value(0).endsWith("bad.asm: Line 2 - Undefined Symbol: oops"));
    assembler->gotoError(0);
    CHECK_EQ(assembler->fileCount(), 2);
    CHECK_EQ(assembler->currentFile(), 1);
    CHECK(assembler->tabText(1) == "Bad");
    CHECK(assembler->editor() && assembler->editor()->line() == 2 && assembler->editor()->errorLine() == 2);
    // Put right in its tab, it is assembled as it stands there, saved or not.
    type(assembler->editor(), "nop\n ld a,1\n");
    CHECK(assembler->tabText(1) == "Bad*");
    assembler->setCurrentFile(0);
    CHECK(assembler->editor() == editor);
    CHECK(assembles());
    // Closing a file with changes asks first.
    {
        Modals modals(answer(QMessageBox::Cancel));
        CHECK(!assembler->closeFile(1));
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK_EQ(assembler->fileCount(), 2);
    // So does leaving the application.
    {
        Modals modals(answer(QMessageBox::Cancel));
        CHECK(!window.close());
        CHECK(modals.seen() >= 1);
    }
    CHECK(window.isVisible());
    {
        Modals modals(answer(QMessageBox::No));
        CHECK(assembler->closeFile(1));
    }
    CHECK_EQ(assembler->fileCount(), 1);
    CHECK(readHost(folder.filePath("lib/bad.asm")) == "nop\n ld a,oops\n");
    // Opening it again, and again: one tab.
    CHECK(assembler->openFile(folder.filePath("lib/Defs.asm")));
    CHECK(assembler->openFile(folder.filePath("lib/Defs.asm")));
    CHECK_EQ(assembler->fileCount(), 2);
    CHECK(assembler->tabText(1) == "Defs");
    CHECK(assembler->editor(1)->toPlainText() == "value equ #55\nmacro put v\n db v\nmend\n");
    // Its line ends are kept when it is saved.
    {
        QTextCursor end(assembler->editor(1)->document());
        end.movePosition(QTextCursor::End);
        end.insertText("; more");
    }
    CHECK(assembler->tabText(1) == "Defs*");
    CHECK(assembler->saveAll());
    CHECK(readHost(folder.filePath("lib/Defs.asm")) == "value equ #55\r\nmacro put v\r\n db v\r\nmend\r\n; more");
    CHECK(assembler->tabText(0) == "Prog" && assembler->tabText(1) == "Defs");
    CHECK(assembler->closeFile(1));
    CHECK(!assembler->openFile(folder.filePath("none.asm")));
    CHECK_EQ(assembler->fileCount(), 1);

    // Output to a file on the disc in the drive, with AMSDOS's header.
    type(editor, "org #8200\nwrite direct \"test.bin\",#8201\ndb 1,2,3\n");
    {
        Modals modals(closeBox);
        CHECK(!assembler->assemble());
    }
    CHECK(assembler->errorTexts().value(0).startsWith("There is no disc in drive A:"));
    CHECK(window.discs()->createBlank(0, folder.filePath("work.dsk"), tuxape::defaultDiscFormat()).isEmpty());
    CHECK(assembles());
    {
        std::optional<std::vector<uint8_t>> stored;
        emulator.withMachine([&](tuxape::Cpc& cpc) {
            tuxape::DiscFiles files(*cpc.fdc().drive(0).disc);
            for (const tuxape::DiscFile& file : files.list())
                if (file.name == "TEST.BIN")
                    stored = files.read(file);
        });
        CHECK(stored.has_value());
        const auto header = stored ? tuxape::amsdosHeader(*stored) : std::nullopt;
        CHECK(header && header->type == 2 && header->loadAddress == 0x8200 && header->entryAddress == 0x8201
              && header->length == 3);
        CHECK(stored && stored->size() >= 131 && (*stored)[128] == 1 && (*stored)[129] == 2 && (*stored)[130] == 3);
    }
    type(editor, "write direct \"b:x.bin\"\ndb 1\n");
    {
        Modals modals(closeBox);
        CHECK(!assembler->assemble());
    }
    CHECK(assembler->errorTexts().value(0).startsWith("There is no disc in drive B:"));

    // The symbols of the last assembly, filtered by the start of their names.
    type(editor, "org #8000\nstart nop\nstack equ #1234\nbadtime jp start\n");
    CHECK(assembles());
    SymbolsDialog* symbols = assembler->showSymbols();
    CHECK(symbols && symbols->isVisible());
    if (!symbols)
        return checkSummary("gui_assembler");
    CHECK(symbols->listed() == (QStringList{"badtime 8001", "stack 1234", "start 8000"}));
    auto* list = symbols->findChild<QTreeWidget*>("lvSymbols");
    CHECK(list && list->topLevelItemCount() == 3);
    if (!list || list->topLevelItemCount() != 3)
        return checkSummary("gui_assembler");
    CHECK(list->topLevelItem(0)->text(2) == QString(QChar(0x2718)));  // nothing uses badtime
    CHECK(list->topLevelItem(2)->text(2) == QString(QChar(0x2714)));
    symbols->setFilter("st");
    CHECK(symbols->listed() == (QStringList{"stack 1234", "start 8000"}));
    symbols->setFilter("*ba");
    CHECK(symbols->listed() == QStringList{"badtime 8001"});
    auto* sourceLabel = symbols->findChild<QLabel*>("lSource");
    auto* lineLabel = symbols->findChild<QLabel*>("lLine");
    CHECK(sourceLabel && sourceLabel->text() == "Source: " + QDir::toNativeSeparators(assembler->filePath(0)));
    CHECK(lineLabel && lineLabel->text() == "Line: 4");
    editor->gotoLine(1);
    emit list->itemDoubleClicked(list->currentItem(), 0);
    CHECK_EQ(editor->line(), 4);
    CHECK(symbols->saveSymbols(folder.filePath("prog.sym")));
    CHECK(readHost(folder.filePath("prog.sym")) == "badtime equ #8001\nstack equ #1234\nstart equ #8000\n");
    symbols->setFilter(QString());
    if (!prefix.isEmpty())
        symbols->grab().save(prefix + "symbols.png");
    symbols->close();

    // Find, find again, replace.
    type(editor, "ld a,b\nLD A,C\nld a,d\n");
    editor->gotoLine(1);
    FindOptions find;
    find.text = "ld a";
    CHECK(assembler->find(find));
    CHECK(editor->line() == 1 && editor->textCursor().selectedText() == "ld a");
    CHECK(assembler->findAgain());
    CHECK(editor->line() == 2 && editor->textCursor().selectedText() == "LD A");
    find.caseSensitive = true;
    CHECK(assembler->find(find));
    CHECK_EQ(editor->line(), 3);
    CHECK(!assembler->findAgain());
    find.backward = true;
    CHECK(assembler->find(find));
    CHECK_EQ(editor->line(), 1);
    find.text = "nowhere";
    find.entireScope = true;
    CHECK(!assembler->find(find));
    FindOptions replace;
    replace.text = "a";
    replace.replacement = "hx";
    replace.wholeWords = true;
    CHECK_EQ(assembler->replaceAll(replace), 3);
    CHECK(editor->toPlainText() == "ld hx,b\nLD hx,C\nld hx,d\n");
    editor->undo();  // in one go
    CHECK(editor->toPlainText() == "ld a,b\nLD A,C\nld a,d\n");
    {
        // The Replace window: Replace All from its button.
        Modals modals([&](QWidget* modal) {
            auto* text = modal->findChild<QLineEdit*>("edFind");
            auto* with = modal->findChild<QLineEdit*>("edReplace");
            auto* all = modal->findChild<QPushButton*>("bReplaceAll");
            auto* caseSensitive = modal->findChild<QCheckBox*>("ckCase");
            CHECK(text && with && all && caseSensitive);
            if (!(text && with && all && caseSensitive))
                return closeBox(modal);
            text->setText("ld");
            with->setText("cp");
            caseSensitive->setChecked(true);
            all->click();
        });
        actionNamed(*assembler, "Replace")->trigger();
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(editor->toPlainText() == "cp a,b\nLD A,C\ncp a,d\n");

    // The options, kept with the settings.
    {
        Modals modals([&](QWidget* modal) {
            auto* path = modal->findChild<QLineEdit*>("edLibraryPath");
            auto* pushPc = modal->findChild<QCheckBox*>("ckPushPC");
            auto* hide = modal->findChild<QCheckBox*>("ckHideOutput");
            CHECK(modal->windowTitle() == "Assembler Options" && path && pushPc && hide);
            if (path && pushPc && hide) {
                CHECK(pushPc->isChecked() && hide->isChecked());
                path->setText("/some/where;/else");
                pushPc->setChecked(false);
            }
            closeBox(modal);
        });
        actionNamed(*assembler, "Options")->trigger();
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(assembler->libraryPath() == "/some/where;/else" && !assembler->pushPcOnRun() && assembler->hideOutput());
    {
        Settings saved;
        saved.load();
        CHECK(saved.assemblerLibraryPath == "/some/where;/else" && !saved.assemblerPushPc && saved.assemblerHideOutput);
    }
    // Without the push, Run leaves the stack alone.
    type(editor, "org #8000\nrun start\n.start jr start ; round and round\n db \"text\",#ff\n");
    const uint16_t stack = sp();
    CHECK(runs());
    CHECK(QTest::qWaitFor([&] { return pc() == 0x8000 && !emulator.isPaused(); }, 5000));
    CHECK_EQ(sp(), stack);

    if (!prefix.isEmpty()) {
        type(editor, "nolist\nread \"firmware.asm\"\n\nrun start\norg #8000\nlimit #a5ff\n\n.start\n"
                             "call KL_L_ROM_ENABLE ; a comment\nld a,2\ndb \"Hello\",0\n");
        editor->toggleBreakpoint(10);
        {
            Modals modals(closeBox);
            assembler->assemble();
        }
        QTest::qWait(50);
        assembler->grab().save(prefix + "assembler.png");
    }

    editor->document()->setModified(false);
    emulator.stop();
    return checkSummary("gui_assembler");
}
