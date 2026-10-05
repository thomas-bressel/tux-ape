#include "debuggerdialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QShortcut>
#include <QVBoxLayout>

#include "emulator.h"

#include "core/disasm.h"

namespace {

constexpr int kMargin = 16;  // the disassembly's margin, where breakpoints show

QFont fixedFont()
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

QString hex(unsigned value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QChar shown(uint8_t byte)
{
    return byte >= 32 && byte < 127 ? QChar(byte) : QChar('.');
}

}  // namespace

// ---- The disassembly ---------------------------------------------------------

DisassemblyView::DisassemblyView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setFont(fixedFont());
    setFocusPolicy(Qt::StrongFocus);
    verticalScrollBar()->setRange(0, 0xFFFF);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        top_ = static_cast<uint16_t>(value);
        viewport()->update();
    });
}

int DisassemblyView::lineHeight() const
{
    return fontMetrics().height();
}

int DisassemblyView::visibleLines() const
{
    return std::max(1, viewport()->height() / lineHeight());
}

void DisassemblyView::setState(uint16_t pc, const std::set<uint16_t>& breakpoints)
{
    pc_ = pc;
    breakpoints_ = breakpoints;
    viewport()->update();
}

void DisassemblyView::setTop(uint16_t address)
{
    top_ = address;
    const QSignalBlocker blocker(verticalScrollBar());
    verticalScrollBar()->setValue(address);
    viewport()->update();
}

void DisassemblyView::select(uint16_t address)
{
    selected_ = address;
    viewport()->update();
    emit selectionChanged(address);
}

std::vector<DisassemblyView::Line> DisassemblyView::lines(int count) const
{
    std::vector<Line> out;
    if (!memory_)
        return out;
    uint16_t address = top_;
    for (int i = 0; i < count; ++i) {
        const tuxape::Instruction instruction = tuxape::disassemble(address, [this](uint16_t a) { return (*memory_)[a]; });
        Line line;
        line.address = address;
        line.length = instruction.length;
        line.instruction = QString::fromLatin1(instruction.text.c_str());
        for (int b = 0; b < instruction.length; ++b) {
            const uint8_t byte = (*memory_)[static_cast<uint16_t>(address + b)];
            line.bytes += (b ? " " : "") + hex(byte, 2);
            line.characters += shown(byte);
        }
        out.push_back(line);
        address = static_cast<uint16_t>(address + instruction.length);
    }
    return out;
}

// The instruction ahead of an address: the longest run of bytes just before
// it that reads as one instruction ending there.
uint16_t DisassemblyView::before(uint16_t address) const
{
    if (memory_)
        for (int length = 4; length >= 1; --length) {
            const uint16_t candidate = static_cast<uint16_t>(address - length);
            if (tuxape::disassemble(candidate, [this](uint16_t a) { return (*memory_)[a]; }).length == length)
                return candidate;
        }
    return static_cast<uint16_t>(address - 1);
}

void DisassemblyView::scrollLines(int count)
{
    uint16_t address = top_;
    for (; count < 0; ++count)
        address = before(address);
    if (count > 0) {
        const std::vector<Line> ahead = lines(count + 1);
        if (!ahead.empty())
            address = ahead.back().address;
    }
    setTop(address);
}

void DisassemblyView::show(uint16_t address)
{
    // Already among the lines shown: nothing moves.
    for (const Line& line : lines(visibleLines() - 1))
        if (line.address == address)
            return;
    uint16_t top = address;
    for (int i = 0; i < 2; ++i)
        top = before(top);
    setTop(top);
}

void DisassemblyView::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().base());
    painter.fillRect(0, 0, kMargin, viewport()->height(), palette().button());
    const int height = lineHeight();
    const int ascent = fontMetrics().ascent();
    const int character = fontMetrics().horizontalAdvance('0');
    int y = 0;
    for (const Line& line : lines(visibleLines() + 1)) {
        const QRect row(kMargin, y, viewport()->width() - kMargin, height);
        QColor text = palette().color(QPalette::Text);
        if (line.address == pc_) {
            painter.fillRect(row, QColor(0xFF, 0x00, 0x00));
            text = Qt::white;
        } else if (line.address == selected_) {
            painter.fillRect(row, palette().highlight());
            text = palette().color(QPalette::HighlightedText);
        }
        if (breakpoints_.count(line.address)) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0xD0, 0x10, 0x10));
            painter.drawEllipse(QRectF(3.5, y + (height - 9) / 2.0, 9, 9));
        }
        const qsizetype space = line.instruction.indexOf(' ');
        const QString mnemonic = space < 0 ? line.instruction : line.instruction.left(space);
        const QString operands = space < 0 ? QString() : line.instruction.mid(space + 1);
        painter.setPen(text);
        const int left = kMargin + 4;
        painter.drawText(left, y + ascent, hex(line.address, 4));
        painter.drawText(left + character * 5, y + ascent, mnemonic);
        painter.drawText(left + character * 10, y + ascent, operands);
        painter.drawText(left + character * 28, y + ascent, line.bytes);
        if (line.address != pc_ && line.address != selected_)
            painter.setPen(QColor(0x00, 0x80, 0x00));
        painter.drawText(left + character * 41, y + ascent, line.characters);
        y += height;
    }
}

