#include "debuggerdialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QTabWidget>
#include <QTreeWidget>
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

void DisassemblyView::setDataAreas(const std::vector<DataArea>& areas)
{
    dataAreas_ = areas;
    viewport()->update();
}

tuxape::Instruction DisassemblyView::instructionAt(uint16_t address) const
{
    if (!memory_)
        return {};
    const auto byte = [&](int n) { return (*memory_)[static_cast<uint16_t>(address + n)]; };
    for (const DataArea& area : dataAreas_) {
        const int offset = static_cast<uint16_t>(address - area.start);
        if (offset >= area.size)
            continue;
        // A word to a line, or up to four bytes.
        const int left = area.size - offset;
        tuxape::Instruction data;
        if (area.words && left >= 2) {
            data.length = 2;
            data.text = "DW #" + hex(static_cast<unsigned>(byte(0) | byte(1) << 8), 4).toStdString();
        } else {
            data.length = area.words ? 1 : std::min(left, 4);
            data.text = "DB ";
            for (int n = 0; n < data.length; ++n)
                data.text += (n ? ",#" : "#") + hex(byte(n), 2).toStdString();
        }
        return data;
    }
    return tuxape::disassemble(address, [this](uint16_t a) { return (*memory_)[a]; });
}

std::vector<DisassemblyView::Line> DisassemblyView::lines(int count) const
{
    std::vector<Line> out;
    if (!memory_)
        return out;
    uint16_t address = top_;
    for (int i = 0; i < count; ++i) {
        const tuxape::Instruction instruction = instructionAt(address);
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
            if (instructionAt(candidate).length == length)
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
        const tuxape::Instruction here = instructionAt(selected_);
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

int MemoryDumpView::selectionStart() const
{
    return anchor_ < 0 ? cursor_ : std::min<int>(anchor_, cursor_);
}

int MemoryDumpView::selectionLength() const
{
    return anchor_ < 0 ? 1 : std::abs(anchor_ - cursor_) + 1;
}

void MemoryDumpView::select(int start, int length)
{
    start = std::clamp(start, 0, limit_ - 1);
    length = std::clamp(length, 1, limit_ - start);
    setCursor(static_cast<uint16_t>(start));
    anchor_ = length > 1 ? start + length - 1 : -1;
    viewport()->update();
}

void MemoryDumpView::setMarks(const std::vector<Mark>& marks)
{
    marks_ = marks;
    viewport()->update();
}

// The cursor moved by the keyboard or the mouse: with `extend` the
// selection stretches from where it was.
void MemoryDumpView::moveTo(int address, bool extend)
{
    const int anchor = extend ? (anchor_ < 0 ? cursor_ : anchor_) : -1;
    setCursor(static_cast<uint16_t>(address));
    anchor_ = anchor == cursor_ ? -1 : anchor;
    viewport()->update();
}

void MemoryDumpView::setCursor(uint16_t address)
{
    anchor_ = -1;
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
        // Memory breakpoints, the selection, and over them the byte under
        // the cursor, in both columns.
        const int first = selectionStart(), last = first + selectionLength() - 1;
        for (int index = 0; index < 16 && address + index < limit_; ++index) {
            const int at = address + index;
            QColor back;
            int watched = 0;
            for (const Mark& mark : marks_)
                if (at >= mark.start && at < mark.start + mark.length)
                    watched |= mark.write ? 2 : 1;
            if (watched)
                back = watched == 3 ? QColor(0xFF, 0xF0, 0x80) : watched == 2 ? QColor(0xFF, 0xB0, 0xB0) : QColor(0xB0, 0xF0, 0xB0);
            if (anchor_ >= 0 && at >= first && at <= last)
                back = palette().highlight().color().lighter(170);
            if (at == cursor_)
                back = palette().highlight().color();
            if (!back.isValid())
                continue;
            painter.fillRect(4 + character * (5 + index * 3), y, character * 2, height, back);
            painter.fillRect(4 + character * (5 + 48 + index), y, character, height, at == cursor_ ? back.lighter(150) : back);
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(4, y + ascent, lineText(row));
    }
}

// The byte at a place of the view, or -1.
int MemoryDumpView::byteAt(const QPointF& position) const
{
    const int character = fontMetrics().horizontalAdvance('0');
    const int row = static_cast<int>(position.y()) / lineHeight();
    const int column = (static_cast<int>(position.x()) - 4) / character;
    int index = -1;
    if (column >= 5 && column < 5 + 48)
        index = (column - 5) / 3;
    else if (column >= 53 && column < 69)
        index = column - 53;
    return index >= 0 && row >= 0 && top_ + row * 16 + index < limit_ ? top_ + row * 16 + index : -1;
}

void MemoryDumpView::mousePressEvent(QMouseEvent* event)
{
    const int at = byteAt(event->position());
    // A right click inside the selection leaves it be, for the menu.
    const bool inside = at >= selectionStart() && at < selectionStart() + selectionLength();
    if (at >= 0 && !(event->button() == Qt::RightButton && inside))
        moveTo(at, (event->modifiers() & Qt::ShiftModifier) != 0);
}

void MemoryDumpView::mouseMoveEvent(QMouseEvent* event)
{
    const int at = byteAt(event->position());
    if (at >= 0 && (event->buttons() & Qt::LeftButton))
        moveTo(at, true);
}

void MemoryDumpView::keyPressEvent(QKeyEvent* event)
{
    const QString text = event->text().toUpper();
    const int digit = text.size() == 1 ? QStringLiteral("0123456789ABCDEF").indexOf(text[0]) : -1;
    if (digit >= 0 && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        typeDigit(digit);
        return;
    }
    const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0;
    const auto move = [&](int by) { moveTo(static_cast<uint16_t>(cursor_ + by), extend); };
    switch (event->key()) {
    case Qt::Key_Left: move(-1); break;
    case Qt::Key_Right: move(1); break;
    case Qt::Key_Up: move(-16); break;
    case Qt::Key_Down: move(16); break;
    case Qt::Key_PageUp: move(-16 * (visibleLines() - 1)); break;
    case Qt::Key_PageDown: move(16 * (visibleLines() - 1)); break;
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
    anyView_ = new QRadioButton(tr("Any"));
    anyView_->setObjectName("rbAny");
    // With Any: which ROMs are in, and which bank of RAM.
    anyBox_ = new QWidget;
    auto* anyLayout = new QHBoxLayout(anyBox_);
    anyLayout->setContentsMargins(0, 0, 0, 0);
    anyLower_ = new QCheckBox(tr("Lower ROM"));
    anyLower_->setObjectName("ckLowerRom");
    anyUpper_ = new QCheckBox(tr("Upper ROM"));
    anyUpper_->setObjectName("ckUpperRom");
    anyUpperRom_ = new QLineEdit("0");
    anyUpperRom_->setObjectName("edUpperRom");
    anyUpperRom_->setMaxLength(2);
    anyUpperRom_->setFixedWidth(32);
    anyRamBank_ = new QLineEdit("C0");
    anyRamBank_->setObjectName("edRamBank");
    anyRamBank_->setMaxLength(2);
    anyRamBank_->setFixedWidth(32);
    anyLayout->addWidget(anyLower_);
    anyLayout->addWidget(anyUpper_);
    anyLayout->addWidget(anyUpperRom_);
    anyLayout->addWidget(new QLabel(tr("RAM")));
    anyLayout->addWidget(anyRamBank_);
    anyBox_->setVisible(false);
    auto* memoryLayout = new QHBoxLayout(memoryBox);
    memoryLayout->addWidget(readView_);
    memoryLayout->addWidget(writeViewButton_);
    memoryLayout->addWidget(anyView_);
    memoryLayout->addWidget(anyBox_);
    connect(writeViewButton_, &QRadioButton::toggled, this, [this](bool write) {
        writeView_ = write;
        refresh();
    });
    connect(anyView_, &QRadioButton::toggled, this, [this](bool any) {
        anyBox_->setVisible(any);
        refresh();
    });
    connect(anyLower_, &QCheckBox::toggled, this, &DebuggerDialog::refresh);
    connect(anyUpper_, &QCheckBox::toggled, this, &DebuggerDialog::refresh);
    connect(anyUpperRom_, &QLineEdit::editingFinished, this, &DebuggerDialog::refresh);
    connect(anyRamBank_, &QLineEdit::editingFinished, this, &DebuggerDialog::refresh);

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
        writeBytes(address, QByteArray(1, static_cast<char>(value)));
    });
    connect(resetTimer, &QPushButton::clicked, this, [this] {
        emulator_->resetCycles();
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
    key(Qt::CTRL | Qt::Key_F, &DebuggerDialog::showFind);
    connect(new QShortcut(Qt::Key_F3, this), &QShortcut::activated, this, [this] { findAgain(); });
    dump_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dump_, &QWidget::customContextMenuRequested, this, &DebuggerDialog::dumpMenu);
    disassembly_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(disassembly_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QMenu menu(this);
        menu.addAction(tr("Find"), Qt::CTRL | Qt::Key_F, this, &DebuggerDialog::showFind);
        menu.addAction(tr("Goto"), Qt::CTRL | Qt::Key_G, this, &DebuggerDialog::askGoTo);
        menu.exec(disassembly_->viewport()->mapToGlobal(at));
    });
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
        tuxape::Memory& memory = cpc.memory();
        const Mapping saved = applyView(memory);
        for (int address = 0; address < 0x10000; ++address)
            memory_[static_cast<size_t>(address)] = write ? memory.readRam(static_cast<uint16_t>(address))
                                                          : memory.read(static_cast<uint16_t>(address));
        restoreView(memory, saved);
        const auto& z80 = cpc.cpu();
        auto pair = [&](int high, int low) { return static_cast<uint16_t>(z80.reg[high] << 8 | z80.reg[low]); };
        return Cpu{pair(z80.A, z80.F), pair(z80.B, z80.C), pair(z80.D, z80.E), pair(z80.H, z80.L), z80.af2, z80.bc2,
                   z80.de2,            z80.hl2,            z80.ix,             z80.iy,             z80.sp,  z80.pc,
                   z80.i,              z80.r,              z80.im,             z80.iff1,           cpc.instructionTime()};
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
    timer_->setText(QString::number(cpu.time - emulator_->cycleBase()));

    stack_->clear();
    for (int i = 0; i < 32; ++i) {
        const uint16_t address = static_cast<uint16_t>(cpu.sp + i * 2);
        const unsigned value = memory_[address] | memory_[static_cast<uint16_t>(address + 1)] << 8;
        stack_->addItem(hex(address, 4) + ": " + hex(value, 4));
    }

    std::vector<MemoryDumpView::Mark> marks;
    for (const Emulator::MemoryBreak& point : emulator_->memoryBreaks())
        marks.push_back({point.address, point.size, point.write});
    dump_->setMarks(marks);
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

// ---- The memory dump's tools ------------------------------------------------

void DebuggerDialog::dumpMenu(const QPoint& at)
{
    QMenu menu(this);
    menu.addAction(tr("Find"), Qt::CTRL | Qt::Key_F, this, &DebuggerDialog::showFind);
    menu.addAction(tr("Goto"), Qt::CTRL | Qt::Key_G, this, &DebuggerDialog::askGoTo);
    menu.addAction(tr("Select Block"), this, &DebuggerDialog::showSelectBlock);
    menu.addSeparator();
    menu.addAction(tr("Load"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Load"));
        if (!path.isEmpty() && !loadAt(path))
            QMessageBox::warning(this, windowTitle(), tr("Cannot read %1.").arg(path));
    });
    menu.addAction(tr("Save"), this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save"));
        if (!path.isEmpty() && !saveSelection(path))
            QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(path));
    });
    menu.addSeparator();
    menu.addAction(tr("Breakpoint on Read"), this, [this] { breakOnSelection(false); });
    menu.addAction(tr("Breakpoint on Write"), this, [this] { breakOnSelection(true); });
    menu.addSeparator();
    menu.addAction(tr("Compare To"), this, &DebuggerDialog::showCompare);
    menu.addAction(tr("Fill"), this, &DebuggerDialog::showFill);
    menu.addAction(tr("Disassemble"), this, &DebuggerDialog::showDisassemble);
    menu.addSeparator();
    menu.addAction(tr("Mark as Data"), this, [this] { markData(false); });
    menu.addAction(tr("Clear Data Area"), this, &DebuggerDialog::clearDataArea);
    menu.exec(dump_->viewport()->mapToGlobal(at));
}

