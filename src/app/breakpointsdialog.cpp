#include "breakpointsdialog.h"

#include "help.h"

#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "emulator.h"

namespace {

// The CPC's devices by the address lines they answer to. Some are a part
// of what a port takes, told by the value written.
struct Device {
    const char* name;
    unsigned port, mask;
    bool input, output;
    const char* filter;
};
const Device kDevices[] = {
    {"Gate Array", 0x7F00, 0xC000, false, true, ""},
    {"  Palette Register Select", 0x7F00, 0xC000, false, true, "value and #C0 = #00"},
    {"  Palette Write", 0x7F00, 0xC000, false, true, "value and #C0 = #40"},
    {"  Interrupt, ROM and Mode", 0x7F00, 0xC000, false, true, "value and #C0 = #80"},
    {"  Secondary ROM Mapping", 0x7F00, 0xC000, false, true, "value and #E0 = #A0"},
    {"PAL (RAM Bank Select)", 0x7F00, 0x8000, false, true, "value and #C0 = #C0"},
    {"CRTC (6845)", 0xBC00, 0x4000, true, true, ""},
    {"  Register Select", 0xBC00, 0x4300, false, true, ""},
    {"  Register Write", 0xBD00, 0x4300, false, true, ""},
    {"  Register Read", 0xBF00, 0x4300, true, false, ""},
    {"  Status Register Read", 0xBE00, 0x4300, true, false, ""},
    {"Upper ROM Select", 0xDF00, 0x2000, false, true, ""},
    {"Printer Port", 0xEF00, 0x1000, false, true, ""},
    {"PPI (8255)", 0xF400, 0x0800, true, true, ""},
    {"  Port A", 0xF400, 0x0B00, true, true, ""},
    {"  Port B", 0xF500, 0x0B00, true, true, ""},
    {"  Port C", 0xF600, 0x0B00, true, true, ""},
    {"  Control", 0xF700, 0x0B00, true, true, ""},
    {"FDC (uPD765)", 0xFB7E, 0x0580, true, true, ""},
    {"  Status", 0xFB7E, 0x0581, true, false, ""},
    {"  Data", 0xFB7F, 0x0581, true, true, ""},
    {"FDC Motor", 0xFA7E, 0x0580, true, true, ""},
    {"User Defined Port", 0, 0xFFFF, true, true, ""},
};
constexpr int kDeviceCount = static_cast<int>(sizeof kDevices / sizeof kDevices[0]);

QString hex4(unsigned value)
{
    return QString("%1").arg(value & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper();
}

QString countText(const Emulator::BreakProps& props)
{
    return props.passCount > 0 ? QString("%1/%2").arg(props.count).arg(props.passCount) : QString::number(props.count);
}

}  // namespace

BreakpointsDialog::BreakpointsDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("Breakpoints"));
    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget;
    tabs_->setObjectName("PageControl");
    const auto addPage = [&](const QString& title, const char* objectName, const QStringList& columns) {
        auto* view = new QTreeWidget;
        view->setObjectName(objectName);
        view->setHeaderLabels(columns);
        view->setRootIsDecorated(false);
        view->setAllColumnsShowFocus(true);
        view->header()->setStretchLastSection(true);
        tabs_->addTab(view, title);
        connect(view, &QTreeWidget::itemDoubleClicked, this, &BreakpointsDialog::showProperties);
        connect(view, &QTreeWidget::itemSelectionChanged, this, &BreakpointsDialog::updateButtons);
    };
    addPage(tr("Code"), "lvCode", {tr("Address"), tr("Type"), tr("Count"), tr("Condition")});
    addPage(tr("Memory"), "lvMemory", {tr("Address"), tr("Size"), tr("Type"), tr("Count"), tr("Condition")});
    addPage(tr("Input/Output"), "lvIO", {tr("Description"), tr("Type"), tr("Count"), tr("Condition")});
    // Room for the condition, which comes last.
    for (int page = Code; page <= InputOutput; ++page)
        for (int column = 0; column < list(page)->columnCount() - 1; ++column)
            list(page)->setColumnWidth(column, column == 0 ? (page == InputOutput ? 200 : 90) : 75);
    layout->addWidget(tabs_);

    auto* buttons = new QHBoxLayout;
    add_ = new QPushButton(tr("Add"));
    add_->setObjectName("bAdd");
    clear_ = new QPushButton(tr("Clear"));
    clear_->setObjectName("bClear");
    clearAll_ = new QPushButton(tr("Clear All"));
    clearAll_->setObjectName("bClearAll");
    auto* close = new QPushButton(tr("&Close"));
    close->setObjectName("bClose");
    auto* help = new QPushButton(tr("&Help"));
    help->setObjectName("bHelp");
    wireHelp(help);
    buttons->addWidget(add_);
    buttons->addWidget(clear_);
    buttons->addWidget(clearAll_);
    buttons->addStretch();
    buttons->addWidget(close);
    buttons->addWidget(help);
    layout->addLayout(buttons);
    resize(520, 230);

    connect(add_, &QPushButton::clicked, this, &BreakpointsDialog::showAdd);
    connect(clear_, &QPushButton::clicked, this, [this] {
        const QTreeWidget* view = list(page());
        if (view->currentItem())
            clear(page(), view->indexOfTopLevelItem(view->currentItem()));
    });
    connect(clearAll_, &QPushButton::clicked, this, [this] { clearAll(page()); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(tabs_, &QTabWidget::currentChanged, this, &BreakpointsDialog::updateButtons);
    // The counts move while the machine runs.
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible() && !emulator_->isPaused())
            refresh();
    });
    timer->start(500);
    refresh();
}