void DisassemblyView::mousePressEvent(QMouseEvent* event)
{
    const std::vector<Line> shownLines = lines(visibleLines() + 1);
    const size_t row = static_cast<size_t>(event->position().y()) / static_cast<size_t>(lineHeight());
    if (row >= shownLines.size())
        return;
    if (event->position().x() < kMargin)
        emit breakpointToggled(shownLines[row].address);
    else
        select(shownLines[row].address);
}

void DisassemblyView::keyPressEvent(QKeyEvent* event)
{
    const std::vector<Line> shownLines = lines(visibleLines());
    switch (event->key()) {
    case Qt::Key_Up:
        if (!shownLines.empty() && selected_ == shownLines.front().address)
            scrollLines(-1);
        select(before(selected_));
        break;
    case Qt::Key_Down: {
        const tuxape::Instruction here = memory_ ? tuxape::disassemble(selected_, [this](uint16_t a) { return (*memory_)[a]; })
                                                 : tuxape::Instruction();
        if (!shownLines.empty() && selected_ == shownLines.back().address)
            scrollLines(1);
        select(static_cast<uint16_t>(selected_ + here.length));
        break;
    }
    case Qt::Key_PageUp: scrollLines(-(visibleLines() - 1)); break;
    case Qt::Key_PageDown: scrollLines(visibleLines() - 1); break;
    default: QAbstractScrollArea::keyPressEvent(event);
    }
}

void DisassemblyView::wheelEvent(QWheelEvent* event)
{
    scrollLines(event->angleDelta().y() > 0 ? -3 : 3);
}

void DisassemblyView::resizeEvent(QResizeEvent*)
{
    verticalScrollBar()->setPageStep(visibleLines());
}

// ---- The memory dump ---------------------------------------------------------

MemoryDumpView::MemoryDumpView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setFont(fixedFont());
    setFocusPolicy(Qt::StrongFocus);
    verticalScrollBar()->setRange(0, 0xFFF);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        top_ = static_cast<uint16_t>(value * 16);
        viewport()->update();
    });
}

int MemoryDumpView::lineHeight() const
{
    return fontMetrics().height();
}

int MemoryDumpView::visibleLines() const
{
    return std::max(1, viewport()->height() / lineHeight());
}

void MemoryDumpView::setLimit(int bytes)
{
    bytes = std::clamp(bytes, 16, 0x10000);
    if (bytes == limit_)
        return;
    limit_ = bytes;
    verticalScrollBar()->setRange(0, std::max(0, (limit_ + 15) / 16 - 1));
    // The cursor stays where it is if that is still within.
    if (cursor_ >= limit_) {
        verticalScrollBar()->setValue(0);
        top_ = 0;
        cursor_ = 0;
        lowNibble_ = false;
    }
    viewport()->update();
}

void MemoryDumpView::setCursor(uint16_t address)
{
    // Round the end of a short piece, back to its start.
    if (address >= limit_)
        address = static_cast<uint16_t>(limit_ == 0x10000 ? address : address % limit_);
    cursor_ = address;
    lowNibble_ = false;
    // Kept in view.
    const int rows = visibleLines();
    const int row = (address - top_) / 16;
    if (address < top_ || row >= rows)
        verticalScrollBar()->setValue(std::max(0, address / 16 - rows / 2));
    viewport()->update();
}

QString MemoryDumpView::lineText(int row) const
{
    if (!memory_ || top_ + row * 16 >= limit_)
        return {};
    const uint16_t address = static_cast<uint16_t>(top_ + row * 16);
    QString text = hex(address, 4), characters;
    for (int i = 0; i < 16; ++i) {
        if (address + i >= limit_) {
            text += "   ";
            continue;
        }
        const uint8_t byte = (*memory_)[static_cast<uint16_t>(address + i)];
        text += ' ' + hex(byte, 2);
        characters += shown(byte);
    }
    return text + ' ' + characters;
}

