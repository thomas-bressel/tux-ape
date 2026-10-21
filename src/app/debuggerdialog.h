#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QDialog>
#include <QStringList>

#include "core/disasm.h"
#include "core/memory.h"

class Emulator;
class QCheckBox;
class QTimer;
class QComboBox;
class QGroupBox;
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
// characters. Typing changes the byte under the cursor, in the column that
// was clicked: hexadecimal digits in the first, characters in the second.
// Ctrl+Tab goes from one to the other. A line under the cursor shows where
// what is typed will go.
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
    // The column that takes what is typed: the characters, or the digits.
    bool editsCharacters() const { return characters_; }
    void setEditsCharacters(bool characters);
    // A character typed there: the byte takes its code.
    void typeCharacter(char character);

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
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    const DebugMemory* memory_ = nullptr;
    uint16_t top_ = 0;     // a multiple of 16
    uint16_t cursor_ = 0;
    int anchor_ = -1;  // the other end of the selection, or -1
    bool lowNibble_ = false;
    bool characters_ = false;  // typing goes to the characters' column
    int limit_ = 0x10000;
    std::vector<Mark> marks_;

    int lineHeight() const;
    qreal characterWidth() const;
    int byteAt(const QPointF& position, bool* characters = nullptr) const;
    void moveTo(int address, bool extend);
};

// WinAPE's debugger: the program being run as instructions, the Z80's
// registers (which can be changed), the stack, and memory. It is shown
// with F7, or when a breakpoint stops the machine; paused, F7 steps one
// instruction, F8 steps over calls, F4 runs to the line chosen, F5 sets or
// clears a breakpoint there and F9 sets the machine running again.
class DebuggerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DebuggerDialog(Emulator* emulator, QWidget* parent = nullptr);

    // Looks at the machine again. To be called when it has stopped.
    void refresh();

    DisassemblyView* disassembly() const { return disassembly_; }
    MemoryDumpView* memoryDump() const { return dump_; }
    // A register by the name it has in the window ("AF", "HL'", "PC"...):
    // what is shown, and a new value typed in. "Flags" is F a bit at a
    // time, as the window has it under the flags' letters.
    QString registerText(const QString& name) const;
    bool setRegister(const QString& name, const QString& hex);
    QString flagsText() const;
    QStringList stackLines() const;
    bool hideOnRun() const;
    void setHideOnRun(bool on);
    // TuxAPE's own "Real Time": while the box is ticked and the machine
    // runs, the window follows it, twenty times a second: registers, stack,
    // memory. The disassembly keeps its place until the machine stops.
    bool realTime() const;
    void setRealTime(bool on);

    void stepInto();
    void stepOver();
    void runToSelection();
    void toggleBreakpoint(uint16_t address);
    void run();
    // Takes a pane to an address: the lower one if it has the keys, the
    // upper one otherwise. Or the lower one whatever has them, which then
    // gets them: Ctrl+Shift+G, where Ctrl+G is for the pane one is in.
    void goTo(uint16_t address);
    void goToMemory(uint16_t address);

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
    // A stretch of memory shown as bytes from now on, in place of the
    // areas it runs into: what the Graphics Finder's "Mark as data" asks.
    void addDataArea(unsigned start, int size);
    void clearDataArea();
    std::vector<DisassemblyView::DataArea> dataAreas() const { return dataAreas_; }
    void setDataAreas(const std::vector<DisassemblyView::DataArea>& areas);
    // The "Any" view of memory: the ROMs switched in or out and the RAM
    // bank chosen here, whatever the machine itself has. A bank is given
    // as a program would select it, #C0 to #FF; an upper ROM by its
    // number, #80 and up being the banks of a Plus's cartridge.
    void setAnyView(bool lowerRom, bool upperRom, int upperRomNumber, int ramBank);
    bool anyView() const;
    // Calls `use` with the machine's memory mapped as the Any view has it,
    // then puts the machine's own mapping back: how another window looks
    // at what this one shows. False, and nothing done, while the view is
    // not Any. To be called with the machine held (Emulator::withMachine).
    bool throughAnyView(tuxape::Memory& memory, const std::function<void()>& use) const;
    // With it, the Plus's secondary mapping: where the lower ROM shows (0:
    // #0000, 1: #4000, 2: #8000, 3: #0000 with the ASIC's registers at
    // #4000) and which of the cartridge's first eight banks it is.
    void setSecondaryMapping(int lowerRomBase, int cartridgeBank);
    // The Read and Write views again.
    void setMachineView(bool write);
    // What the list of upper ROMs offers: "07 - AMSDOS", "0A - (Empty)",
    // "80 - Cartridge Bank 0"...
    QStringList upperRomChoices() const;
    // The buttons' file boxes.
    void askLoad();
    void askSave();
    // The windows of the menu.
    void showFind();
    void showDataAreas();