QTreeWidget* BreakpointsDialog::list(int page) const
{
    return static_cast<QTreeWidget*>(tabs_->widget(page));
}

int BreakpointsDialog::page() const
{
    return tabs_->currentIndex();
}

void BreakpointsDialog::setPage(int page)
{
    tabs_->setCurrentIndex(page);
}

void BreakpointsDialog::select(int row)
{
    QTreeWidget* view = list(page());
    view->setCurrentItem(view->topLevelItem(row));
}

void BreakpointsDialog::updateButtons()
{
    // Code breakpoints are set in the debugger and in the assembler.
    add_->setEnabled(page() != Code);
    clear_->setEnabled(list(page())->currentItem() != nullptr);
    clearAll_->setEnabled(list(page())->topLevelItemCount() > 0);
}

QStringList BreakpointsDialog::devices()
{
    QStringList names;
    for (const Device& device : kDevices)
        names << device.name;
    return names;
}

void BreakpointsDialog::refresh()
{
    // Each page is filled again with the row that was chosen chosen still.
    const auto fill = [this](int page, const QList<QStringList>& rows) {
        QTreeWidget* view = list(page);
        const int chosen = view->currentItem() ? view->indexOfTopLevelItem(view->currentItem()) : -1;
        if (view->topLevelItemCount() != rows.size()) {
            view->clear();
            for (const QStringList& row : rows)
                new QTreeWidgetItem(view, row);
            if (chosen >= 0 && chosen < rows.size())
                view->setCurrentItem(view->topLevelItem(chosen));
            return;
        }
        for (int r = 0; r < rows.size(); ++r)
            for (int column = 0; column < rows[r].size(); ++column)
                if (view->topLevelItem(r)->text(column) != rows[r][column])
                    view->topLevelItem(r)->setText(column, rows[r][column]);
    };
    QList<QStringList> code, memory, io;
    for (const uint16_t address : emulator_->breakpoints()) {
        const Emulator::BreakProps props = emulator_->breakProps(address);
        code << QStringList{hex4(address), tr("User"), countText(props), QString::fromStdString(props.condition)};
    }
    for (const Emulator::MemoryBreak& point : emulator_->memoryBreaks())
        memory << QStringList{hex4(point.address), hex4(static_cast<unsigned>(point.size)), point.write ? tr("Write") : tr("Read"),
                              countText(point.props), QString::fromStdString(point.props.condition)};
    for (const Emulator::IoBreak& point : emulator_->ioBreaks()) {
        const QString what = point.description.empty() ? tr("User - Port AND %1 = %2").arg(hex4(point.mask), hex4(point.port & point.mask))
                                                       : QString::fromStdString(point.description);
        const QString type = point.input && point.output ? tr("Read/Write") : point.input ? tr("Read") : tr("Write");
        io << QStringList{what, type, countText(point.props), QString::fromStdString(point.props.condition)};
    }
    fill(Code, code);
    fill(Memory, memory);
    fill(InputOutput, io);
    updateButtons();
}

QStringList BreakpointsDialog::rows(int page) const
{
    QStringList out;
    const QTreeWidget* view = list(page);
    for (int r = 0; r < view->topLevelItemCount(); ++r) {
        QStringList cells;
        for (int column = 0; column < view->columnCount(); ++column)
            cells << view->topLevelItem(r)->text(column);
        out << cells.join('|');
    }
    return out;
}