void MemoryDumpView::typeDigit(int digit)
{
    if (!memory_)
        return;
    const uint8_t now = (*memory_)[cursor_];
    const uint8_t value = lowNibble_ ? static_cast<uint8_t>((now & 0xF0) | digit) : static_cast<uint8_t>((now & 0x0F) | digit << 4);
    emit byteEdited(cursor_, value);
    if (lowNibble_)
        setCursor(static_cast<uint16_t>(cursor_ + 1));
    else
        lowNibble_ = true;
    viewport()->update();
}

void MemoryDumpView::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().base());
    const int height = lineHeight();
    const int ascent = fontMetrics().ascent();
    const int character = fontMetrics().horizontalAdvance('0');
    for (int row = 0; row <= visibleLines(); ++row) {
        const uint16_t address = static_cast<uint16_t>(top_ + row * 16);
        const int y = row * height;
        // The byte under the cursor, in both columns.
        if (static_cast<uint16_t>(cursor_ - address) < 16) {
            const int index = cursor_ - address;
            painter.fillRect(4 + character * (5 + index * 3), y, character * 2, height, palette().highlight());
            painter.fillRect(4 + character * (5 + 48 + index), y, character, height, palette().highlight().color().lighter(150));
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(4, y + ascent, lineText(row));
    }
}

void MemoryDumpView::mousePressEvent(QMouseEvent* event)
{
    const int character = fontMetrics().horizontalAdvance('0');
    const int row = static_cast<int>(event->position().y()) / lineHeight();
    const int column = (static_cast<int>(event->position().x()) - 4) / character;
    int index = -1;
    if (column >= 5 && column < 5 + 48)
        index = (column - 5) / 3;
    else if (column >= 53 && column < 69)
        index = column - 53;
    if (index >= 0 && top_ + row * 16 + index < limit_)
        setCursor(static_cast<uint16_t>(top_ + row * 16 + index));
}

void MemoryDumpView::keyPressEvent(QKeyEvent* event)
{
    const QString text = event->text().toUpper();
    const int digit = text.size() == 1 ? QStringLiteral("0123456789ABCDEF").indexOf(text[0]) : -1;
    if (digit >= 0 && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        typeDigit(digit);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left: setCursor(static_cast<uint16_t>(cursor_ - 1)); break;
    case Qt::Key_Right: setCursor(static_cast<uint16_t>(cursor_ + 1)); break;
    case Qt::Key_Up: setCursor(static_cast<uint16_t>(cursor_ - 16)); break;
    case Qt::Key_Down: setCursor(static_cast<uint16_t>(cursor_ + 16)); break;
    case Qt::Key_PageUp: setCursor(static_cast<uint16_t>(cursor_ - 16 * (visibleLines() - 1))); break;
    case Qt::Key_PageDown: setCursor(static_cast<uint16_t>(cursor_ + 16 * (visibleLines() - 1))); break;
    default: QAbstractScrollArea::keyPressEvent(event);
    }
}

void MemoryDumpView::wheelEvent(QWheelEvent* event)
{
    verticalScrollBar()->setValue(verticalScrollBar()->value() + (event->angleDelta().y() > 0 ? -3 : 3));
}

void MemoryDumpView::resizeEvent(QResizeEvent*)
{
    verticalScrollBar()->setPageStep(visibleLines());
}

// ---- The window ---------------------------------------------------------------