QByteArray DebuggerDialog::selectedBytes() const
{
    QByteArray bytes;
    const int start = dump_->selectionStart();
    for (int n = 0; n < dump_->selectionLength(); ++n)
        bytes += static_cast<char>(memory_[static_cast<uint16_t>(start + n)]);
    return bytes;
}

// Bytes put in the machine's memory, as far as its top.
void DebuggerDialog::writeBytes(int start, const QByteArray& bytes)
{
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        const Mapping saved = applyView(cpc.memory());
        for (qsizetype n = 0; n < bytes.size() && start + n < 0x10000; ++n)
            cpc.memory().write(static_cast<uint16_t>(start + n), static_cast<uint8_t>(bytes[n]));
        restoreView(cpc.memory(), saved);
    });
    refresh();
}

// With the Any view, memory is looked at, and written to, as the boxes
// beside it say; the machine's own mapping is put back afterwards.
template <class Memory>
DebuggerDialog::Mapping DebuggerDialog::applyView(Memory& memory) const
{
    const Mapping saved{memory.lowerRomEnabled(), memory.upperRomEnabled(), memory.selectedUpperRom(), memory.ramBank()};
    if (anyView_->isChecked()) {
        bool ok = false;
        const unsigned rom = anyUpperRom_->text().toUInt(&ok, 16);
        const unsigned bank = anyRamBank_->text().toUInt(nullptr, 16);
        memory.setRomEnables(anyLower_->isChecked(), anyUpper_->isChecked());
        memory.selectUpperRom(static_cast<uint8_t>(ok ? rom : 0));
        memory.selectRamBank(static_cast<uint8_t>(bank >= 0xC0 && bank <= 0xFF ? bank : 0xC0), 0x7F);
    }
    return saved;
}

