// The debugger: the window that comes up when the machine is paused, single
// steps, steps over calls, breakpoints, a run to a chosen line, registers
// and memory changed by hand. Runs without a display
// (QT_QPA_PLATFORM=offscreen), on a program of its own: no ROM is needed.
//
//   gui_debugger [prefix]   also saves a picture of the window as
//                           <prefix>debugger.png

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
#include "debuggerdialog.h"
#include "emulator.h"
#include "mainwindow.h"
#include "settings.h"

namespace {

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text))
            return action;
    return nullptr;
}

QToolButton* toolButton(MainWindow& window, const char* tip)
{
    for (QToolButton* candidate : window.findChildren<QToolButton*>())
        if (candidate->toolTip() == tip)
            return candidate;
    return nullptr;
}

// 8000 LD A,3 / 8002 CALL 8010 / 8005 DEC A / 8006 JR NZ,8002 /
// 8008 LD BC,3 / 800B LDIR / 800D JR 800D / 8010 INC B / 8011 RET
const uint8_t kProgram[] = {0x3E, 0x03, 0xCD, 0x10, 0x80, 0x3D, 0x20, 0xFA, 0x01, 0x03,
                            0x00, 0xED, 0xB0, 0x18, 0xFE, 0x00, 0x04, 0xC9};

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

    QAction* run = actionNamed(window, "Run");
    QAction* pause = actionNamed(window, "Pause");
    QToolButton* stepButton = toolButton(window, "Single Step (F7)");
    QToolButton* overButton = toolButton(window, "Step Over (F8)");
    CHECK(run && pause && stepButton && overButton);
    if (!(run && pause && stepButton && overButton))
        return checkSummary("gui_debugger");
    CHECK(pause->isEnabled() && !run->isEnabled());
    CHECK(!stepButton->isEnabled() && !overButton->isEnabled());
    CHECK(window.debugger() == nullptr);

    // Paused, the machine is the debugger's.
    pause->trigger();
    CHECK(emulator.isPaused());
    DebuggerDialog* debugger = window.debugger();
    CHECK(debugger && debugger->isVisible());
    if (!debugger)
        return checkSummary("gui_debugger");
    CHECK(run->isEnabled() && !pause->isEnabled());
    CHECK(stepButton->isEnabled() && overButton->isEnabled());

    // The program, in RAM with the ROMs out of the way and no interrupts.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.out(0x7F00, 0x8C);
        for (size_t i = 0; i < sizeof kProgram; ++i)
            cpc.memory().write(static_cast<uint16_t>(0x8000 + i), kProgram[i]);
        auto& cpu = cpc.cpu();
        cpu.pc = 0x8000;
        cpu.sp = 0xBFF0;
        cpu.iff1 = cpu.iff2 = false;
        cpu.halted = false;
        cpu.reg[cpu.B] = 0x10;
        cpu.reg[cpu.H] = 0x90;
        cpu.reg[cpu.L] = 0x00;
        cpu.reg[cpu.D] = 0x91;
        cpu.reg[cpu.E] = 0x00;
        cpu.reg[cpu.F] = 0x41;  // Z and C
        cpc.memory().write(0x9000, 0x11);
        cpc.memory().write(0x9001, 0x22);
        cpc.memory().write(0x9002, 0x33);
        cpc.memory().write(0xBFF0, 0xCD);
        cpc.memory().write(0xBFF1, 0xAB);
    });
    debugger->refresh();
    auto pc = [&] { return emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.cpu().pc; }); };
    auto stopsAt = [&](uint16_t address) {
        const bool stopped = QTest::qWaitFor([&] { return emulator.isPaused(); }, 5000);
        QApplication::processEvents();  // the window hears of it
        return stopped && pc() == address;
    };

    // What the window shows of it.
    DisassemblyView* code = debugger->disassembly();
    // The program counter's line is in view, a little below the top.
    bool inView = false;
    for (const DisassemblyView::Line& line : code->lines(code->visibleLines()))
        inView = inView || line.address == 0x8000;
    CHECK(inView);
    CHECK(code->top() < 0x8000 && code->top() >= 0x7FF8);
    code->setTop(0x8000);
    const auto lines = code->lines(9);
    CHECK_EQ(lines.size(), 9);
    if (lines.size() == 9) {
        CHECK(lines[0].instruction == "LD A,#03" && lines[0].bytes == "3E 03");
        CHECK(lines[1].instruction == "CALL #8010" && lines[1].address == 0x8002 && lines[1].length == 3);
        CHECK(lines[2].instruction == "DEC A");
        CHECK(lines[3].instruction == "JR NZ,#8002");
        CHECK(lines[4].instruction == "LD BC,#0003");
        CHECK(lines[5].instruction == "LDIR");
        CHECK(lines[6].instruction == "JR #800D");
        CHECK(lines[8].instruction == "INC B" && lines[8].address == 0x8010);
    }
    CHECK(debugger->registerText("PC") == "8000");
    CHECK(debugger->registerText("SP") == "BFF0");
    CHECK(debugger->registerText("HL") == "9000");
    CHECK(debugger->registerText("BC").startsWith("10"));
    CHECK(debugger->flagsText() == ".Z.....C");
    CHECK(debugger->stackLines().value(0) == "BFF0: ABCD");
    CHECK_EQ(code->selected(), 0x8000);
    if (!prefix.isEmpty()) {
        QTest::qWait(50);
        debugger->grab().save(prefix + "debugger.png");
    }

    // A single step, from the debugger and from the main window's button.
    debugger->stepInto();
    QApplication::processEvents();
    CHECK_EQ(pc(), 0x8002);
    CHECK(debugger->registerText("AF").startsWith("03"));
    CHECK(debugger->registerText("PC") == "8002");
    CHECK_EQ(code->selected(), 0x8002);
    // Into the call...
    stepButton->click();
    QApplication::processEvents();
    CHECK_EQ(pc(), 0x8010);
    CHECK(debugger->stackLines().value(0) == "BFEE: 8005");
    debugger->stepInto();
    debugger->stepInto();
    QApplication::processEvents();
    CHECK_EQ(pc(), 0x8005);
    CHECK(debugger->registerText("BC").startsWith("11"));
    debugger->stepInto();
    debugger->stepInto();
    QApplication::processEvents();
    CHECK_EQ(pc(), 0x8002);
    // ... and over it: the machine runs to the instruction after.
    overButton->click();
    CHECK(stopsAt(0x8005));
    CHECK(debugger->registerText("BC").startsWith("12"));
    CHECK(debugger->isVisible());
    // An instruction that is not a call is stepped over as it is stepped.
    debugger->stepOver();
    QApplication::processEvents();
    CHECK_EQ(pc(), 0x8006);

    // A breakpoint, set by a click in the margin's place: the machine set
    // running stops there, and the window, which hides while it runs,
    // comes back.
    debugger->toggleBreakpoint(0x8008);
    CHECK(emulator.breakpoints() == std::set<uint16_t>{0x8008});
    run->trigger();
    CHECK(stopsAt(0x8008));
    // The window hears of the stop from the machine's thread, a moment later.
    CHECK(QTest::qWaitFor([&] { return debugger->isVisible() && run->isEnabled(); }, 2000));
    CHECK(debugger->registerText("AF").startsWith("00"));
    CHECK(debugger->registerText("BC").startsWith("13"));
    CHECK(run->isEnabled() && !pause->isEnabled());
    // Leaving a breakpoint does not stop on it again.
    debugger->stepInto();
    QApplication::processEvents();
    CHECK_EQ(pc(), 0x800B);
    // A step is one turn of an instruction that repeats; a step over, all
    // of them.
    debugger->stepInto();
    QApplication::processEvents();
    CHECK_EQ(pc(), 0x800B);
    CHECK(debugger->registerText("BC") == "0002");
    debugger->stepOver();
    CHECK(stopsAt(0x800D));
    CHECK(debugger->registerText("BC") == "0000");
    CHECK_EQ(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().read(0x9102); }), 0x33);

    // Run to the line chosen: here back at the top, from a program counter
    // put there by hand.
    CHECK(debugger->setRegister("PC", "8000"));
    CHECK_EQ(pc(), 0x8000);
    code->select(0x8006);
    debugger->runToSelection();
    CHECK(stopsAt(0x8006));
    CHECK(debugger->registerText("AF").startsWith("02"));

    // Registers changed by hand; what is not a number changes nothing.
    CHECK(debugger->setRegister("HL", "1234"));
    CHECK(debugger->setRegister("AF'", "beef"));
    CHECK(debugger->setRegister("I", "7f"));
    CHECK(!debugger->setRegister("DE", "xyz"));
    CHECK(!debugger->setRegister("IM", "3"));
    CHECK(debugger->setRegister("IM", "2"));
    const bool taken = emulator.withMachine([](tuxape::Cpc& cpc) {
        const auto& cpu = cpc.cpu();
        return cpu.reg[cpu.H] == 0x12 && cpu.reg[cpu.L] == 0x34 && cpu.af2 == 0xBEEF && cpu.i == 0x7F && cpu.im == 2
               && cpu.reg[cpu.D] == 0x91;
    });
    CHECK(taken);
    CHECK(debugger->registerText("HL") == "1234");
    CHECK(debugger->registerText("AF'") == "BEEF");
    CHECK(debugger->registerText("IM") == "2");

    // Memory changed by hand, a digit at a time.
    MemoryDumpView* dump = debugger->memoryDump();
    dump->setCursor(0x9001);
    CHECK(dump->lineText(0x9000 / 16 - dump->verticalScrollBar()->value()).startsWith("9000 11 22 33"));
    dump->typeDigit(0xA);
    dump->typeDigit(0x5);
    CHECK_EQ(dump->cursor(), 0x9002);
    CHECK_EQ(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.memory().read(0x9001); }), 0xA5);
    CHECK(dump->lineText(0x9000 / 16 - dump->verticalScrollBar()->value()).startsWith("9000 11 A5 33"));

    // Breakpoints switched off all together are run through.
    debugger->toggleBreakpoint(0x8008);
    debugger->toggleBreakpoint(0x8005);
    auto* breakpointsOn = debugger->findChild<QCheckBox*>("ckBreakpoints");
    CHECK(breakpointsOn && breakpointsOn->isChecked());
    breakpointsOn->setChecked(false);
    CHECK(debugger->setRegister("PC", "8000"));
    code->select(0x8008);
    debugger->runToSelection();
    CHECK(stopsAt(0x8008));
    breakpointsOn->setChecked(true);
    CHECK(debugger->setRegister("PC", "8000"));
    run->trigger();
    CHECK(stopsAt(0x8005));

    // The break instruction, ED FF: the machine stops after it, when the
    // box is ticked.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.memory().write(0x8020, 0xED);
        cpc.memory().write(0x8021, 0xFF);
        cpc.memory().write(0x8022, 0x18);
        cpc.memory().write(0x8023, 0xFE);
    });
    debugger->toggleBreakpoint(0x8005);
    auto* breakInstructions = debugger->findChild<QCheckBox*>("ckBreakInstr");
    CHECK(breakInstructions && !breakInstructions->isChecked());
    breakInstructions->setChecked(true);
    CHECK(debugger->setRegister("PC", "8020"));
    code->setTop(0x8020);
    CHECK(code->lines(1).front().instruction == "BRK");
    run->trigger();
    CHECK(stopsAt(0x8022));

    // The view of memory as it is written to: the RAM under a ROM.
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.memory().setLowerRom(std::vector<uint8_t>(0x4000, 0xC9));
        cpc.out(0x7F00, 0x88);  // the lower ROM back in
        cpc.memory().write(0x0100, 0x3C);
    });
    debugger->refresh();
    code->setTop(0x0100);
    CHECK(code->lines(1).front().instruction == "RET");
    debugger->findChild<QRadioButton*>("rbWrite")->setChecked(true);
    code->setTop(0x0100);
    CHECK(code->lines(1).front().instruction == "INC A");

    // Run from the debugger's own key hides it, if it is set to; pausing
    // brings it back.
    QTest::keyClick(debugger, Qt::Key_F9);
    CHECK(!emulator.isPaused());
    CHECK(!debugger->isVisible());
    CHECK(pause->isEnabled() && !stepButton->isEnabled());
    pause->trigger();
    CHECK(emulator.isPaused() && debugger->isVisible());
    debugger->findChild<QCheckBox*>("ckHideOnRun")->setChecked(false);
    run->trigger();
    CHECK(debugger->isVisible());

    emulator.stop();
    return checkSummary("gui_debugger");
}