bool BreakpointsDialog::setProperties(int page, int row, const QString& condition, int passCount)
{
    const std::string text = condition.trimmed().toStdString();
    if (row < 0 || passCount < 0 || (!text.empty() && !emulator_->conditionValid(text)))
        return false;
    if (page == Code) {
        const std::set<uint16_t> addresses = emulator_->breakpoints();
        if (row >= static_cast<int>(addresses.size()))
            return false;
        emulator_->setBreakProps(*std::next(addresses.begin(), row), text, passCount);
    } else if (page == Memory) {
        std::vector<Emulator::MemoryBreak> breaks = emulator_->memoryBreaks();
        if (row >= static_cast<int>(breaks.size()))
            return false;
        breaks[static_cast<size_t>(row)].props = {text, passCount, 0};
        emulator_->setMemoryBreaks(breaks);
    } else {
        std::vector<Emulator::IoBreak> breaks = emulator_->ioBreaks();
        if (row >= static_cast<int>(breaks.size()))
            return false;
        breaks[static_cast<size_t>(row)].props = {text, passCount, 0};
        emulator_->setIoBreaks(breaks);
    }
    refresh();
    return true;
}

bool BreakpointsDialog::addIoBreak(int device, const QString& condition, int passCount, unsigned port, unsigned mask,
                                   bool input, bool output)
{
    const std::string text = condition.trimmed().toStdString();
    if (device < 0 || device >= kDeviceCount || passCount < 0 || (!text.empty() && !emulator_->conditionValid(text)))
        return false;
    Emulator::IoBreak point;
    if (device < kDeviceCount - 1) {
        const Device& known = kDevices[device];
        point.description = QString(known.name).trimmed().toStdString();
        point.filter = known.filter;
        point.port = static_cast<uint16_t>(known.port);
        point.mask = static_cast<uint16_t>(known.mask);
        point.input = known.input;
        point.output = known.output;
    } else {
        if (port > 0xFFFF || mask > 0xFFFF || (!input && !output))
            return false;
        point.port = static_cast<uint16_t>(port);
        point.mask = static_cast<uint16_t>(mask);
        point.input = input;
        point.output = output;
    }
    point.props = {text, passCount, 0};
    std::vector<Emulator::IoBreak> breaks = emulator_->ioBreaks();
    breaks.push_back(point);
    emulator_->setIoBreaks(breaks);
    refresh();
    emit changed();
    return true;
}

bool BreakpointsDialog::addMemoryBreak(unsigned address, int size, bool write, const QString& condition, int passCount)
{
    const std::string text = condition.trimmed().toStdString();
    if (address > 0xFFFF || size < 1 || address + static_cast<unsigned>(size) > 0x10000 || passCount < 0
        || (!text.empty() && !emulator_->conditionValid(text)))
        return false;
    Emulator::MemoryBreak point;
    point.address = static_cast<uint16_t>(address);
    point.size = size;
    point.write = write;
    point.props = {text, passCount, 0};
    std::vector<Emulator::MemoryBreak> breaks = emulator_->memoryBreaks();
    breaks.push_back(point);
    emulator_->setMemoryBreaks(breaks);
    refresh();
    emit changed();
    return true;
}

void BreakpointsDialog::clear(int page, int row)
{
    if (row < 0)
        return;
    if (page == Code) {
        std::set<uint16_t> addresses = emulator_->breakpoints();
        if (row < static_cast<int>(addresses.size()))
            addresses.erase(std::next(addresses.begin(), row));
        emulator_->setBreakpoints(addresses);
    } else if (page == Memory) {
        std::vector<Emulator::MemoryBreak> breaks = emulator_->memoryBreaks();
        if (row < static_cast<int>(breaks.size()))
            breaks.erase(breaks.begin() + row);
        emulator_->setMemoryBreaks(breaks);
    } else {
        std::vector<Emulator::IoBreak> breaks = emulator_->ioBreaks();
        if (row < static_cast<int>(breaks.size()))
            breaks.erase(breaks.begin() + row);
        emulator_->setIoBreaks(breaks);
    }
    refresh();
    emit changed();
}

void BreakpointsDialog::clearAll(int page)
{
    if (page == Code)
        emulator_->setBreakpoints({});
    else if (page == Memory)
        emulator_->setMemoryBreaks({});
    else
        emulator_->setIoBreaks({});
    refresh();
    emit changed();
}