template <class Memory>
void DebuggerDialog::restoreView(Memory& memory, const Mapping& saved) const
{
    if (!anyView_->isChecked())
        return;
    memory.setRomEnables(saved.lower, saved.upper);
    memory.selectUpperRom(saved.rom);
    memory.selectRamBank(saved.bank, 0x7F);
}

void DebuggerDialog::setAnyView(bool lowerRom, bool upperRom, int upperRomNumber, int ramBank)
{
    anyLower_->setChecked(lowerRom);
    anyUpper_->setChecked(upperRom);
    anyUpperRom_->setText(QString::number(upperRomNumber, 16).toUpper());
    anyRamBank_->setText(QString::number(ramBank, 16).toUpper());
    anyView_->setChecked(true);
    refresh();
}

bool DebuggerDialog::anyView() const
{
    return anyView_->isChecked();
}

int DebuggerDialog::find(FindKind kind, const QString& what, bool caseSensitive)
{
    lastFindKind_ = kind;
    lastFind_ = what;
    lastFindCase_ = caseSensitive;
    if (kind == FindKind::Assembler) {
        // Instruction after instruction from the one chosen.
        const QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(what.simplified()),
                                         QRegularExpression::CaseInsensitiveOption);
        uint16_t address = disassembly_->selected();
        for (int gone = 0; gone < 0x10000;) {
            const int length = disassembly_->instructionAt(address).length;
            address = static_cast<uint16_t>(address + length);
            gone += length;
            if (pattern.match(QString::fromLatin1(disassembly_->instructionAt(address).text.c_str())).hasMatch()) {
                disassembly_->show(address);
                disassembly_->select(address);
                return address;
            }
        }
        return -1;
    }
    // What each byte must be, or -1 for any.
    std::vector<int> wanted;
    if (kind == FindKind::Hex) {
        for (const QString& item : what.split(' ', Qt::SkipEmptyParts)) {
            bool ok = true;
            const unsigned value = item.contains('?') ? 0 : item.toUInt(&ok, 16);
            if (!ok || value > 0xFF)
                return -1;
            wanted.push_back(item.contains('?') ? -1 : static_cast<int>(value));
        }
    } else {
        for (const char c : what.toLatin1())
            wanted.push_back(c == '?' ? -1 : static_cast<uint8_t>(c));
    }
    if (wanted.empty())
        return -1;
    const bool fold = kind == FindKind::Text && !caseSensitive;
    const auto same = [fold](int a, int b) { return fold ? QChar(a).toLower() == QChar(b).toLower() : a == b; };
    for (int n = 1; n <= 0x10000; ++n) {
        const uint16_t address = static_cast<uint16_t>(dump_->selectionStart() + n);
        size_t k = 0;
        while (k < wanted.size() && (wanted[k] < 0 || same(memory_[static_cast<uint16_t>(address + k)], wanted[k])))
            ++k;
        if (k == wanted.size()) {
            dump_->select(address, static_cast<int>(wanted.size()));
            return address;
        }
    }
    return -1;
}

