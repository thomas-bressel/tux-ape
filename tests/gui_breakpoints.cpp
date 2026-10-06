// The Breakpoints window: breakpoints on code, on memory and on input and
// output, with their conditions and pass counts, and the machine pausing
// where they say. Runs without a display (QT_QPA_PLATFORM=offscreen); no
// ROM is needed.
//
//   gui_breakpoints [prefix]   also saves a picture of the window as
//                              <prefix>breakpoints.png

#include <functional>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "breakpointsdialog.h"
#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"
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
    if (auto* buttons = modal->findChild<QDialogButtonBox*>())
        buttons->button(QDialogButtonBox::Ok)->click();
    else
        static_cast<QDialog*>(modal)->reject();
}

// 8000 LD A,0 / 8002 INC A / 8003 LD (9000),A / 8006 LD B,EF / 8008 LD C,A
// 8009 OUT (C),C (the printer's port) / 800B JR 8002
const uint8_t kProgram[] = {0x3E, 0x00, 0x3C, 0x32, 0x00, 0x90, 0x06, 0xEF, 0x4F, 0xED, 0x49, 0x18, 0xF5};

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
    emulator.setPaused(true);
    emulator.withMachine([](tuxape::Cpc& cpc) {
        cpc.out(0x7F00, 0x8C);
        for (size_t i = 0; i < sizeof kProgram; ++i)
            cpc.memory().write(static_cast<uint16_t>(0x8000 + i), kProgram[i]);
        cpc.cpu().pc = 0x8000;
        cpc.cpu().sp = 0xBFF0;
        cpc.cpu().iff1 = cpc.cpu().iff2 = false;
        cpc.cpu().halted = false;
    });
    struct Where {
        unsigned pc, a, stored;
    };
    // Lets the machine go, waits for a breakpoint to pause it, and says
    // where it stands.
    const auto runs = [&] {
        emulator.setPaused(false);
        CHECK(QTest::qWaitFor([&] { return emulator.isPaused(); }, 5000));
        emulator.setPaused(true);
        const Where where = emulator.withMachine([](tuxape::Cpc& cpc) {
            return Where{cpc.cpu().pc, cpc.cpu().reg[cpc.cpu().A], cpc.memory().readRam(0x9000)};
        });
        if (qEnvironmentVariableIsSet("TUXAPE_TEST_TRACE"))
            std::printf("  paused at %04X, A=%02X, (9000)=%02X\n", where.pc, where.a, where.stored);
        return where;
    };

    QAction* show = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text() == "&Breakpoints")
            show = action;
    CHECK(show && show->isEnabled());
    CHECK(window.breakpoints() == nullptr);
    if (!show)
        return checkSummary("gui_breakpoints");
    show->trigger();
    BreakpointsDialog* dialog = window.breakpoints();
    CHECK(dialog && dialog->isVisible());
    if (!dialog)
        return checkSummary("gui_breakpoints");
    using Page = BreakpointsDialog::Page;
    CHECK(dialog->rows(Page::Code).isEmpty() && dialog->rows(Page::Memory).isEmpty()
          && dialog->rows(Page::InputOutput).isEmpty());
    auto* add = dialog->findChild<QPushButton*>("bAdd");
    auto* clearAll = dialog->findChild<QPushButton*>("bClearAll");
    CHECK(add && !add->isEnabled() && clearAll && !clearAll->isEnabled());
    const QStringList devices = BreakpointsDialog::devices();
    CHECK(devices.contains("FDC Motor") && devices.contains("  Register Select") && devices.last() == "User Defined Port");

    // A code breakpoint with a condition pauses only when it holds.
    emulator.setBreakpoints({0x8003});
    dialog->refresh();
    CHECK(dialog->rows(Page::Code) == QStringList{"8003|User|0|"});
    CHECK(!dialog->setProperties(Page::Code, 0, "A = ", 0));
    CHECK(!dialog->setProperties(Page::Code, 0, "nowhere = 1", 0));
    CHECK(!dialog->setProperties(Page::Code, 1, "A = 1", 0));
    CHECK(dialog->setProperties(Page::Code, 0, "A = 5", 0));
    CHECK(dialog->rows(Page::Code) == QStringList{"8003|User|0|A = 5"});
    Where at = runs();
    CHECK(at.pc == 0x8003 && at.a == 5 && at.stored == 4);
    dialog->refresh();
    CHECK(dialog->rows(Page::Code) == QStringList{"8003|User|1|A = 5"});
    // With a pass count, only every so many times.
    CHECK(dialog->setProperties(Page::Code, 0, "", 3));
    CHECK(dialog->rows(Page::Code) == QStringList{"8003|User|0/3|"});
    at = runs();
    CHECK(at.pc == 0x8003 && at.a == 8);
    // A symbol of the last assembly may be named.
    emulator.setSymbols({{"LOOP", 0x8002}});
    CHECK(dialog->setProperties(Page::Code, 0, "pc = loop + 1 and a = #10", 0));
    at = runs();
    CHECK(at.pc == 0x8003 && at.a == 0x10);
    // Its properties, from the window a double click opens.
    {
        Modals modals([&](QWidget* modal) {
            auto* condition = modal->findChild<QLineEdit*>("edCondition");
            auto* passCount = modal->findChild<QLineEdit*>("edPassCount");
            CHECK(modal->windowTitle() == "Breakpoint Properties" && condition && passCount);
            if (condition && passCount) {
                CHECK(condition->text() == "pc = loop + 1 and a = #10" && passCount->text().isEmpty());
                condition->setText("a > #17");
                passCount->setText("2");
            }
            accept(modal);
        });
        dialog->setPage(Page::Code);
        dialog->select(0);
        dialog->showProperties();
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(dialog->rows(Page::Code) == QStringList{"8003|User|0/2|a > #17"});
    at = runs();
    CHECK(at.pc == 0x8003 && at.a == 0x19);
    dialog->clear(Page::Code, 0);
    CHECK(dialog->rows(Page::Code).isEmpty() && emulator.breakpoints().empty());

    // A memory breakpoint pauses after the instruction that wrote there.
    CHECK(!dialog->addMemoryBreak(0x10000, 1, true));
    CHECK(!dialog->addMemoryBreak(0xFFFF, 2, true));
    CHECK(!dialog->addMemoryBreak(0x9000, 1, true, "value ="));
    CHECK(dialog->addMemoryBreak(0x9000, 1, true, "value = #20"));
    CHECK(dialog->rows(Page::Memory) == QStringList{"9000|0001|Write|0|value = #20"});
    at = runs();
    CHECK(at.pc == 0x8006 && at.a == 0x20 && at.stored == 0x20);
    CHECK(dialog->setProperties(Page::Memory, 0, "previous = #30 and address = #9000", 0));
    at = runs();
    CHECK(at.pc == 0x8006 && at.a == 0x31);
    // One on reads is not set off by writes.
    dialog->clearAll(Page::Memory);
    CHECK(dialog->addMemoryBreak(0x8FF0, 0x20, false));
    CHECK(dialog->rows(Page::Memory) == QStringList{"8FF0|0020|Read|0|"});
    emulator.setBreakpoints({0x8003});
    at = runs();
    CHECK(at.pc == 0x8003 && at.a == 0x32);  // the code breakpoint, not the memory one
    emulator.setBreakpoints({});
    dialog->clearAll(Page::Memory);
    CHECK(dialog->rows(Page::Memory).isEmpty() && emulator.memoryBreaks().empty());

    // Input and output: a device by its name, or a port of one's own.
    CHECK(dialog->addIoBreak(static_cast<int>(devices.indexOf("Printer Port")), "value = #40"));
    CHECK(dialog->rows(Page::InputOutput) == QStringList{"Printer Port|Write|0|value = #40"});
    at = runs();
    CHECK(at.pc == 0x800B && at.a == 0x40);
    dialog->clearAll(Page::InputOutput);
    const int user = static_cast<int>(devices.size()) - 1;
    CHECK(!dialog->addIoBreak(user, "", 0, 0xEF00, 0xFF00, false, false));
    CHECK(dialog->addIoBreak(user, "", 0, 0xEF00, 0xFF00, false, true));
    CHECK(dialog->rows(Page::InputOutput) == QStringList{"User - Port AND FF00 = EF00|Write|0|"});
    at = runs();
    CHECK(at.pc == 0x800B && at.a == 0x41);
    // Input only: the program's outputs go by; so do writes to the Gate
    // Array that are not for the palette's colours.
    dialog->clearAll(Page::InputOutput);
    CHECK(dialog->addIoBreak(user, "", 0, 0xEF00, 0xFF00, true, false));
    CHECK(dialog->addIoBreak(static_cast<int>(devices.indexOf("  Palette Write"))));
    CHECK(dialog->rows(Page::InputOutput)
          == (QStringList{"User - Port AND FF00 = EF00|Read|0|", "Palette Write|Write|0|"}));
    emulator.setBreakpoints({0x8002});
    at = runs();
    CHECK(at.pc == 0x8002 && at.a == 0x41);
    emulator.setBreakpoints({});
    dialog->clearAll(Page::InputOutput);

    // The Add window: a device shows its port and mask; a port of one's
    // own is typed with X for the digits that do not matter.
    dialog->setPage(Page::InputOutput);
    CHECK(add && add->isEnabled());
    {
        Modals modals([&](QWidget* modal) {
            auto* type = modal->findChild<QComboBox*>("cbType");
            auto* port = modal->findChild<QLineEdit*>("edPort");
            auto* mask = modal->findChild<QLineEdit*>("edMask");
            auto* shown = modal->findChild<QLabel*>("lPort");
            auto* input = modal->findChild<QCheckBox*>("ckInput");
            auto* condition = modal->findChild<QLineEdit*>("edCondition");
            CHECK(modal->windowTitle() == "Add Input/Output Breakpoint" && type && port && mask && shown && input && condition);
            if (!(type && port && mask && shown && input && condition))
                return static_cast<QDialog*>(modal)->reject();
            type->setCurrentIndex(static_cast<int>(devices.indexOf("FDC Motor")));
            CHECK(port->text() == "0000" && mask->text() == "0580" && shown->text() == "Port AND 0580 = 0000");
            CHECK(!port->isEnabled() && !input->isEnabled() && input->isChecked());
            type->setCurrentIndex(type->count() - 1);
            CHECK(port->isEnabled() && input->isEnabled());
            port->clear();
            QTest::keyClicks(port, "BCXX");
            CHECK(mask->text() == "FF00" && shown->text() == "Port AND FF00 = BC00");
            condition->setText("value = 4");
            accept(modal);
        });
        if (add)
            add->click();
        CHECK_EQ(modals.seen(), 1);
    }
    CHECK(dialog->rows(Page::InputOutput) == QStringList{"User - Port AND FF00 = BC00|Read/Write|0|value = 4"});
    CHECK(clearAll && clearAll->isEnabled());
    if (!prefix.isEmpty()) {
        dialog->addIoBreak(static_cast<int>(devices.indexOf("FDC Motor")), "", 100);
        dialog->grab().save(prefix + "breakpoints.png");
    }
    if (clearAll)
        clearAll->click();
    CHECK(dialog->rows(Page::InputOutput).isEmpty() && emulator.ioBreaks().empty());

    // Timers: a breakpoint's condition starts one, another's stops it and
    // counts the microseconds between the two. Neither pauses the machine
    // unless its condition says so.
    QAction* showTimers = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text() == "&Timers")
            showTimers = action;
    CHECK(showTimers && showTimers->isEnabled());
    if (showTimers)
        showTimers->trigger();
    TimersDialog* timers = window.timers();
    CHECK(timers && timers->isVisible() && timers->rows().isEmpty());
    if (!timers)
        return checkSummary("gui_breakpoints");
    // From 8003, LD (9000),A, to 800B, JR 8002: 4 + 2 + 1 + 4 microseconds.
    emulator.setBreakpoints({0x8003, 0x800B});
    dialog->refresh();
    CHECK(!dialog->setProperties(Page::Code, 0, "timer_start(1, 2)", 0));
    CHECK(dialog->setProperties(Page::Code, 0, "timer_start(#B941)", 0));
    CHECK(dialog->setProperties(Page::Code, 1, "timer_stop(#B941) and a = #60", 0));
    CHECK(timers->rows().isEmpty());  // only looked at so far: nothing started
    at = runs();
    CHECK(at.pc == 0x800B && at.a == 0x60);
    timers->refresh();
    const unsigned turns = 0x60 - 0x41;
    CHECK(timers->rows() == QStringList{QString("B941|%1|11|11|11|11.00").arg(turns)});
    // The value itself: what timer_stop gives is the time.
    CHECK(dialog->setProperties(Page::Code, 1, "timer_stop(#B941) = 11 and a = #62", 0));
    at = runs();
    CHECK(at.pc == 0x800B && at.a == 0x62);
    // reset_cycles sets the debugger's count of microseconds.
    CHECK(dialog->setProperties(Page::Code, 0, "reset_cycles(5)", 0));
    CHECK(dialog->setProperties(Page::Code, 1, "a = #63", 0));
    at = runs();
    CHECK(at.pc == 0x800B && at.a == 0x63);
    const uint64_t now = emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.instructionTime(); });
    CHECK_EQ(now - emulator.cycleBase(), 5 + 4 + 2 + 1 + 4);
    auto* clearTimers = timers->findChild<QPushButton*>("bClearAll");
    CHECK(clearTimers != nullptr);
    if (clearTimers)
        clearTimers->click();
    CHECK(timers->rows().isEmpty() && emulator.timers().empty());
    emulator.setBreakpoints({});

    emulator.stop();
    return checkSummary("gui_breakpoints");
}