void BreakpointsDialog::showProperties()
{
    const QTreeWidget* view = list(page());
    if (!view->currentItem())
        return;
    const int row = view->indexOfTopLevelItem(view->currentItem());
    const QString count = view->currentItem()->text(view->columnCount() - 2);
    QDialog box(this);
    box.setObjectName("BreakProps");
    box.setWindowTitle(tr("Breakpoint Properties"));
    auto* form = new QFormLayout(&box);
    auto* condition = new QLineEdit(view->currentItem()->text(view->columnCount() - 1));
    condition->setObjectName("edCondition");
    condition->setMinimumWidth(220);
    auto* passCount = new QLineEdit(count.contains('/') ? count.section('/', 1) : QString());
    passCount->setObjectName("edPassCount");
    form->addRow(tr("Condition:"), condition);
    form->addRow(tr("Pass Count:"), passCount);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &box, [&] {
        bool number = true;
        const int passes = passCount->text().trimmed().isEmpty() ? 0 : passCount->text().toInt(&number);
        if (number && setProperties(page(), row, condition->text(), passes))
            box.accept();
        else
            QMessageBox::warning(&box, box.windowTitle(), tr("The condition or the pass count is not one that can be used."));
    });
    box.exec();
}

void BreakpointsDialog::showAdd()
{
    const bool memory = page() == Memory;
    QDialog box(this);
    box.setObjectName(memory ? "MemoryBreak" : "IOBreak");
    box.setWindowTitle(memory ? tr("Add Memory Breakpoint") : tr("Add Input/Output Breakpoint"));
    auto* form = new QFormLayout(&box);
    auto* type = new QComboBox;
    type->setObjectName("cbType");
    auto* port = new QLineEdit;
    port->setObjectName("edPort");
    port->setMaxLength(4);
    auto* mask = new QLineEdit;
    mask->setObjectName("edMask");
    mask->setMaxLength(4);
    auto* shown = new QLabel;
    shown->setObjectName("lPort");
    auto* input = new QCheckBox(tr("Input"));
    input->setObjectName("ckInput");
    auto* output = new QCheckBox(tr("Output"));
    output->setObjectName("ckOutput");
    auto* address = new QLineEdit;
    address->setObjectName("edAddress");
    address->setMaxLength(4);
    auto* size = new QLineEdit("0001");
    size->setObjectName("edSize");
    size->setMaxLength(4);
    auto* onRead = new QRadioButton(tr("Read"));
    onRead->setObjectName("rbRead");
    auto* onWrite = new QRadioButton(tr("Write"));
    onWrite->setObjectName("rbWrite");
    onWrite->setChecked(true);
    if (memory) {
        form->addRow(tr("Address:"), address);
        form->addRow(tr("Size:"), size);
        auto* kinds = new QHBoxLayout;
        kinds->addWidget(onRead);
        kinds->addWidget(onWrite);
        kinds->addStretch();
        form->addRow(tr("Type:"), kinds);
    } else {
        type->addItems(devices());
        form->addRow(tr("Type:"), type);
        auto* ports = new QHBoxLayout;
        ports->addWidget(port);
        ports->addWidget(new QLabel(tr("Mask:")));
        ports->addWidget(mask);
        ports->addWidget(shown, 1);
        form->addRow(tr("Port:"), ports);
        auto* ways = new QHBoxLayout;
        ways->addWidget(input);
        ways->addWidget(output);
        ways->addStretch();
        form->addRow(QString(), ways);
    }
    auto* condition = new QLineEdit;
    condition->setObjectName("edCondition");
    condition->setMinimumWidth(240);
    auto* passCount = new QLineEdit;
    passCount->setObjectName("edPassCount");
    form->addRow(tr("Condition:"), condition);
    form->addRow(tr("Pass Count:"), passCount);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);

    // A device gives its port and its mask; one's own is typed, with X
    // for the digits that do not matter.
    const auto userPort = [&] { return type->currentIndex() == kDeviceCount - 1; };
    const auto showPort = [&] {
        bool a = false, b = false;
        QString digits = port->text().leftJustified(4, '0');
        digits.replace('X', '0', Qt::CaseInsensitive);
        const unsigned p = digits.toUInt(&a, 16), m = mask->text().toUInt(&b, 16);
        shown->setText(a && b ? tr("Port AND %1 = %2").arg(hex4(m), hex4(p & m)) : QString());
    };
    const auto deviceChosen = [&] {
        const Device& device = kDevices[qBound(0, type->currentIndex(), kDeviceCount - 1)];
        const bool own = userPort();
        for (QWidget* widget : {static_cast<QWidget*>(port), static_cast<QWidget*>(mask), static_cast<QWidget*>(input),
                                static_cast<QWidget*>(output)})
            widget->setEnabled(own);
        if (!own) {
            port->setText(hex4(device.port & device.mask));
            mask->setText(hex4(device.mask));
            input->setChecked(device.input);
            output->setChecked(device.output);
        }
        showPort();
    };
    connect(type, &QComboBox::currentIndexChanged, &box, deviceChosen);
    connect(port, &QLineEdit::textEdited, &box, [&](const QString& text) {
        if (text.contains('X', Qt::CaseInsensitive)) {
            QString digits;
            for (const QChar c : text)
                digits += c.toUpper() == 'X' ? '0' : 'F';
            mask->setText(digits.leftJustified(4, '0'));
        }
        showPort();
    });
    connect(mask, &QLineEdit::textEdited, &box, showPort);
    if (!memory)
        deviceChosen();
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &box, [&] {
        bool number = true, a = true, b = true;
        const int passes = passCount->text().trimmed().isEmpty() ? 0 : passCount->text().toInt(&number);
        bool added = false;
        if (memory) {
            const unsigned at = address->text().toUInt(&a, 16);
            const unsigned length = size->text().toUInt(&b, 16);
            added = number && a && b && addMemoryBreak(at, static_cast<int>(length), onWrite->isChecked(), condition->text(), passes);
        } else {
            QString digits = port->text().leftJustified(4, '0');
            digits.replace('X', '0', Qt::CaseInsensitive);
            const unsigned p = digits.toUInt(&a, 16), m = mask->text().toUInt(&b, 16);
            added = number && a && b
                && addIoBreak(type->currentIndex(), condition->text(), passes, p, m, input->isChecked(), output->isChecked());
        }
        if (added)
            box.accept();
        else
            QMessageBox::warning(&box, box.windowTitle(), tr("This is not a breakpoint that can be set."));
    });
    box.exec();
}