int DebuggerDialog::findAgain()
{
    return lastFind_.isEmpty() ? -1 : find(lastFindKind_, lastFind_, lastFindCase_);
}

void DebuggerDialog::selectBlock(unsigned start, unsigned end)
{
    if (start <= end && end <= 0xFFFF)
        dump_->select(static_cast<int>(start), static_cast<int>(end - start + 1));
}

bool DebuggerDialog::loadAt(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll().left(0x10000 - dump_->selectionStart());
    const int start = dump_->selectionStart();
    writeBytes(start, bytes);
    if (!bytes.isEmpty())
        dump_->select(start, static_cast<int>(bytes.size()));
    return true;
}

bool DebuggerDialog::saveSelection(const QString& path)
{
    const QByteArray bytes = selectedBytes();
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

void DebuggerDialog::fillSelection(const QByteArray& pattern)
{
    if (pattern.isEmpty())
        return;
    const int start = dump_->selectionStart(), length = dump_->selectionLength();
    QByteArray bytes;
    for (int n = 0; n < length; ++n)
        bytes += pattern[n % pattern.size()];
    writeBytes(start, bytes);
    dump_->select(start, length);
}

namespace {

// The runs of bytes that differ between a selection and something else.
QStringList differences(int start, const QByteArray& mine, const QByteArray& other)
{
    QStringList rows;
    for (qsizetype n = 0; n < mine.size();) {
        if (n < other.size() && mine[n] == other[n]) {
            ++n;
            continue;
        }
        qsizetype end = n;
        while (end < mine.size() && (end >= other.size() || mine[end] != other[end]))
            ++end;
        rows << hex(static_cast<unsigned>(start + n), 4) + '|' + hex(static_cast<unsigned>(end - n), 4);
        n = end;
    }
    return rows;
}

}  // namespace

QStringList DebuggerDialog::compareSelection(unsigned address) const
{
    QByteArray other;
    for (int n = 0; n < dump_->selectionLength(); ++n)
        other += static_cast<char>(memory_[static_cast<uint16_t>(address + static_cast<unsigned>(n))]);
    return differences(dump_->selectionStart(), selectedBytes(), other);
}

QStringList DebuggerDialog::compareSelectionWithFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {tr("Cannot read %1.").arg(path)};
    return differences(dump_->selectionStart(), selectedBytes(), file.readAll());
}

