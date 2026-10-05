#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include <QAbstractScrollArea>
#include <QDialog>

class Emulator;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;

// The memory a pane of the debugger shows: 64K as they stood when the
// machine was last looked at.
using DebugMemory = std::array<uint8_t, 0x10000>;

// The debugger's upper pane: memory as Z80 instructions, one to a line,
// with the address, the instruction, its bytes and what they are as
// characters. The line of the program counter is in red; a dot in the
// margin marks a breakpoint, and a click there sets or clears one.
class DisassemblyView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit DisassemblyView(QWidget* parent = nullptr);

    void setMemory(const DebugMemory* memory) { memory_ = memory; }
    void setState(uint16_t pc, const std::set<uint16_t>& breakpoints);
    // The address of the first line, and of the line chosen.
    uint16_t top() const { return top_; }
    void setTop(uint16_t address);
    uint16_t selected() const { return selected_; }
    void select(uint16_t address);
    // Brings an address into view, a few lines from the top.
    void show(uint16_t address);

    struct Line {
        uint16_t address = 0;
        int length = 1;
        QString instruction;  // "LD (HL),C"
        QString bytes;        // "71"
        QString characters;
    };
    std::vector<Line> lines(int count) const;
    int visibleLines() const;

signals:
    void breakpointToggled(uint16_t address);
    void selectionChanged(uint16_t address);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    const DebugMemory* memory_ = nullptr;
    uint16_t top_ = 0;
    uint16_t selected_ = 0;
    uint16_t pc_ = 0;
    std::set<uint16_t> breakpoints_;

    int lineHeight() const;
    uint16_t before(uint16_t address) const;
    void scrollLines(int count);
};

// The lower pane: memory sixteen bytes to a line, in hexadecimal and as
// characters. Typing hexadecimal digits changes the byte under the cursor.
class MemoryDumpView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit MemoryDumpView(QWidget* parent = nullptr);

    void setMemory(const DebugMemory* memory) { memory_ = memory; }
    // How much of the memory there is to show: all 64K, or the first bytes
    // only (a sector of a disc, say).
    void setLimit(int bytes);
    uint16_t cursor() const { return cursor_; }
    void setCursor(uint16_t address);
    QString lineText(int row) const;
    int visibleLines() const;
    // A digit typed on the byte under the cursor, as the keyboard would.
    void typeDigit(int digit);

signals:
    void byteEdited(uint16_t address, uint8_t value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    const DebugMemory* memory_ = nullptr;
    uint16_t top_ = 0;     // a multiple of 16
    uint16_t cursor_ = 0;
    bool lowNibble_ = false;
    int limit_ = 0x10000;

    int lineHeight() const;
};

// WinAPE's debugger: the program being run as instructions, the Z80's
// registers (which can be changed), the stack, and memory. It is shown
// when the machine is paused; F7 steps one instruction, F8 steps over
// calls, F4 runs to the line chosen, F5 sets or clears a breakpoint there
// and F9 sets the machine running again.
class DebuggerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DebuggerDialog(Emulator* emulator, QWidget* parent = nullptr);

    // Looks at the machine again. To be called when it has stopped.
    void refresh();

    DisassemblyView* disassembly() const { return disassembly_; }
    MemoryDumpView* memoryDump() const { return dump_; }
    // A register by the name it has in the window ("AF", "HL'", "PC"...):
    // what is shown, and a new value typed in.
    QString registerText(const QString& name) const;
    bool setRegister(const QString& name, const QString& hex);
    QString flagsText() const;
    QStringList stackLines() const;
    bool hideOnRun() const;

    void stepInto();
    void stepOver();
    void runToSelection();
    void toggleBreakpoint(uint16_t address);
    void run();
    void goTo(uint16_t address);

signals:
    // The user set the machine running from here.
    void runRequested();
    // A step over or a run to the line chosen has set it running, until it
    // gets there.
    void runningOn();

private:
    Emulator* emulator_;
    DebugMemory memory_ = {};
    uint64_t timerBase_ = 0;
    bool writeView_ = false;

    DisassemblyView* disassembly_;
    MemoryDumpView* dump_;
    std::vector<std::pair<QString, QLineEdit*>> registers_;
    QLabel* flags_;
    QLineEdit* interruptMode_;
    QCheckBox* interrupts_;
    QLabel* timer_;
    QListWidget* stack_;
    QRadioButton* readView_;
    QRadioButton* writeViewButton_;
    QCheckBox* followPc_;
    QCheckBox* hideOnRun_;
    QCheckBox* breakpointsOn_;
    QCheckBox* breakInstructions_;

    void registerEdited(const QString& name);
    void askGoTo();
};