DebuggerDialog::DebuggerDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("TuxAPE Debugger"));

    disassembly_ = new DisassemblyView;
    disassembly_->setObjectName("Disassembler");
    disassembly_->setMemory(&memory_);
    dump_ = new MemoryDumpView;
    dump_->setObjectName("MemoryDump");
    dump_->setMemory(&memory_);

    // The registers, in WinAPE's two columns.
    auto* registerGrid = new QGridLayout;
    registerGrid->setSpacing(2);
    flags_ = new QLabel;
    flags_->setObjectName("lFlags");
    flags_->setFont(fixedFont());
    registerGrid->addWidget(new QLabel(tr("Flags")), 0, 0);
    registerGrid->addWidget(flags_, 0, 1, 1, 3);
    const char* const names[] = {"AF", "AF'", "HL", "HL'", "DE", "DE'", "BC", "BC'", "IX", "SP", "IY", "I", "PC", "R"};
    for (int i = 0; i < 14; ++i) {
        auto* edit = new QLineEdit;
        edit->setObjectName(QString("ed") + names[i]);
        edit->setFont(fixedFont());
        const bool byte = names[i][0] == 'I' && names[i][1] == 0 ? true : names[i][0] == 'R';
        edit->setMaxLength(byte ? 2 : 4);
        edit->setFixedWidth(fontMetrics().horizontalAdvance("00000") + 12);
        const QString name = names[i];
        connect(edit, &QLineEdit::editingFinished, this, [this, name] { registerEdited(name); });
        registerGrid->addWidget(new QLabel(name), 1 + i / 2, (i % 2) * 2);
        registerGrid->addWidget(edit, 1 + i / 2, (i % 2) * 2 + 1);
        registers_.emplace_back(name, edit);
    }
    interruptMode_ = new QLineEdit;
    interruptMode_->setObjectName("edIM");
    interruptMode_->setMaxLength(1);
    interruptMode_->setFixedWidth(24);
    interrupts_ = new QCheckBox(tr("Ints"));
    interrupts_->setObjectName("ckInts");
    registerGrid->addWidget(new QLabel(tr("IM")), 8, 0);
    registerGrid->addWidget(interruptMode_, 8, 1);
    registerGrid->addWidget(interrupts_, 8, 2, 1, 2);
    connect(interruptMode_, &QLineEdit::editingFinished, this, [this] { registerEdited("IM"); });
    connect(interrupts_, &QCheckBox::clicked, this, [this] { registerEdited("Ints"); });

    timer_ = new QLabel;
    timer_->setObjectName("lTimer");
    auto* resetTimer = new QPushButton(tr("X"));
    resetTimer->setObjectName("bResetT");
    resetTimer->setFixedWidth(24);
    resetTimer->setToolTip(tr("Reset the count of microseconds"));
    resetTimer->setAutoDefault(false);
    auto* timerRow = new QHBoxLayout;
    timerRow->addWidget(new QLabel(tr("T")));
    timerRow->addWidget(timer_, 1);
    timerRow->addWidget(resetTimer);
    stack_ = new QListWidget;
    stack_->setObjectName("lbStack");
    stack_->setFont(fixedFont());
    stack_->setFixedWidth(fontMetrics().horizontalAdvance("0000: 0000") + 48);

    auto* right = new QVBoxLayout;
    right->addLayout(registerGrid);
    right->addLayout(timerRow);
    right->addWidget(new QLabel(tr("Stack")));
    right->addWidget(stack_, 1);

    // Which memory the panes show.
    auto* memoryBox = new QGroupBox(tr("Memory"));
    readView_ = new QRadioButton(tr("Read"));
    readView_->setObjectName("rbRead");
    readView_->setChecked(true);
    writeViewButton_ = new QRadioButton(tr("Write"));
    writeViewButton_->setObjectName("rbWrite");
    auto* anyView = new QRadioButton(tr("Any"));
    anyView->setObjectName("rbAny");
    anyView->setEnabled(false);
    anyView->setToolTip(tr("Not available yet"));
    auto* memoryLayout = new QHBoxLayout(memoryBox);
    memoryLayout->addWidget(readView_);
    memoryLayout->addWidget(writeViewButton_);
    memoryLayout->addWidget(anyView);
    connect(writeViewButton_, &QRadioButton::toggled, this, [this](bool write) {
        writeView_ = write;
        refresh();
    });

    followPc_ = new QCheckBox(tr("Follow PC"));
    followPc_->setObjectName("ckFollowPC");
    followPc_->setChecked(true);
    hideOnRun_ = new QCheckBox(tr("Hide On Run"));
    hideOnRun_->setObjectName("ckHideOnRun");
    hideOnRun_->setChecked(true);
    breakpointsOn_ = new QCheckBox(tr("Breakpoints"));
    breakpointsOn_->setObjectName("ckBreakpoints");
    breakpointsOn_->setChecked(emulator_->breakpointsEnabled());
    breakInstructions_ = new QCheckBox(tr("Break Instructions"));
    breakInstructions_->setObjectName("ckBreakInstr");
    breakInstructions_->setChecked(emulator_->breakInstructions());
    connect(breakpointsOn_, &QCheckBox::toggled, this, [this](bool on) { emulator_->setBreakpointsEnabled(on); });
    connect(breakInstructions_, &QCheckBox::toggled, this, [this](bool on) { emulator_->setBreakInstructions(on); });
    auto* goTo = new QPushButton(tr("Goto"));
    goTo->setObjectName("bGoto");
    goTo->setAutoDefault(false);
    goTo->setToolTip(tr("Goto (CTRL+G)"));
    connect(goTo, &QPushButton::clicked, this, [this] { askGoTo(); });

    auto* options = new QHBoxLayout;
    options->addWidget(memoryBox);
    for (QCheckBox* box : {followPc_, hideOnRun_, breakpointsOn_, breakInstructions_})
        options->addWidget(box);
    options->addStretch(1);
    options->addWidget(goTo);

    auto* panes = new QVBoxLayout;
    panes->addWidget(disassembly_, 2);
    panes->addWidget(dump_, 3);
    auto* top = new QHBoxLayout;
    top->addLayout(panes, 1);
    top->addLayout(right);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addLayout(options);
    resize(760, 520);

    connect(disassembly_, &DisassemblyView::breakpointToggled, this, &DebuggerDialog::toggleBreakpoint);
    connect(dump_, &MemoryDumpView::byteEdited, this, [this](uint16_t address, uint8_t value) {
        emulator_->withMachine([&](tuxape::Cpc& cpc) { cpc.memory().write(address, value); });
        refresh();
    });
    connect(resetTimer, &QPushButton::clicked, this, [this] {
        timerBase_ = emulator_->withMachine([](tuxape::Cpc& cpc) { return cpc.microseconds(); });
        refresh();
    });

    auto key = [this](const QKeySequence& keys, void (DebuggerDialog::*slot)()) {
        connect(new QShortcut(keys, this), &QShortcut::activated, this, slot);
    };
    key(Qt::Key_F7, &DebuggerDialog::stepInto);
    key(Qt::Key_F8, &DebuggerDialog::stepOver);
    key(Qt::Key_F4, &DebuggerDialog::runToSelection);
    key(Qt::Key_F9, &DebuggerDialog::run);
    key(Qt::CTRL | Qt::Key_G, &DebuggerDialog::askGoTo);
    connect(new QShortcut(Qt::Key_F5, this), &QShortcut::activated, this,
            [this] { toggleBreakpoint(disassembly_->selected()); });

    refresh();
}