void DebuggerDialog::breakOnSelection(bool write)
{
    std::vector<Emulator::MemoryBreak> breaks = emulator_->memoryBreaks();
    Emulator::MemoryBreak point;
    point.address = static_cast<uint16_t>(dump_->selectionStart());
    point.size = dump_->selectionLength();
    point.write = write;
    breaks.push_back(point);
    emulator_->setMemoryBreaks(breaks);
    refresh();
    emit breakpointsChanged();
}

QString DebuggerDialog::disassembleSelection() const
{
    const int start = dump_->selectionStart(), end = start + dump_->selectionLength();
    QString source = "org #" + hex(static_cast<unsigned>(start), 4) + '\n';
    for (int address = start; address < end;) {
        const tuxape::Instruction instruction = disassembly_->instructionAt(static_cast<uint16_t>(address));
        // One that would run past the end is given as its bytes.
        if (address + instruction.length > end) {
            for (; address < end; ++address)
                source += "DB #" + hex(memory_[static_cast<uint16_t>(address)], 2) + '\n';
            break;
        }
        source += QString::fromLatin1(instruction.text.c_str()) + '\n';
        address += instruction.length;
    }
    return source;
}

void DebuggerDialog::setDataAreas(const std::vector<DisassemblyView::DataArea>& areas)
{
    dataAreas_ = areas;
    disassembly_->setDataAreas(dataAreas_);
}