// ---- Timers -------------------------------------------------------------------

TimersDialog::TimersDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("Timers"));
    auto* layout = new QVBoxLayout(this);
    list_ = new QTreeWidget;
    list_->setObjectName("lvTimers");
    list_->setHeaderLabels({tr("ID"), tr("Count"), tr("Last"), tr("Min"), tr("Max"), tr("Average")});
    list_->setRootIsDecorated(false);
    for (int column = 1; column < 6; ++column)
        list_->headerItem()->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
    for (int column = 0; column < 6; ++column)
        list_->setColumnWidth(column, column == 0 ? 60 : 58);
    layout->addWidget(list_);
    auto* buttons = new QHBoxLayout;
    auto* clearAll = new QPushButton(tr("Clear All"));
    clearAll->setObjectName("bClearAll");
    auto* close = new QPushButton(tr("&Close"));
    close->setObjectName("bClose");
    auto* help = new QPushButton(tr("&Help"));
    help->setObjectName("bHelp");
    wireHelp(help);
    buttons->addWidget(clearAll);
    buttons->addStretch();
    buttons->addWidget(close);
    buttons->addWidget(help);
    layout->addLayout(buttons);
    resize(400, 180);
    connect(clearAll, &QPushButton::clicked, this, &TimersDialog::clearAll);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    // The figures move while the machine runs.
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible() && !emulator_->isPaused())
            refresh();
    });
    timer->start(500);
    refresh();
}

void TimersDialog::refresh()
{
    const std::vector<Emulator::TimerState> timers = emulator_->timers();
    while (list_->topLevelItemCount() > static_cast<int>(timers.size()))
        delete list_->topLevelItem(list_->topLevelItemCount() - 1);
    for (size_t i = 0; i < timers.size(); ++i) {
        const Emulator::TimerState& state = timers[i];
        QTreeWidgetItem* item = list_->topLevelItem(static_cast<int>(i));
        if (!item) {
            item = new QTreeWidgetItem(list_);
            for (int column = 1; column < 6; ++column)
                item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
        }
        const QStringList cells = {hex4(static_cast<unsigned>(state.id)),
                                   QString::number(state.count),
                                   QString::number(state.last),
                                   QString::number(state.least),
                                   QString::number(state.most),
                                   state.count ? QString::number(static_cast<double>(state.total) / static_cast<double>(state.count), 'f', 2)
                                               : QString("0.00")};
        for (int column = 0; column < cells.size(); ++column)
            if (item->text(column) != cells[column])
                item->setText(column, cells[column]);
    }
}

QStringList TimersDialog::rows() const
{
    QStringList out;
    for (int r = 0; r < list_->topLevelItemCount(); ++r) {
        QStringList cells;
        for (int column = 0; column < list_->columnCount(); ++column)
            cells << list_->topLevelItem(r)->text(column);
        out << cells.join('|');
    }
    return out;
}

void TimersDialog::clearAll()
{
    emulator_->clearTimers();
    refresh();
}