void DebuggerDialog::refresh()
{
    struct Cpu {
        uint16_t af, bc, de, hl, af2, bc2, de2, hl2, ix, iy, sp, pc;
        uint8_t i, r, im;
        bool interrupts;
        uint64_t time;
    };
    const bool write = writeView_;
    const Cpu cpu = emulator_->withMachine([&](tuxape::Cpc& cpc) {
        const tuxape::Memory& memory = cpc.memory();
        for (int address = 0; address < 0x10000; ++address)
            memory_[static_cast<size_t>(address)] = write ? memory.readRam(static_cast<uint16_t>(address))
                                                          : memory.read(static_cast<uint16_t>(address));
        const auto& z80 = cpc.cpu();
        auto pair = [&](int high, int low) { return static_cast<uint16_t>(z80.reg[high] << 8 | z80.reg[low]); };
        return Cpu{pair(z80.A, z80.F), pair(z80.B, z80.C), pair(z80.D, z80.E), pair(z80.H, z80.L), z80.af2, z80.bc2,
                   z80.de2,            z80.hl2,            z80.ix,             z80.iy,             z80.sp,  z80.pc,
                   z80.i,              z80.r,              z80.im,             z80.iff1,           cpc.microseconds()};
    });
    const uint16_t values[] = {cpu.af, cpu.af2, cpu.hl, cpu.hl2, cpu.de, cpu.de2, cpu.bc,
                               cpu.bc2, cpu.ix, cpu.sp, cpu.iy, cpu.i, cpu.pc, cpu.r};
    for (size_t i = 0; i < registers_.size(); ++i)
        registers_[i].second->setText(hex(values[i], registers_[i].second->maxLength()));
    // A letter for each flag that is set.
    QString flags = QStringLiteral("SZ-H-VNC");
    for (int bit = 0; bit < 8; ++bit)
        if (!(cpu.af & 0x80 >> bit))
            flags[bit] = QLatin1Char('.');
    flags_->setText(flags);
    interruptMode_->setText(QString::number(cpu.im));
    interrupts_->setChecked(cpu.interrupts);
    timer_->setText(QString::number(cpu.time - timerBase_));

    stack_->clear();
    for (int i = 0; i < 32; ++i) {
        const uint16_t address = static_cast<uint16_t>(cpu.sp + i * 2);
        const unsigned value = memory_[address] | memory_[static_cast<uint16_t>(address + 1)] << 8;
        stack_->addItem(hex(address, 4) + ": " + hex(value, 4));
    }

    disassembly_->setState(cpu.pc, emulator_->breakpoints());
    if (followPc_->isChecked()) {
        disassembly_->show(cpu.pc);
        disassembly_->select(cpu.pc);
    }
    disassembly_->viewport()->update();
    dump_->viewport()->update();
}

