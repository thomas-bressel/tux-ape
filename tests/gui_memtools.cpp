// The tools of the debugger's memory dump: a selection, find (text, bytes,
// instructions), fill, load, save, compare, breakpoints on the selection,
// disassembly as a source, and data areas. Runs without a display
// (QT_QPA_PLATFORM=offscreen); no ROM is needed.

#include <functional>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "assemblerdialog.h"
#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
#include "debuggerdialog.h"
#include "emulator.h"
#include "mainwindow.h"
#include "settings.h"

namespace {

class Modals {
public:
    explicit Modals(std::function<void(QWidget*)> handle)
    {
        QObject::connect(&timer_, &QTimer::timeout, [this, handle] {
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

void accept(QWidget* modal)
{
    if (auto* buttons = modal->findChild<QDialogButtonBox*>(); buttons && buttons->button(QDialogButtonBox::Ok))
        buttons->button(QDialogButtonBox::Ok)->click();
    else
        static_cast<QDialog*>(modal)->reject();
}

QByteArray readHost(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("?");
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));

    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();
    emulator.start();
    emulator.setPaused(true);
    // 9000 "Hello World"; 9100 LD HL,#B7C4 / LD (#B7C4),HL / RET
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.out(0x7F00, 0x8C);
        for (int address = 0x8000; address < 0xB000; ++address)
            cpc.memory().write(static_cast<uint16_t>(address), 0);
        const char text[] = "Hello World";
        for (int i = 0; text[i]; ++i)
            cpc.memory().write(static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(text[i]));
        const uint8_t code[] = {0x21, 0xC4, 0xB7, 0x22, 0xC4, 0xB7, 0xC9};
        for (size_t i = 0; i < sizeof code; ++i)
            cpc.memory().write(static_cast<uint16_t>(0x9100 + i), code[i]);
        cpc.cpu().pc = 0x9100;
    });
    QAction* debuggerAction = nullptr;
    QAction* dataAreasAction = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == "&Pause")
            debuggerAction = action;
        if (action->text() == "&Data Areas")
            dataAreasAction = action;
    }
    CHECK(dataAreasAction && dataAreasAction->isEnabled());
    CHECK(debuggerAction != nullptr);
    if (debuggerAction)
        debuggerAction->trigger();
    DebuggerDialog* debugger = window.debugger();
    CHECK(debugger != nullptr);
    if (!debugger)
        return checkSummary("gui_memtools");
    debugger->refresh();
    MemoryDumpView* dump = debugger->memoryDump();
    const auto peek = [&](uint16_t address) {
        return emulator.withMachine([&](tuxape::Cpc& cpc) { return cpc.memory().readRam(address); });
    };
    const auto bytes = [&](uint16_t address, int count) {
        QByteArray out;
        for (int i = 0; i < count; ++i)
            out += static_cast<char>(peek(static_cast<uint16_t>(address + i)));
        return out.toHex();
    };
    using Kind = DebuggerDialog::FindKind;
    // The start of the dump's line for an address, wherever the view has
    // scrolled to: "C000 77".
    const auto shown = [&](unsigned address) {
        dump->setCursor(static_cast<uint16_t>(address));
        const QString wanted = QString("%1").arg(address, 4, 16, QLatin1Char('0')).toUpper();
        for (int row = 0; row <= dump->visibleLines(); ++row)
            if (dump->lineText(row).startsWith(wanted))
                return dump->lineText(row).left(7);
        return QString("not shown");
    };

    // The selection: the cursor's byte, stretched with Shift, or a block
    // given by its ends.
    dump->setCursor(0x9000);
    CHECK(dump->selectionStart() == 0x9000 && dump->selectionLength() == 1);
    QTest::keyClick(dump, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(dump, Qt::Key_Right, Qt::ShiftModifier);
    CHECK(dump->selectionStart() == 0x9000 && dump->selectionLength() == 3);
    QTest::keyClick(dump, Qt::Key_Down);
    CHECK(dump->selectionStart() == 0x9012 && dump->selectionLength() == 1);
    debugger->selectBlock(0x9000, 0x900A);
    CHECK(dump->selectionStart() == 0x9000 && dump->selectionLength() == 11 && dump->cursor() == 0x9000);

    // Find: text, with or without its case, '?' for any character.
    dump->setCursor(0x8000);
    CHECK_EQ(debugger->find(Kind::Text, "hello w"), 0x9000);
    CHECK(dump->selectionStart() == 0x9000 && dump->selectionLength() == 7);
    CHECK_EQ(debugger->find(Kind::Text, "hello w", true), -1);
    CHECK_EQ(debugger->find(Kind::Text, "W?rld", true), 0x9006);
    CHECK_EQ(debugger->findAgain(), 0x9006);  // the only one: round memory and back
    CHECK_EQ(debugger->find(Kind::Text, ""), -1);
    // Bytes.
    dump->setCursor(0);
    CHECK_EQ(debugger->find(Kind::Hex, "22 ?? b7 C9"), 0x9103);
    CHECK(dump->selectionStart() == 0x9103 && dump->selectionLength() == 4);
    CHECK_EQ(debugger->find(Kind::Hex, "zz"), -1);
    CHECK_EQ(debugger->find(Kind::Hex, "100"), -1);
    // Instructions.
    debugger->disassembly()->select(0x9000);
    CHECK_EQ(debugger->find(Kind::Assembler, "ld (*),hl"), 0x9103);
    CHECK_EQ(debugger->disassembly()->selected(), 0x9103);
    CHECK_EQ(debugger->find(Kind::Assembler, "LD HL,#B7C?"), 0x9100);
    CHECK_EQ(debugger->find(Kind::Assembler, "LD IX,*"), -1);

    // Fill: the pattern again and again (the help's own example).
    debugger->selectBlock(0xA000, 0xA009);
    debugger->fillSelection(QByteArray::fromHex("010203"));
    CHECK(bytes(0xA000, 11) == "0102030102030102030100");
    debugger->selectBlock(0xA020, 0xA033);
    debugger->fillSelection("Hello World!");
    CHECK(bytes(0xA020, 21) == "48656c6c6f20576f726c642148656c6c6f20576f00");

    // Save the selection; load it back somewhere else.
    const QString path = folder.filePath("block.bin");
    debugger->selectBlock(0xA000, 0xA009);
    CHECK(debugger->saveSelection(path));
    CHECK(readHost(path).toHex() == "01020301020301020301");
    dump->setCursor(0xA100);
    CHECK(debugger->loadAt(path));
    CHECK(bytes(0xA100, 11) == "0102030102030102030100");
    CHECK(dump->selectionStart() == 0xA100 && dump->selectionLength() == 10);
    CHECK(!debugger->loadAt(folder.filePath("none.bin")));

    // Compare with memory elsewhere, and with a file.
    debugger->selectBlock(0xA000, 0xA009);
    CHECK(debugger->compareSelection(0xA100).isEmpty());
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.memory().write(0xA104, 0xEE);
        cpc.memory().write(0xA105, 0xEE);
        cpc.memory().write(0xA109, 0xEE);
    });
    debugger->refresh();
    CHECK(debugger->compareSelection(0xA100) == (QStringList{"A004|0002", "A009|0001"}));
    CHECK(debugger->compareSelectionWithFile(path).isEmpty());
    debugger->selectBlock(0xA000, 0xA00B);
    CHECK(debugger->compareSelectionWithFile(path) == QStringList{"A00A|0002"});

    // Breakpoints on the selection.
    debugger->selectBlock(0xA000, 0xA003);
    debugger->breakOnSelection(true);
    debugger->selectBlock(0xA002, 0xA002);
    debugger->breakOnSelection(false);
    const std::vector<Emulator::MemoryBreak> breaks = emulator.memoryBreaks();
    CHECK_EQ(breaks.size(), 2);
    CHECK(breaks.size() == 2 && breaks[0].address == 0xA000 && breaks[0].size == 4 && breaks[0].write);
    CHECK(breaks.size() == 2 && breaks[1].address == 0xA002 && breaks[1].size == 1 && !breaks[1].write);
    emulator.setMemoryBreaks({});

    // The selection as a source.
    debugger->selectBlock(0x9100, 0x9106);
    CHECK(debugger->disassembleSelection() == "org #9100\nLD HL,#B7C4\nLD (#B7C4),HL\nRET\n");
    debugger->selectBlock(0x9100, 0x9101);
    CHECK(debugger->disassembleSelection() == "org #9100\nDB #21\nDB #C4\n");

    // Data areas: bytes or words where instructions would be shown.
    debugger->selectBlock(0x9000, 0x900A);
    debugger->markData(false);
    CHECK_EQ(debugger->dataAreas().size(), 1);
    CHECK(debugger->disassembly()->instructionAt(0x9000).text == "DB #48,#65,#6C,#6C");
    CHECK_EQ(debugger->disassembly()->instructionAt(0x9000).length, 4);
    CHECK(debugger->disassembly()->instructionAt(0x9008).text == "DB #72,#6C,#64");
    CHECK(debugger->disassembly()->instructionAt(0x900B).text == "NOP");
    debugger->markData(true);
    CHECK_EQ(debugger->dataAreas().size(), 1);
    CHECK(debugger->disassembly()->instructionAt(0x9000).text == "DW #6548");
    CHECK(debugger->disassembly()->instructionAt(0x900A).text == "DB #64");
    debugger->selectBlock(0x9008, 0x900C);
    CHECK(debugger->disassembleSelection() == "org #9008\nDW #6C72\nDB #64\nNOP\nNOP\n");
    debugger->clearDataArea();
    CHECK(debugger->dataAreas().empty());
    CHECK(debugger->disassembly()->instructionAt(0x9000).text == "LD C,B");

    // The Any view: memory as the boxes say, whatever the machine has in.
    // Here two ROMs fitted for the purpose, and a bank of the second 64K.
    {
        static std::vector<uint8_t> lower(0x4000, 0x11), upper0(0x4000, 0x22), upper7(0x4000, 0x77);
        emulator.withMachine([&](tuxape::Cpc& cpc) {
            cpc.memory().setLowerRom(lower);
            cpc.memory().setUpperRom(0, upper0);
            cpc.memory().setUpperRom(7, upper7);
            cpc.out(0x7F00, 0x8C);                // both ROMs out, as the machine sees it
            cpc.memory().write(0x0000, 0xA0);     // the RAM under them
            cpc.memory().write(0xC000, 0xAC);
            cpc.out(0x7F00, 0xC4);                // a bank of the second 64K at &4000...
            cpc.memory().write(0x4000, 0xB4);
            cpc.out(0x7F00, 0xC0);                // ...and the first back
            cpc.memory().write(0x4000, 0xA4);
        });
        debugger->refresh();
        CHECK(!debugger->anyView());
        CHECK(shown(0x0000) == "0000 A0");
        debugger->setAnyView(true, true, 7, 0xC4);
        CHECK(debugger->anyView());
        CHECK(shown(0x0000) == "0000 11");
        CHECK(shown(0xC000) == "C000 77");
        CHECK(shown(0x4000) == "4000 B4");
        debugger->setAnyView(false, true, 0, 0xC0);
        CHECK(shown(0xC000) == "C000 22");
        CHECK(shown(0x0000) == "0000 A0");
        // A byte typed in goes to the RAM of the view, in the bank shown.
        debugger->setAnyView(false, false, 0, 0xC4);
        dump->setCursor(0x4000);
        dump->typeDigit(0x5);
        dump->typeDigit(0xE);
        CHECK(shown(0x4000) == "4000 5E");
        // The machine's own mapping has not moved.
        const bool same = emulator.withMachine([](tuxape::Cpc& cpc) {
            return !cpc.memory().lowerRomEnabled() && !cpc.memory().upperRomEnabled() && cpc.memory().ramBank() == 0xC0
                && cpc.memory().read(0x4000) == 0xA4;
        });
        CHECK(same);
        auto* readView = debugger->findChild<QRadioButton*>("rbRead");
        CHECK(readView != nullptr);
        if (readView)
            readView->setChecked(true);
        CHECK(!debugger->anyView());
        CHECK(shown(0x4000) == "4000 A4");
    }

    // The Find window, and the disassembly sent to a new tab of the assembler.
    dump->setCursor(0);
    {
        Modals modals([&](QWidget* modal) {
            auto* tabs = modal->findChild<QTabWidget*>("PageControl");
            auto* hexData = modal->findChild<QLineEdit*>("edHex");
            CHECK(modal->windowTitle() == "Find" && tabs && hexData && tabs->count() == 3);
            if (tabs && hexData) {
                tabs->setCurrentIndex(1);
                hexData->setText("B7 C9");
            }
            accept(modal);
        });
        debugger->showFind();
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(dump->selectionStart() == 0x9105 && dump->selectionLength() == 2);
    debugger->selectBlock(0x9100, 0x9106);
    CHECK(window.assembler() == nullptr);
    emit debugger->sourceProduced(debugger->disassembleSelection());
    CHECK(window.assembler() && window.assembler()->isVisible());
    if (window.assembler()) {
        CHECK(window.assembler()->editor()->toPlainText() == "org #9100\nLD HL,#B7C4\nLD (#B7C4),HL\nRET\n");
        // And the assembler makes of it what it came from.
        emulator.withMachine([](tuxape::Cpc& cpc) {
            for (int address = 0x9100; address < 0x9107; ++address)
                cpc.memory().write(static_cast<uint16_t>(address), 0);
        });
        window.assembler()->setOptions(QString(), true, true);
        CHECK(window.assembler()->assemble());
        CHECK(bytes(0x9100, 7) == "21c4b722c4b7c9");
        window.assembler()->editor()->document()->setModified(false);
    }

    emulator.stop();
    return checkSummary("gui_memtools");
}
