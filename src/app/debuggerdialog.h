#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QDialog>
#include <QStringList>

#include "core/disasm.h"

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

    // Regions shown as data, not as instructions: bytes (DB) or words (DW).
    struct DataArea {
        uint16_t start = 0;
        int size = 0;
        bool words = false;
    };
    void setDataAreas(const std::vector<DataArea>& areas);
    // The instruction at an address, or the line of data of the area it
    // is in.
    tuxape::Instruction instructionAt(uint16_t address) const;

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
    std::vector<DataArea> dataAreas_;

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

    // The bytes chosen, by dragging the mouse or moving the cursor with
    // Shift held: the cursor's alone when none are.
    int selectionStart() const;
    int selectionLength() const;
    void select(int start, int length);
    // Memory breakpoints show in colour: green where reads are watched,
    // red for writes, yellow for both.
    struct Mark {
        int start = 0;
        int length = 0;
        bool write = false;
    };
    void setMarks(const std::vector<Mark>& marks);

signals:
    void byteEdited(uint16_t address, uint8_t value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    const DebugMemory* memory_ = nullptr;
    uint16_t top_ = 0;     // a multiple of 16
    uint16_t cursor_ = 0;
    int anchor_ = -1;  // the other end of the selection, or -1
    bool lowNibble_ = false;
    int limit_ = 0x10000;
    std::vector<Mark> marks_;

    int lineHeight() const;
    int byteAt(const QPointF& position) const;
    void moveTo(int address, bool extend);
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

    // ---- The tools of the memory dump's menu, on its selection ----
    enum class FindKind { Text, Hex, Assembler };
    // Looks on from the place in view, round the top of memory, for a
    // text, for bytes in hexadecimal ("3E ?? C9") or for an instruction
    // ("LD (*),HL"); '?' and, in instructions, '*' stand for anything.
    // The address found, shown and selected, or -1.
    int find(FindKind kind, const QString& what, bool caseSensitive = false);
    int findAgain();
    void selectBlock(unsigned start, unsigned end);
    // A file's bytes put in memory from the start of the selection, and
    // the selection's written to a file.
    bool loadAt(const QString& path);
    bool saveSelection(const QString& path);
    void fillSelection(const QByteArray& pattern);
    // Where the selection differs from the memory at an address, or from
    // a file: "address|size" for each run of bytes that differ.
    QStringList compareSelection(unsigned address) const;
    QStringList compareSelectionWithFile(const QString& path) const;
    void breakOnSelection(bool write);
    // The selection as assembler source.
    QString disassembleSelection() const;
    void markData(bool words);
    void clearDataArea();
    std::vector<DisassemblyView::DataArea> dataAreas() const { return dataAreas_; }
    void setDataAreas(const std::vector<DisassemblyView::DataArea>& areas);
    // The windows of the menu.
    void showFind();
    void showDataAreas();

signals:
    // Memory breakpoints were set from here.
    void breakpointsChanged();
    // A disassembly meant for a new tab of the assembler.
    void sourceProduced(const QString& source);
    // The user set the machine running from here.
    void runRequested();
    // A step over or a run to the line chosen has set it running, until it
    // gets there.
    void runningOn();

private:
    Emulator* emulator_;
    DebugMemory memory_ = {};
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

    std::vector<DisassemblyView::DataArea> dataAreas_;
    FindKind lastFindKind_ = FindKind::Text;
    QString lastFind_;
    bool lastFindCase_ = false;

    void registerEdited(const QString& name);
    void askGoTo();
    void dumpMenu(const QPoint& at);
    void showSelectBlock();
    void showFill();
    void showCompare();
    void showDisassemble();
    QByteArray selectedBytes() const;
    void writeBytes(int start, const QByteArray& bytes);
};