void DebuggerDialog::markData(bool words)
{
    clearDataArea();
    dataAreas_.push_back({static_cast<uint16_t>(dump_->selectionStart()), dump_->selectionLength(), words});
    std::sort(dataAreas_.begin(), dataAreas_.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    disassembly_->setDataAreas(dataAreas_);
}

void DebuggerDialog::addDataArea(unsigned start, int size)
{
    if (size < 1 || start > 0xFFFF)
        return;
    const int from = static_cast<int>(start), end = std::min(from + size, 0x10000);
    std::erase_if(dataAreas_, [&](const DisassemblyView::DataArea& area) { return area.start < end && area.start + area.size > from; });
    dataAreas_.push_back({static_cast<uint16_t>(from), end - from, false});
    std::sort(dataAreas_.begin(), dataAreas_.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    disassembly_->setDataAreas(dataAreas_);
}

// Takes away the areas the selection touches.
void DebuggerDialog::clearDataArea()
{
    const int start = dump_->selectionStart(), end = start + dump_->selectionLength();
    std::erase_if(dataAreas_, [&](const DisassemblyView::DataArea& area) { return area.start < end && area.start + area.size > start; });
    disassembly_->setDataAreas(dataAreas_);
}

void DebuggerDialog::showFind()
{
    QDialog box(this);
    box.setObjectName("FindDialog");
    box.setWindowTitle(tr("Find"));
    auto* layout = new QVBoxLayout(&box);
    auto* tabs = new QTabWidget;
    tabs->setObjectName("PageControl");
    QLineEdit* edits[3];
    const char* const kNames[3] = {"edText", "edHex", "edAssembler"};
    const QString kTitles[3] = {tr("Text"), tr("Hex Data"), tr("Assembler")};
    auto* caseSensitive = new QCheckBox(tr("Case sensitive"));
    caseSensitive->setObjectName("ckCase");
    caseSensitive->setChecked(lastFindCase_);
    for (int page = 0; page < 3; ++page) {
        auto* widget = new QWidget;
        auto* inside = new QVBoxLayout(widget);
        edits[page] = new QLineEdit(static_cast<int>(lastFindKind_) == page ? lastFind_ : QString());
        edits[page]->setObjectName(kNames[page]);
        edits[page]->setMinimumWidth(260);
        inside->addWidget(edits[page]);
        if (page == 0)
            inside->addWidget(caseSensitive);
        inside->addStretch();
        tabs->addTab(widget, kTitles[page]);
    }
    tabs->setCurrentIndex(static_cast<int>(lastFindKind_));
    layout->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    edits[tabs->currentIndex()]->setFocus();
    if (box.exec() != QDialog::Accepted)
        return;
    const QString what = edits[tabs->currentIndex()]->text();
    if (find(static_cast<FindKind>(tabs->currentIndex()), what, caseSensitive->isChecked()) < 0)
        QMessageBox::information(this, windowTitle(), tr("'%1' was not found.").arg(what));
}

void DebuggerDialog::showSelectBlock()
{
    QDialog box(this);
    box.setObjectName("SelectBlock");
    box.setWindowTitle(tr("Select Block"));
    auto* form = new QFormLayout(&box);
    auto* start = new QLineEdit(hex(static_cast<unsigned>(dump_->selectionStart()), 4));
    start->setObjectName("edStart");
    auto* end = new QLineEdit(hex(static_cast<unsigned>(dump_->selectionStart() + dump_->selectionLength() - 1), 4));
    end->setObjectName("edEnd");
    form->addRow(tr("Start:"), start);
    form->addRow(tr("End:"), end);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    if (box.exec() != QDialog::Accepted)
        return;
    bool a = false, b = false;
    const unsigned first = start->text().toUInt(&a, 16), last = end->text().toUInt(&b, 16);
    if (a && b)
        selectBlock(first, last);
}

void DebuggerDialog::showFill()
{
    QDialog box(this);
    box.setObjectName("FillDialog");
    box.setWindowTitle(tr("Fill Memory"));
    auto* layout = new QVBoxLayout(&box);
    auto* tabs = new QTabWidget;
    tabs->setObjectName("PageControl");
    auto* hexData = new QLineEdit;
    hexData->setObjectName("edHex");
    hexData->setMinimumWidth(260);
    auto* text = new QLineEdit;
    text->setObjectName("edText");
    tabs->addTab(hexData, tr("Hex Data"));
    tabs->addTab(text, tr("Text"));
    layout->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    if (box.exec() != QDialog::Accepted)
        return;
    QByteArray pattern = text->text().toLatin1();
    if (tabs->currentIndex() == 0) {
        pattern.clear();
        for (const QString& item : hexData->text().split(' ', Qt::SkipEmptyParts)) {
            bool ok = false;
            const unsigned value = item.toUInt(&ok, 16);
            if (!ok || value > 0xFF)
                return;
            pattern += static_cast<char>(value);
        }
    }
    fillSelection(pattern);
}

void DebuggerDialog::showCompare()
{
    QDialog box(this);
    box.setObjectName("CompareDialog");
    box.setWindowTitle(tr("Compare Memory"));
    auto* layout = new QVBoxLayout(&box);
    auto* tabs = new QTabWidget;
    tabs->setObjectName("PageControl");
    auto* address = new QLineEdit;
    address->setObjectName("edAddress");
    address->setMinimumWidth(260);
    auto* fileRow = new QWidget;
    auto* fileLayout = new QHBoxLayout(fileRow);
    auto* fileName = new QLineEdit;
    fileName->setObjectName("edFile");
    auto* browse = new QPushButton("...");
    fileLayout->addWidget(fileName);
    fileLayout->addWidget(browse);
    tabs->addTab(address, tr("Memory"));
    tabs->addTab(fileRow, tr("File"));
    layout->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(browse, &QPushButton::clicked, &box, [&] {
        const QString path = QFileDialog::getOpenFileName(&box, tr("Compare To"));
        if (!path.isEmpty())
            fileName->setText(path);
    });
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    if (box.exec() != QDialog::Accepted)
        return;
    bool ok = true;
    const unsigned other = tabs->currentIndex() == 0 ? address->text().toUInt(&ok, 16) : 0;
    if (!ok || other > 0xFFFF)
        return;
    const QStringList rows = tabs->currentIndex() == 0 ? compareSelection(other) : compareSelectionWithFile(fileName->text());

    QDialog results(this);
    results.setObjectName("CompareResults");
    results.setWindowTitle(tr("Comparison Results"));
    auto* resultsLayout = new QVBoxLayout(&results);
    auto* list = new QTreeWidget;
    list->setObjectName("lvDifferences");
    list->setHeaderLabels({tr("Address"), tr("Size"), tr("Data")});
    list->setRootIsDecorated(false);
    for (const QString& row : rows) {
        bool isAddress = false;
        const unsigned at = row.section('|', 0, 0).toUInt(&isAddress, 16);
        const unsigned size = row.section('|', 1, 1).toUInt(nullptr, 16);
        QString data;
        for (unsigned n = 0; isAddress && n < size && n < 16; ++n)
            data += hex(memory_[static_cast<uint16_t>(at + n)], 2) + ' ';
        new QTreeWidgetItem(list, {row.section('|', 0, 0), row.section('|', 1, 1), data.trimmed()});
    }
    resultsLayout->addWidget(rows.isEmpty() ? static_cast<QWidget*>(new QLabel(tr("The two are the same."))) : list);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    resultsLayout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &results, &QDialog::reject);
    results.exec();
}

void DebuggerDialog::showDisassemble()
{
    QDialog box(this);
    box.setObjectName("DisassembleDialog");
    box.setWindowTitle(tr("Disassemble Output"));
    auto* layout = new QVBoxLayout(&box);
    auto* toTab = new QRadioButton(tr("New Assembler Tab"));
    toTab->setObjectName("rbNewTab");
    toTab->setChecked(true);
    auto* toFile = new QRadioButton(tr("File"));
    toFile->setObjectName("rbFile");
    auto* fileRow = new QHBoxLayout;
    auto* fileName = new QLineEdit;
    fileName->setObjectName("edFile");
    fileName->setMinimumWidth(260);
    auto* browse = new QPushButton("...");
    fileRow->addWidget(fileName);
    fileRow->addWidget(browse);
    auto* append = new QCheckBox(tr("Append Output"));
    append->setObjectName("ckAppend");
    layout->addWidget(toTab);
    layout->addWidget(toFile);
    layout->addLayout(fileRow);
    layout->addWidget(append);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(browse, &QPushButton::clicked, &box, [&] {
        const QString path = QFileDialog::getSaveFileName(&box, tr("Disassemble Output"), QString(), tr("Assembler Files (*.asm);;All Files (*)"));
        if (!path.isEmpty()) {
            fileName->setText(path);
            toFile->setChecked(true);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    if (box.exec() != QDialog::Accepted)
        return;
    const QString source = disassembleSelection();
    if (toTab->isChecked()) {
        emit sourceProduced(source);
        return;
    }
    QFile file(fileName->text());
    if (!file.open(append->isChecked() ? QIODevice::WriteOnly | QIODevice::Append : QIODevice::WriteOnly)
        || file.write(source.toLatin1()) < 0)
        QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(fileName->text()));
}

void DebuggerDialog::showDataAreas()
{
    QDialog box(this);
    box.setObjectName("DataAreas");
    box.setWindowTitle(tr("Data Areas"));
    auto* layout = new QVBoxLayout(&box);
    auto* list = new QTreeWidget;
    list->setObjectName("lvDataAreas");
    list->setHeaderLabels({tr("Start"), tr("End"), tr("Type")});
    list->setRootIsDecorated(false);
    const auto fill = [&] {
        list->clear();
        for (size_t i = 0; i < dataAreas_.size(); ++i) {
            const DisassemblyView::DataArea& area = dataAreas_[i];
            auto* item = new QTreeWidgetItem(list, {hex(area.start, 4), hex(static_cast<unsigned>(area.start + area.size - 1), 4)});
            // Bytes or words, chosen in the row itself.
            auto* type = new QComboBox;
            type->addItems({tr("Bytes"), tr("Words")});
            type->setCurrentIndex(area.words ? 1 : 0);
            list->setItemWidget(item, 2, type);
            connect(type, &QComboBox::currentIndexChanged, this, [this, i](int index) {
                if (i < dataAreas_.size()) {
                    dataAreas_[i].words = index == 1;
                    disassembly_->setDataAreas(dataAreas_);
                }
            });
        }
    };
    fill();
    layout->addWidget(list);
    auto* row = new QHBoxLayout;
    auto* remove = new QPushButton(tr("Clear"));
    remove->setObjectName("bClear");
    auto* removeAll = new QPushButton(tr("Clear All"));
    removeAll->setObjectName("bClearAll");
    auto* close = new QPushButton(tr("&Close"));
    row->addWidget(remove);
    row->addWidget(removeAll);
    row->addStretch();
    row->addWidget(close);
    layout->addLayout(row);
    connect(remove, &QPushButton::clicked, &box, [&] {
        const int index = list->indexOfTopLevelItem(list->currentItem());
        if (index >= 0) {
            dataAreas_.erase(dataAreas_.begin() + index);
            disassembly_->setDataAreas(dataAreas_);
            fill();
        }
    });
    connect(removeAll, &QPushButton::clicked, &box, [&] {
        setDataAreas({});
        fill();
    });
    connect(close, &QPushButton::clicked, &box, &QDialog::accept);
    box.resize(340, 240);
    box.exec();
}