signals:
    // Memory breakpoints were set from here.
    void breakpointsChanged();
    // The memory shown is no longer the same: another view of it was
    // chosen, or bytes were written from here.
    void memoryViewChanged();
    // A disassembly meant for a new tab of the assembler.
    void sourceProduced(const QString& source);
    // The user set the machine running from here.
    void runRequested();
    // A step over or a run to the line chosen has set it running, until it
    // gets there.
    void runningOn();
    // The buttons for the windows that are not the debugger's own.
    void breakpointsRequested();
    void timersRequested();
    void graphicsRequested();
    // "Hide On Run" or "Real Time" ticked or cleared.
    void boxesChanged();

private:
    Emulator* emulator_;
    DebugMemory memory_ = {};
    bool writeView_ = false;

    DisassemblyView* disassembly_;
    MemoryDumpView* dump_;
    std::vector<std::pair<QString, QLineEdit*>> registers_;
    QLineEdit* flags_;
    QLineEdit* interruptMode_;
    QCheckBox* interrupts_;
    QLabel* timer_;
    QListWidget* stack_;
    QRadioButton* readView_;
    QRadioButton* writeViewButton_;
    QRadioButton* anyView_;
    QGroupBox* romBox_;
    QGroupBox* ramBox_;
    QGroupBox* secondaryBox_;
    QCheckBox* anyLower_;
    QCheckBox* anyUpper_;
    QComboBox* upperRom_;
    QComboBox* ramBank_;
    QComboBox* romBase_;
    QComboBox* cartridgeBank_;
    QCheckBox* followPc_;
    QCheckBox* hideOnRun_;
    QCheckBox* realTime_;
    QTimer* realTimer_;
    bool live_ = false;  // refreshing as the machine runs
    QCheckBox* breakpointsOn_;
    QCheckBox* breakInstructions_;

    std::vector<DisassemblyView::DataArea> dataAreas_;
    FindKind lastFindKind_ = FindKind::Text;
    QString lastFind_;
    bool lastFindCase_ = false;

    void registerEdited(const QString& name);
    void askGoTo();
    void askGoToMemory();
    void askGoToIn(bool memory);
    void dumpMenu(const QPoint& at);
    void showSelectBlock();
    void showFill();
    void showCompare();
    void showDisassemble();
    using Mapping = tuxape::Memory::Mapping;
    Mapping applyView(tuxape::Memory& memory) const;
    void restoreView(tuxape::Memory& memory, const Mapping& saved) const;
    void viewChosen();
    // What is fitted, for the list of upper ROMs.
    struct Fitted {
        int count = 16;  // ROM slots: 16, or 32 with the larger board
        std::array<bool, tuxape::Memory::kRomSlots> present = {};
        bool cartridge = false, discRom = false;
    };
    void showMapping(const Mapping& machine, const Fitted& fitted);
    QByteArray selectedBytes() const;
    void writeBytes(int start, const QByteArray& bytes);
};