QString DebuggerDialog::registerText(const QString& name) const
{
    if (name == "IM")
        return interruptMode_->text();
    for (const auto& [known, edit] : registers_)
        if (known == name)
            return edit->text();
    return {};
}

bool DebuggerDialog::setRegister(const QString& name, const QString& hexText)
{
    bool ok = false;
    const unsigned value = hexText.toUInt(&ok, 16);
    if (!ok || !emulator_->isPaused())
        return false;
    const bool known = emulator_->withMachine([&](tuxape::Cpc& cpc) {
        auto& z80 = cpc.cpu();
        auto pair = [&](int high, int low) {
            z80.reg[high] = static_cast<uint8_t>(value >> 8);
            z80.reg[low] = static_cast<uint8_t>(value);
        };
        const uint16_t word = static_cast<uint16_t>(value);
        if (name == "AF") pair(z80.A, z80.F);
        else if (name == "BC") pair(z80.B, z80.C);
        else if (name == "DE") pair(z80.D, z80.E);
        else if (name == "HL") pair(z80.H, z80.L);
        else if (name == "AF'") z80.af2 = word;
        else if (name == "BC'") z80.bc2 = word;
        else if (name == "DE'") z80.de2 = word;
        else if (name == "HL'") z80.hl2 = word;
        else if (name == "IX") z80.ix = word;
        else if (name == "IY") z80.iy = word;
        else if (name == "SP") z80.sp = word;
        else if (name == "PC") z80.pc = word;
        else if (name == "I") z80.i = static_cast<uint8_t>(value);
        else if (name == "R") z80.r = static_cast<uint8_t>(value);
        else if (name == "IM" && value <= 2) z80.im = static_cast<uint8_t>(value);
        else return false;
        return true;
    });
    refresh();
    return known;
}

void DebuggerDialog::registerEdited(const QString& name)
{
    if (name == "Ints") {
        if (emulator_->isPaused())
            emulator_->withMachine([this](tuxape::Cpc& cpc) { cpc.cpu().iff1 = cpc.cpu().iff2 = interrupts_->isChecked(); });
        refresh();
        return;
    }
    QString text = name == "IM" ? interruptMode_->text() : QString();
    for (const auto& [known, edit] : registers_)
        if (known == name)
            text = edit->text();
    // What cannot be taken goes back to what the register holds.
    if (!setRegister(name, text))
        refresh();
}

QString DebuggerDialog::flagsText() const
{
    return flags_->text();
}

QStringList DebuggerDialog::stackLines() const
{
    QStringList lines;
    for (int i = 0; i < stack_->count(); ++i)
        lines << stack_->item(i)->text();
    return lines;
}

bool DebuggerDialog::hideOnRun() const
{
    return hideOnRun_->isChecked();
}

void DebuggerDialog::stepInto()
{
    emulator_->stepInto();
}

void DebuggerDialog::stepOver()
{
    emulator_->stepOver();
    if (!emulator_->isPaused())
        emit runningOn();
}

void DebuggerDialog::runToSelection()
{
    if (!emulator_->isPaused())
        return;
    emulator_->runTo(disassembly_->selected());
    emit runningOn();
}

void DebuggerDialog::toggleBreakpoint(uint16_t address)
{
    std::set<uint16_t> breakpoints = emulator_->breakpoints();
    if (!breakpoints.erase(address))
        breakpoints.insert(address);
    emulator_->setBreakpoints(breakpoints);
    refresh();
}

void DebuggerDialog::run()
{
    emit runRequested();
}

void DebuggerDialog::goTo(uint16_t address)
{
    if (dump_->hasFocus()) {
        dump_->setCursor(address);
        return;
    }
    disassembly_->setTop(address);
    disassembly_->select(address);
}

void DebuggerDialog::askGoTo()
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Goto"), tr("Address:"), QLineEdit::Normal, QString(), &ok);
    const unsigned address = text.trimmed().remove('#').remove('&').toUInt(&ok, 16);
    if (ok && address <= 0xFFFF)
        goTo(static_cast<uint16_t>(address));
}
