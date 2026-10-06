#include "assemblerdialog.h"

#include <array>
#include <memory>

#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QSyntaxHighlighter>
#include <QTabBar>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "emulator.h"

#include "core/disc.h"
#include "core/discfiles.h"
#include "core/files.h"

namespace {

constexpr int kMarginWidth = 18;
const QColor kBreakLine(255, 205, 205);
const QColor kErrorLine(255, 228, 225);
const QColor kBreakDot(214, 0, 0);
const char kFileFilter[] = QT_TRANSLATE_NOOP("AssemblerDialog", "Assembler Files (*.asm *.mxm *.src);;All Files (*)");

// Instructions, registers and numbers in navy, strings in red, comments
// in green, as WinAPE's editor has them.
class Highlighter : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString& text) override
    {
        static const QSet<QString> kRegisters = {"A",  "B",  "C",  "D",  "E",   "H",   "L",   "I",   "R",  "AF", "BC",
                                                 "DE", "HL", "SP", "IX", "IY",  "HX",  "LX",  "HY",  "LY", "NZ", "Z",
                                                 "NC", "PO", "PE", "P",  "IXH", "IXL", "IYH", "IYL", "M"};
        QTextCharFormat word, string, comment;
        word.setForeground(QColor(0, 0, 128));
        string.setForeground(QColor(200, 0, 0));
        comment.setForeground(QColor(0, 128, 0));
        const int n = static_cast<int>(text.size());
        for (int i = 0; i < n;) {
            const QChar c = text[i];
            if (c == ';') {
                setFormat(i, n - i, comment);
                break;
            }
            // The apostrophe of AF' opens no string.
            const bool afAlt = c == '\'' && i >= 2 && text.mid(i - 2, 2).compare(QLatin1String("af"), Qt::CaseInsensitive) == 0;
            if ((c == '"' || c == '\'') && !afAlt) {
                int close = static_cast<int>(text.indexOf(c, i + 1));
                if (close < 0)
                    close = n - 1;
                setFormat(i, close - i + 1, string);
                i = close + 1;
            } else if (c.isLetter() || c == '_' || c == '@') {
                int j = i + 1;
                while (j < n && (text[j].isLetterOrNumber() || text[j] == '_'))
                    ++j;
                const QString name = text.mid(i, j - i);
                const bool dotted = i > 0 && text[i - 1] == '.';
                if (dotted || kRegisters.contains(name.toUpper()) || tuxape::isAssemblerWord(name.toStdString()))
                    setFormat(dotted ? i - 1 : i, dotted ? j - i + 1 : j - i, word);
                i = j;
            } else if (c.isDigit() || c == '#' || c == '&' || c == '%') {
                int j = i + 1;
                while (j < n && text[j].isLetterOrNumber())
                    ++j;
                if (c.isDigit() || j > i + 1)
                    setFormat(i, j - i, word);
                i = j;
            } else {
                ++i;
            }
        }
    }
};

class Margin : public QWidget {
public:
    explicit Margin(CodeEditor* editor)
        : QWidget(editor)
        , editor_(editor)
    {
    }

protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintMargin(event); }
    void mousePressEvent(QMouseEvent* event) override
    {
        const int line = editor_->lineAt(event->pos().y());
        if (line)
            editor_->toggleBreakpoint(line);
    }

private:
    CodeEditor* editor_;
};

QString titleOf(const QString& path)
{
    QString title = QFileInfo(path).completeBaseName();
    if (!title.isEmpty())
        title[0] = title[0].toUpper();
    return title;
}

QTextDocument::FindFlags flagsOf(const FindOptions& options)
{
    QTextDocument::FindFlags flags;
    if (options.caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;
    if (options.wholeWords)
        flags |= QTextDocument::FindWholeWords;
    if (options.backward)
        flags |= QTextDocument::FindBackward;
    return flags;
}

}  // namespace

// ---- The editor ---------------------------------------------------------------

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(8 * fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    margin_ = new Margin(this);
    setViewportMargins(kMarginWidth, 0, 0, 0);
    new Highlighter(document());
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy)
            margin_->scroll(0, dy);
        else
            margin_->update(0, rect.y(), margin_->width(), rect.height());
    });
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        errorLine_ = 0;
        markLines();
    });
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect inside = contentsRect();
    margin_->setGeometry(inside.left(), inside.top(), kMarginWidth, inside.height());
}

void CodeEditor::paintMargin(QPaintEvent* event)
{
    QPainter painter(margin_);
    painter.fillRect(event->rect(), palette().color(QPalette::Button));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBreakDot);
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF box = blockBoundingGeometry(block).translated(contentOffset());
        if (box.top() > event->rect().bottom())
            break;
        if (block.userState() == 1) {
            const qreal size = qMin<qreal>(box.height(), kMarginWidth) - 4;
            painter.drawEllipse(QRectF((kMarginWidth - size) / 2, box.top() + (box.height() - size) / 2, size, size));
        }
    }
}

int CodeEditor::lineAt(int y) const
{
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        const QRectF box = blockBoundingGeometry(block).translated(contentOffset());
        if (y < box.top())
            break;
        if (y < box.bottom())
            return block.blockNumber() + 1;
    }
    return 0;
}

QList<int> CodeEditor::breakpoints() const
{
    QList<int> lines;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
        if (block.userState() == 1)
            lines << block.blockNumber() + 1;
    return lines;
}

void CodeEditor::toggleBreakpoint(int line)
{
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    block.setUserState(block.userState() == 1 ? -1 : 1);
    markLines();
    emit breakpointsChanged();
}

void CodeEditor::clearBreakpoints()
{
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
        block.setUserState(-1);
    markLines();
    emit breakpointsChanged();
}

void CodeEditor::setErrorLine(int line)
{
    errorLine_ = line;
    markLines();
}

void CodeEditor::markLines()
{
    QList<QTextEdit::ExtraSelection> marks;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const bool error = block.blockNumber() + 1 == errorLine_;
        if (!error && block.userState() != 1)
            continue;
        QTextEdit::ExtraSelection mark;
        mark.format.setBackground(error ? kErrorLine : kBreakLine);
        mark.format.setProperty(QTextFormat::FullWidthSelection, true);
        mark.cursor = QTextCursor(block);
        marks << mark;
    }
    setExtraSelections(marks);
    margin_->update();
}

void CodeEditor::gotoLine(int line, int column)
{
    const QTextBlock block = document()->findBlockByNumber(qBound(1, line, blockCount()) - 1);
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, qBound(0, column - 1, block.length() - 1));
    setTextCursor(cursor);
    centerCursor();
}

int CodeEditor::line() const
{
    return textCursor().blockNumber() + 1;
}

int CodeEditor::column() const
{
    return textCursor().positionInBlock() + 1;
}

// ---- The symbols --------------------------------------------------------------

SymbolsDialog::SymbolsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Assembler Symbols"));
    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Filter:")));
    filter_ = new QLineEdit;
    filter_->setObjectName("edFilter");
    top->addWidget(filter_);
    layout->addLayout(top);

    list_ = new QTreeWidget;
    list_->setObjectName("lvSymbols");
    list_->setColumnCount(3);
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(false);
    list_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(list_);

    auto* bottom = new QHBoxLayout;
    auto* where = new QVBoxLayout;
    source_ = new QLabel;
    source_->setObjectName("lSource");
    line_ = new QLabel;
    line_->setObjectName("lLine");
    where->addWidget(source_);
    where->addWidget(line_);
    bottom->addLayout(where, 1);
    auto* close = new QPushButton(tr("&Close"));
    close->setObjectName("bClose");
    bottom->addWidget(close);
    layout->addLayout(bottom);
    resize(430, 320);

    const auto gotoSource = [this] {
        if (const tuxape::AsmSymbol* symbol = selected())
            emit sourceRequested(QString::fromStdString(symbol->file), symbol->line);
    };
    connect(filter_, &QLineEdit::textChanged, this, &SymbolsDialog::fill);
    connect(list_, &QTreeWidget::currentItemChanged, this, &SymbolsDialog::showSelected);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, gotoSource);
    connect(list_, &QWidget::customContextMenuRequested, this, [this, gotoSource](const QPoint& at) {
        QMenu menu(this);
        menu.addAction(tr("Goto &Source"), this, gotoSource)->setEnabled(selected() != nullptr);
        menu.addSeparator();
        menu.addAction(tr("Sa&ve Symbols..."), this, [this] {
            const QString path = QFileDialog::getSaveFileName(this, tr("Save Symbols"), QString(),
                                                              tr("Symbol Files (*.sym);;All Files (*)"));
            if (!path.isEmpty() && !saveSymbols(path))
                QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
        });
        menu.exec(list_->viewport()->mapToGlobal(at));
    });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    showSelected();
}

void SymbolsDialog::setSymbols(const std::vector<tuxape::AsmSymbol>& symbols)
{
    symbols_ = symbols;
    fill();
}

void SymbolsDialog::setFilter(const QString& filter)
{
    filter_->setText(filter);
}

void SymbolsDialog::fill()
{
    list_->clear();
    const QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(filter_->text() + '*'),
                                     QRegularExpression::CaseInsensitiveOption);
    for (size_t i = 0; i < symbols_.size(); ++i) {
        const tuxape::AsmSymbol& symbol = symbols_[i];
        const QString name = QString::fromLatin1(symbol.name.c_str());
        if (!pattern.match(name).hasMatch())
            continue;
        const QString value = QString("%1").arg(symbol.value & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper();
        auto* item = new QTreeWidgetItem(list_, {name, value, symbol.used ? QString(QChar(0x2714)) : QString(QChar(0x2718))});
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        item->setForeground(1, QColor(0, 102, 204));
        item->setForeground(2, QColor(200, 0, 0));
        item->setToolTip(2, symbol.used ? tr("Used by the program") : tr("Not used by the program"));
    }
    if (list_->topLevelItemCount())
        list_->setCurrentItem(list_->topLevelItem(0));
    showSelected();
}

const tuxape::AsmSymbol* SymbolsDialog::selected() const
{
    const QTreeWidgetItem* item = list_->currentItem();
    return item ? &symbols_[static_cast<size_t>(item->data(0, Qt::UserRole).toInt())] : nullptr;
}

void SymbolsDialog::showSelected()
{
    const tuxape::AsmSymbol* symbol = selected();
    source_->setText(tr("Source: %1").arg(symbol ? QDir::toNativeSeparators(QString::fromStdString(symbol->file)) : QString()));
    line_->setText(tr("Line: %1").arg(symbol ? QString::number(symbol->line) : QString()));
}

QStringList SymbolsDialog::listed() const
{
    QStringList rows;
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        rows << list_->topLevelItem(i)->text(0) + ' ' + list_->topLevelItem(i)->text(1);
    return rows;
}

void SymbolsDialog::select(int row)
{
    list_->setCurrentItem(list_->topLevelItem(row));
}

bool SymbolsDialog::saveSymbols(const QString& path) const
{
    // As a source the assembler can read back.
    QByteArray text;
    for (const tuxape::AsmSymbol& symbol : symbols_)
        text += QString("%1 equ #%2\n").arg(QString::fromLatin1(symbol.name.c_str())).arg(symbol.value & 0xFFFF, 4, 16, QLatin1Char('0')).toLatin1();
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

// ---- The window ---------------------------------------------------------------

AssemblerDialog::AssemblerDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("TuxAPE Z80 Assembler"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* bar = new QMenuBar;
    layout->setMenuBar(bar);

    stack_ = new QStackedWidget;
    errors_ = new QListWidget;
    errors_->setObjectName("lbErrors");
    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(stack_);
    splitter->addWidget(errors_);
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({400, 48});
    layout->addWidget(splitter, 1);

    tabs_ = new QTabBar;
    tabs_->setObjectName("TabSet");
    tabs_->setShape(QTabBar::RoundedSouth);
    tabs_->setExpanding(false);
    layout->addWidget(tabs_);

    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(4, 2, 4, 2);
    position_ = new QLabel;
    position_->setObjectName("lPos");
    position_->setMinimumWidth(90);
    position_->setAlignment(Qt::AlignCenter);
    position_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    status_ = new QLabel;
    status_->setObjectName("lStatus");
    status_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    statusRow->addWidget(position_);
    statusRow->addWidget(status_, 1);
    layout->addLayout(statusRow);
    resize(640, 520);

    buildMenus(bar);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < fileCount()) {
            stack_->setCurrentWidget(files_[index].editor);
            files_[index].editor->setFocus();
        }
        updateStatus();
    });
    connect(errors_, &QListWidget::itemDoubleClicked, this, [this] { gotoError(errors_->currentRow()); });
    newFile();
}

void AssemblerDialog::buildMenus(QMenuBar* bar)
{
    const auto add = [this](QMenu* menu, const QString& text, const QKeySequence& keys, auto&& slot) {
        QAction* action = menu->addAction(text, this, std::forward<decltype(slot)>(slot));
        action->setShortcut(keys);
        return action;
    };
    // What is not there yet stays grey, as in the main window.
    const auto later = [](QMenu* menu, const QString& text, const QKeySequence& keys = {}) {
        QAction* action = menu->addAction(text);
        action->setShortcut(keys);
        action->setEnabled(false);
    };
    const auto edit = [this](void (QPlainTextEdit::*slot)()) {
        return [this, slot] {
            if (CodeEditor* current = editor())
                (current->*slot)();
        };
    };

    QMenu* file = bar->addMenu(tr("&File"));
    add(file, tr("&New"), {}, [this] { newFile(); });
    add(file, tr("&Open"), {}, [this] {
        const QString folder = currentFile() >= 0 ? QFileInfo(filePath(currentFile())).absolutePath() : QString();
        for (const QString& path : QFileDialog::getOpenFileNames(this, tr("Open"), folder, tr(kFileFilter)))
            if (!openFile(path))
                QMessageBox::warning(this, windowTitle(), tr("Cannot read %1.").arg(QDir::toNativeSeparators(path)));
    });
    add(file, tr("&Save"), Qt::CTRL | Qt::Key_S, [this] { saveFile(currentFile()); });
    add(file, tr("Save &As"), {}, [this] {
        const int index = currentFile();
        if (index >= 0)
            saveFileAs(index, QFileDialog::getSaveFileName(this, tr("Save As"), filePath(index), tr(kFileFilter)));
    });
    add(file, tr("Save A&ll"), Qt::SHIFT | Qt::CTRL | Qt::Key_S, [this] { saveAll(); });
    add(file, tr("&Close"), {}, [this] { closeFile(currentFile()); });
    file->addSeparator();
    later(file, tr("Read &BASIC"));
    file->addSeparator();
    add(file, tr("E&xit"), {}, [this] { close(); });

    QMenu* editMenu = bar->addMenu(tr("&Edit"));
    add(editMenu, tr("&Undo"), Qt::CTRL | Qt::Key_Z, edit(&QPlainTextEdit::undo));
    add(editMenu, tr("R&edo"), Qt::SHIFT | Qt::CTRL | Qt::Key_Z, edit(&QPlainTextEdit::redo));
    editMenu->addSeparator();
    add(editMenu, tr("Cu&t"), Qt::CTRL | Qt::Key_X, edit(&QPlainTextEdit::cut));
    add(editMenu, tr("&Copy"), Qt::CTRL | Qt::Key_C, edit(&QPlainTextEdit::copy));
    add(editMenu, tr("&Paste"), Qt::CTRL | Qt::Key_V, edit(&QPlainTextEdit::paste));
    add(editMenu, tr("Select &All"), Qt::CTRL | Qt::Key_A, edit(&QPlainTextEdit::selectAll));
    editMenu->addSeparator();
    add(editMenu, tr("&Find"), Qt::CTRL | Qt::Key_F, [this] { showFind(false); });
    add(editMenu, tr("Fin&d Again"), Qt::Key_F3, [this] { findAgain(); });
    add(editMenu, tr("&Replace"), Qt::CTRL | Qt::Key_R, [this] { showFind(true); });
    editMenu->addSeparator();
    add(editMenu, tr("&Goto Line number"), Qt::CTRL | Qt::Key_G, [this] {
        CodeEditor* current = editor();
        if (!current)
            return;
        bool ok = false;
        const int line = QInputDialog::getInt(this, tr("Goto Line"), tr("Line number:"), current->line(), 1,
                                              current->blockCount(), 1, &ok);
        if (ok)
            current->gotoLine(line);
    });
    editMenu->addSeparator();
    add(editMenu, tr("C&lear all breakpoints"), {}, [this] {
        for (const Source& source : files_)
            source.editor->clearBreakpoints();
    });
    add(editMenu, tr("Goto &next breakpoint"), {}, [this] { gotoNextBreakpoint(); });

    QMenu* assembleMenu = bar->addMenu(tr("&Assemble"));
    add(assembleMenu, tr("&Assemble"), Qt::CTRL | Qt::Key_F9, [this] { assemble(); });
    add(assembleMenu, tr("&Run"), Qt::Key_F9, [this] { run(); });
    assembleMenu->addSeparator();
    later(assembleMenu, tr("&Information"));
    add(assembleMenu, tr("&Symbols"), {}, [this] { showSymbols(); });
    assembleMenu->addSeparator();
    add(assembleMenu, tr("&Options"), {}, [this] { showOptions(); });

    QMenu* help = bar->addMenu(tr("&Help"));
    later(help, tr("Contents"));
    help->addSeparator();
    later(help, tr("Assembler"), Qt::Key_F1);
}

// ---- Files --------------------------------------------------------------------

int AssemblerDialog::currentFile() const
{
    return tabs_->currentIndex();
}

void AssemblerDialog::setCurrentFile(int index)
{
    tabs_->setCurrentIndex(index);
}

CodeEditor* AssemblerDialog::editor(int index) const
{
    if (index < 0)
        index = currentFile();
    return index >= 0 && index < fileCount() ? files_[index].editor : nullptr;
}

QString AssemblerDialog::filePath(int index) const
{
    return index >= 0 && index < fileCount() ? files_[index].path : QString();
}

QString AssemblerDialog::fileTitle(int index) const
{
    return index >= 0 && index < fileCount() ? files_[index].title : QString();
}

QString AssemblerDialog::tabText(int index) const
{
    return tabs_->tabText(index);
}

// What the assembler knows a file by: its path, or its tab's name until
// it has one.
QString AssemblerDialog::sourceName(int index) const
{
    return filePath(index).isEmpty() ? fileTitle(index) : filePath(index);
}

int AssemblerDialog::newFile()
{
    Source source;
    source.editor = new CodeEditor;
    ++untitled_;
    source.title = untitled_ == 1 ? tr("Untitled") : tr("Untitled%1").arg(untitled_);
#ifdef Q_OS_WIN
    source.crlf = true;
#endif
    CodeEditor* edit = source.editor;
    files_ << source;
    stack_->addWidget(edit);
    const auto indexOf = [this, edit] {
        for (int i = 0; i < fileCount(); ++i)
            if (files_[i].editor == edit)
                return i;
        return -1;
    };
    connect(edit->document(), &QTextDocument::modificationChanged, this, [this, indexOf] {
        updateTab(indexOf());
        updateStatus();
    });
    connect(edit, &QPlainTextEdit::cursorPositionChanged, this, &AssemblerDialog::updateStatus);
    connect(edit, &CodeEditor::breakpointsChanged, this, [this] {
        if (result_.ok() && !result_.addresses.empty())
            applyBreakpoints();
    });
    edit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(edit, &QWidget::customContextMenuRequested, this, [this, edit](const QPoint& at) {
        QMenu menu(this);
        menu.addAction(tr("Cut"), edit, &QPlainTextEdit::cut);
        menu.addAction(tr("Copy"), edit, &QPlainTextEdit::copy);
        menu.addAction(tr("Paste"), edit, &QPlainTextEdit::paste);
        menu.addAction(tr("Select All"), edit, &QPlainTextEdit::selectAll);
        menu.addSeparator();
        const int line = edit->cursorForPosition(at).blockNumber() + 1;
        menu.addAction(tr("&Toggle Breakpoint"), edit, [edit, line] { edit->toggleBreakpoint(line); });
        menu.addAction(tr("&Breakpoint Properties..."))->setEnabled(false);
        menu.exec(edit->viewport()->mapToGlobal(at));
    });
    const int index = tabs_->addTab(source.title);
    tabs_->setCurrentIndex(index);
    return index;
}

void AssemblerDialog::updateTab(int index)
{
    if (index < 0 || index >= fileCount())
        return;
    // A star beside the name of a file with changes not saved.
    tabs_->setTabText(index, files_[index].title + (files_[index].editor->document()->isModified() ? "*" : ""));
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(files_[index].path));
}

void AssemblerDialog::updateStatus()
{
    const CodeEditor* current = editor();
    position_->setText(current ? QString("%1 : %2").arg(current->line()).arg(current->column()) : QString());
    status_->setText(current && current->document()->isModified() ? tr("Modified") : QString());
}

bool AssemblerDialog::openFile(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < fileCount(); ++i) {
        if (files_[i].path == absolute) {
            setCurrentFile(i);
            return true;
        }
    }
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    // An empty file without a name gives its tab up.
    int index = currentFile();
    const bool spare = index >= 0 && files_[index].path.isEmpty() && files_[index].editor->document()->isEmpty()
        && !files_[index].editor->document()->isModified();
    if (!spare)
        index = newFile();
    Source& source = files_[index];
    source.path = absolute;
    source.title = titleOf(absolute);
    source.crlf = data.contains("\r\n");
    QString text = QString::fromLatin1(data);
    text.replace("\r\n", "\n");
    source.editor->setPlainText(text);
    source.editor->document()->setModified(false);
    updateTab(index);
    setCurrentFile(index);
    updateStatus();
    return true;
}

bool AssemblerDialog::saveFile(int index)
{
    if (index < 0 || index >= fileCount())
        return false;
    QString path = files_[index].path;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, tr("Save As"), QString(), tr(kFileFilter));
    return saveFileAs(index, path);
}

bool AssemblerDialog::saveFileAs(int index, const QString& path)
{
    if (index < 0 || index >= fileCount() || path.isEmpty())
        return false;
    Source& source = files_[index];
    QString text = source.editor->toPlainText();
    if (source.crlf)
        text.replace("\n", "\r\n");
    const QByteArray data = text.toLatin1();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    source.path = QFileInfo(path).absoluteFilePath();
    source.title = titleOf(source.path);
    source.editor->document()->setModified(false);
    updateTab(index);
    updateStatus();
    return true;
}

bool AssemblerDialog::saveAll()
{
    bool all = true;
    for (int i = 0; i < fileCount(); ++i)
        if (files_[i].editor->document()->isModified())
            all = saveFile(i) && all;
    return all;
}

bool AssemblerDialog::saveBeforeLeaving()
{
    for (int i = 0; i < fileCount(); ++i) {
        if (!files_[i].editor->document()->isModified())
            continue;
        setCurrentFile(i);
        const auto answer = QMessageBox::question(this, windowTitle(), tr("Save changes to %1?").arg(files_[i].title),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Yes && !saveFile(i)))
            return false;
    }
    return true;
}

bool AssemblerDialog::closeFile(int index)
{
    if (index < 0 || index >= fileCount())
        return false;
    if (files_[index].editor->document()->isModified()) {
        setCurrentFile(index);
        const auto answer = QMessageBox::question(this, windowTitle(), tr("Save changes to %1?").arg(files_[index].title),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Yes && !saveFile(index)))
            return false;
    }
    CodeEditor* gone = files_[index].editor;
    files_.removeAt(index);
    tabs_->removeTab(index);
    stack_->removeWidget(gone);
    gone->deleteLater();
    if (files_.isEmpty())
        newFile();
    else
        stack_->setCurrentWidget(files_[currentFile()].editor);
    updateStatus();
    return true;
}

// ---- Assembling ---------------------------------------------------------------

// A file `read` or `incbin` names: beside the file that names it, then in
// the folders of the library path.
QString AssemblerDialog::locate(const QString& name, const QString& from) const
{
    QString wanted = name;
    wanted.replace('\\', '/');
    QStringList folders;
    if (QFileInfo(from).isAbsolute())
        folders << QFileInfo(from).absolutePath();
    for (const QString& folder : libraryPath_.split(';', Qt::SkipEmptyParts))
        folders << folder.trimmed();
    folders << QDir::currentPath();
    for (const QString& folder : folders) {
        const QFileInfo candidate(QDir(folder), wanted);
        if (candidate.isFile())
            return candidate.absoluteFilePath();
        // Sources written on Windows do not mind the case of a name.
        const QStringList alike = candidate.dir().entryList({candidate.fileName()}, QDir::Files);
        if (!alike.isEmpty())
            return candidate.dir().absoluteFilePath(alike.first());
    }
    return {};
}

bool AssemblerDialog::assemble()
{
    const int index = currentFile();
    if (index < 0)
        return false;
    const QString name = sourceName(index);
    // memory() and checksum() read the machine as it is now.
    auto memory = std::make_shared<std::array<uint8_t, 0x10000>>();
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        for (int address = 0; address < 0x10000; ++address)
            (*memory)[static_cast<size_t>(address)] = cpc.memory().read(static_cast<uint16_t>(address));
    });
    tuxape::AsmHost host;
    host.memory = [memory](uint16_t address) { return (*memory)[address]; };
    host.source = [this](std::string& wanted, const std::string& from) -> std::optional<std::string> {
        const QString path = locate(QString::fromStdString(wanted), QString::fromStdString(from));
        if (path.isEmpty())
            return std::nullopt;
        wanted = path.toStdString();
        // A file open here is taken as it stands in its tab.
        for (const Source& source : files_)
            if (source.path == path)
                return source.editor->toPlainText().toLatin1().toStdString();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return std::nullopt;
        return file.readAll().toStdString();
    };
    host.binary = [this](const std::string& wanted, const std::string& from) -> std::optional<std::vector<uint8_t>> {
        const QString path = locate(QString::fromStdString(wanted), QString::fromStdString(from));
        if (path.isEmpty())
            return std::nullopt;
        return tuxape::readFile(path.toStdString());
    };
    result_ = tuxape::assemble(files_[index].editor->toPlainText().toLatin1().toStdString(), name.toStdString(), host);

    listed_ = result_.errors;
    if (result_.ok()) {
        const QString folder = filePath(index).isEmpty() ? QDir::currentPath() : QFileInfo(filePath(index)).absolutePath();
        applyToMachine(folder);
    }
    errors_->clear();
    for (const tuxape::AsmError& error : listed_) {
        const QString where = error.line ? tr("%1: Line %2 - ").arg(QDir::toNativeSeparators(QString::fromStdString(error.file))).arg(error.line)
                                         : QString();
        errors_->addItem(where + QString::fromStdString(error.message));
    }
    // The first error shows in its source, if that is open.
    for (int i = 0; i < fileCount(); ++i) {
        const bool first = !listed_.empty() && listed_[0].line && sourceName(i).toStdString() == listed_[0].file;
        files_[i].editor->setErrorLine(first ? listed_[0].line : 0);
    }
    if (!listed_.empty())
        errors_->setCurrentRow(0);
    const bool ok = listed_.empty();
    if (ok)
        applyBreakpoints();
    if (symbols_)
        symbols_->setSymbols(result_.symbols);
    if (!ok || !hideOutput_)
        showOutput(name);
    return ok;
}

void AssemblerDialog::applyToMachine(const QString& folder)
{
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        tuxape::Memory& memory = cpc.memory();
        const uint8_t bank = memory.ramBank();
        for (const tuxape::AsmBlock& block : result_.memory) {
            if (block.bank >= 0)
                memory.selectRamBank(static_cast<uint8_t>(block.bank), 0x7F);
            for (size_t i = 0; i < block.data.size(); ++i)
                memory.write(static_cast<uint16_t>(block.address + i), block.data[i]);
            if (block.bank >= 0)
                memory.selectRamBank(bank, 0x7F);
        }
    });
    const auto failed = [this](const QString& message) {
        if (!message.isEmpty())
            listed_.push_back({std::string(), 0, message.toStdString()});
    };
    for (const tuxape::AsmFile& file : result_.files)
        failed(store(QString::fromStdString(file.name), file.direct, file.data, file.loadAddress, file.execAddress, folder));
    for (const tuxape::AsmSave& save : result_.saves) {
        std::vector<uint8_t> data;
        emulator_->withMachine([&](tuxape::Cpc& cpc) {
            for (const auto& [address, size] : save.regions)
                for (int n = 0; n < size; ++n)
                    data.push_back(cpc.memory().readRam(static_cast<uint16_t>(address + n)));
        });
        const uint16_t load = save.regions.empty() ? uint16_t(0) : save.regions.front().first;
        failed(store(QString::fromStdString(save.name), save.direct, data, load, save.execAddress, folder));
    }
}

// Writes a file the program asked for: on the host, or with AMSDOS's
// header on the disc in a drive. An error message, or nothing.
QString AssemblerDialog::store(const QString& name, bool direct, const std::vector<uint8_t>& data, uint16_t load,
                               int exec, const QString& folder)
{
    if (!direct) {
        QString wanted = name;
        wanted.replace('\\', '/');
        const QString path = QFileInfo(QDir(folder), wanted).absoluteFilePath();
        return tuxape::writeFile(path.toStdString(), data) ? QString()
                                                           : tr("Cannot write %1.").arg(QDir::toNativeSeparators(path));
    }
    QString file = name;
    int drive = 0;
    if (file.size() > 2 && file[1] == ':') {
        drive = file[0].toUpper() == 'B' ? 1 : 0;
        file = file.mid(2);
    }
    file = file.toUpper();
    tuxape::AmsdosHeader header;
    header.loadAddress = load;
    header.entryAddress = exec < 0 ? uint16_t(0) : static_cast<uint16_t>(exec);
    header.length = static_cast<int>(data.size());
    std::vector<uint8_t> bytes = tuxape::makeAmsdosHeader(file.toStdString(), header);
    bytes.insert(bytes.end(), data.begin(), data.end());
    const QString driveName = QChar('A' + drive);
    return emulator_->withMachine([&](tuxape::Cpc& cpc) {
        tuxape::Disc* disc = cpc.fdc().drive(drive).disc.get();
        if (!disc)
            return tr("There is no disc in drive %1: for %2.").arg(driveName, file);
        if (disc->writeProtected)
            return tr("The disc in drive %1: cannot be written to.").arg(driveName);
        tuxape::DiscFiles files(*disc);
        if (!files.valid())
            return tr("The format of the disc in drive %1: is not one files can be put on.").arg(driveName);
        return files.write(file.toStdString(), bytes) ? QString()
                                                      : tr("Cannot put %1 on the disc in drive %2:.").arg(file, driveName);
    });
}

// Breakpoints set on source lines become breakpoints at the addresses the
// lines were assembled to; a line without code stands for the next with.
void AssemblerDialog::applyBreakpoints()
{
    std::set<uint16_t> all = emulator_->breakpoints();
    for (const uint16_t address : ownBreakpoints_)
        all.erase(address);
    ownBreakpoints_.clear();
    for (int i = 0; i < fileCount(); ++i) {
        const std::string name = sourceName(i).toStdString();
        for (const int line : files_[i].editor->breakpoints()) {
            for (const tuxape::AsmLine& at : result_.addresses) {
                if (at.file == name && at.line >= line) {
                    ownBreakpoints_.insert(at.address);
                    break;
                }
            }
        }
    }
    if (result_.breakpoint)
        ownBreakpoints_.insert(*result_.breakpoint);
    all.insert(ownBreakpoints_.begin(), ownBreakpoints_.end());
    emulator_->setBreakpoints(all);
    emit breakpointsChanged();
}

void AssemblerDialog::showOutput(const QString& name)
{
    QDialog box(this);
    box.setObjectName("Assembling");
    box.setWindowTitle(tr("Assembling"));
    auto* layout = new QVBoxLayout(&box);
    const auto panel = [](const QString& text, const char* objectName) {
        auto* label = new QLabel(text);
        label->setObjectName(objectName);
        label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        label->setMargin(3);
        return label;
    };
    layout->addWidget(panel(tr("Assembling : %1").arg(QDir::toNativeSeparators(name)), "lAssembling"));
    layout->addWidget(panel(tr("Output To : %1").arg(result_.files.empty() ? tr("Direct to Emulator")
                                                                            : QString::fromStdString(result_.files.back().name)),
                            "lOutputTo"));
    auto* counts = new QHBoxLayout;
    counts->addWidget(panel(tr("Pass : %1").arg(2), "lPass"));
    counts->addWidget(panel(tr("Current Line : %1").arg(result_.lines), "lCurrentLine"));
    counts->addWidget(panel(tr("Total Lines : %1").arg(result_.lines), "lTotalLines"));
    counts->addWidget(panel(tr("Errors : %1").arg(listed_.size()), "lErrors"));
    layout->addLayout(counts);
    auto* output = new QPlainTextEdit;
    output->setObjectName("mOutput");
    output->setReadOnly(true);
    output->setLineWrapMode(QPlainTextEdit::NoWrap);
    output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QString text = tr("TuxAPE Z80 Assembler") + "\n\n";
    for (const std::string& line : result_.output)
        text += QString::fromLatin1(line.c_str()) + '\n';
    for (int row = 0; row < errors_->count(); ++row)
        text += errors_->item(row)->text() + '\n';
    output->setPlainText(text);
    layout->addWidget(output, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("bOK");
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    layout->addWidget(buttons);
    box.resize(560, 340);
    box.exec();
}

bool AssemblerDialog::run()
{
    if (!assemble())
        return false;
    if (!result_.run) {
        errors_->addItem(tr("Nothing to run: the program has no run directive"));
        listed_.push_back({std::string(), 0, std::string()});
        return false;
    }
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        auto& z80 = cpc.cpu();
        if (pushPc_) {
            // As a CALL would: a RET at the end of the program goes back
            // to where the machine was.
            z80.sp = static_cast<uint16_t>(z80.sp - 2);
            cpc.memory().write(z80.sp, static_cast<uint8_t>(z80.pc));
            cpc.memory().write(static_cast<uint16_t>(z80.sp + 1), static_cast<uint8_t>(z80.pc >> 8));
        }
        z80.pc = *result_.run;
        z80.halted = false;
    });
    emit runRequested();
    return true;
}

QStringList AssemblerDialog::errorTexts() const
{
    QStringList texts;
    for (int row = 0; row < errors_->count(); ++row)
        texts << errors_->item(row)->text();
    return texts;
}

void AssemblerDialog::gotoSource(const QString& file, int line)
{
    int index = -1;
    for (int i = 0; i < fileCount() && index < 0; ++i)
        if (sourceName(i) == file)
            index = i;
    if (index < 0 && QFileInfo(file).isFile() && openFile(file))
        index = currentFile();
    if (index < 0)
        return;
    setCurrentFile(index);
    files_[index].editor->gotoLine(line);
    files_[index].editor->setFocus();
}

void AssemblerDialog::gotoError(int row)
{
    if (row < 0 || row >= static_cast<int>(listed_.size()) || !listed_[static_cast<size_t>(row)].line)
        return;
    const tuxape::AsmError& error = listed_[static_cast<size_t>(row)];
    gotoSource(QString::fromStdString(error.file), error.line);
    if (CodeEditor* current = editor(); current && sourceName(currentFile()).toStdString() == error.file)
        current->setErrorLine(error.line);
}

SymbolsDialog* AssemblerDialog::showSymbols()
{
    if (!symbols_) {
        symbols_ = new SymbolsDialog(this);
        connect(symbols_, &SymbolsDialog::sourceRequested, this, &AssemblerDialog::gotoSource);
    }
    symbols_->setSymbols(result_.symbols);
    symbols_->show();
    symbols_->raise();
    return symbols_;
}

// ---- Editing ------------------------------------------------------------------

bool AssemblerDialog::find(const FindOptions& options)
{
    CodeEditor* current = editor();
    if (!current || options.text.isEmpty())
        return false;
    lastFind_ = options;
    QTextCursor from = current->textCursor();
    int low = 0, high = current->document()->characterCount() - 1;
    if (options.selectedOnly && from.hasSelection()) {
        low = from.selectionStart();
        high = from.selectionEnd();
    }
    if (options.entireScope || options.selectedOnly) {
        from = QTextCursor(current->document());
        from.setPosition(options.backward ? high : low);
    }
    const QTextCursor found = current->document()->find(options.text, from, flagsOf(options));
    if (found.isNull() || found.selectionStart() < low || found.selectionEnd() > high)
        return false;
    current->setTextCursor(found);
    return true;
}

bool AssemblerDialog::findAgain()
{
    FindOptions options = lastFind_;
    options.entireScope = false;
    options.selectedOnly = false;
    return find(options);
}

bool AssemblerDialog::replaceNext(const FindOptions& options)
{
    if (!find(options))
        return false;
    if (options.prompt
        && QMessageBox::question(this, tr("Confirm"), tr("Replace this occurrence of '%1'?").arg(options.text))
            != QMessageBox::Yes)
        return true;
    editor()->textCursor().insertText(options.replacement);
    return true;
}

int AssemblerDialog::replaceAll(const FindOptions& options)
{
    CodeEditor* current = editor();
    if (!current || options.text.isEmpty())
        return 0;
    QTextDocument* document = current->document();
    int position = 0, high = document->characterCount() - 1;
    if (options.selectedOnly && current->textCursor().hasSelection()) {
        position = current->textCursor().selectionStart();
        high = current->textCursor().selectionEnd();
    }
    FindOptions forward = options;
    forward.backward = false;
    bool ask = options.prompt;
    int count = 0;
    QTextCursor edit(document);
    edit.beginEditBlock();
    for (;;) {
        QTextCursor found = document->find(options.text, position, flagsOf(forward));
        if (found.isNull() || found.selectionEnd() > high)
            break;
        position = found.selectionEnd();
        if (ask) {
            current->setTextCursor(found);
            const auto answer = QMessageBox::question(this, tr("Confirm"), tr("Replace this occurrence of '%1'?").arg(options.text),
                                                      QMessageBox::Yes | QMessageBox::No | QMessageBox::YesToAll | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel)
                break;
            if (answer == QMessageBox::No)
                continue;
            ask = answer != QMessageBox::YesToAll;
        }
        const int before = found.selectionEnd() - found.selectionStart();
        found.insertText(options.replacement);
        position = found.position();
        high += static_cast<int>(options.replacement.size()) - before;
        ++count;
    }
    edit.endEditBlock();
    return count;
}

bool AssemblerDialog::showFind(bool replace)
{
    CodeEditor* current = editor();
    if (!current)
        return false;
    QDialog box(this);
    box.setObjectName(replace ? "ReplaceDialog" : "FindDialog");
    box.setWindowTitle(replace ? tr("Replace Text") : tr("Find Text"));
    auto* layout = new QVBoxLayout(&box);
    auto* fields = new QGridLayout;
    auto* text = new QLineEdit(current->textCursor().hasSelection() ? current->textCursor().selectedText() : lastFind_.text);
    text->setObjectName("edFind");
    auto* with = new QLineEdit(lastFind_.replacement);
    with->setObjectName("edReplace");
    fields->addWidget(new QLabel(tr("&Text to find:")), 0, 0);
    fields->addWidget(text, 0, 1);
    if (replace) {
        fields->addWidget(new QLabel(tr("&Replace with:")), 1, 0);
        fields->addWidget(with, 1, 1);
    }
    layout->addLayout(fields);

    const auto group = [](const QString& title, std::initializer_list<QWidget*> widgets) {
        auto* box = new QGroupBox(title);
        auto* inside = new QVBoxLayout(box);
        for (QWidget* widget : widgets)
            inside->addWidget(widget);
        inside->addStretch();
        return box;
    };
    const auto named = [](auto* widget, const char* objectName, bool checked) {
        widget->setObjectName(objectName);
        widget->setChecked(checked);
        return widget;
    };
    auto* caseSensitive = named(new QCheckBox(tr("&Case sensitive")), "ckCase", lastFind_.caseSensitive);
    auto* wholeWords = named(new QCheckBox(tr("&Whole words only")), "ckWholeWords", lastFind_.wholeWords);
    auto* prompt = named(new QCheckBox(tr("&Prompt on replace")), "ckPrompt", lastFind_.prompt);
    prompt->setVisible(replace);
    auto* forward = named(new QRadioButton(tr("Forwar&d")), "rbForward", !lastFind_.backward);
    auto* backward = named(new QRadioButton(tr("&Backward")), "rbBackward", lastFind_.backward);
    auto* global = named(new QRadioButton(tr("&Global")), "rbGlobal", !lastFind_.selectedOnly);
    auto* selectedText = named(new QRadioButton(tr("&Selected text")), "rbSelected", lastFind_.selectedOnly);
    auto* fromCursor = named(new QRadioButton(tr("&From cursor")), "rbFromCursor", !lastFind_.entireScope);
    auto* entireScope = named(new QRadioButton(tr("&Entire scope")), "rbEntireScope", lastFind_.entireScope);
    auto* choices = new QGridLayout;
    choices->addWidget(group(tr("Options"), {caseSensitive, wholeWords, prompt}), 0, 0);
    choices->addWidget(group(tr("Direction"), {forward, backward}), 0, 1);
    choices->addWidget(group(tr("Scope"), {global, selectedText}), 1, 0);
    choices->addWidget(group(tr("Origin"), {fromCursor, entireScope}), 1, 1);
    layout->addLayout(choices);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("bOK");
    QPushButton* all = replace ? buttons->addButton(tr("Replace &All"), QDialogButtonBox::ActionRole) : nullptr;
    if (all)
        all->setObjectName("bReplaceAll");
    layout->addWidget(buttons);
    bool everywhere = false;
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    if (all) {
        connect(all, &QPushButton::clicked, &box, [&] {
            everywhere = true;
            box.accept();
        });
    }
    if (box.exec() != QDialog::Accepted)
        return false;

    FindOptions options;
    options.text = text->text();
    options.replacement = with->text();
    options.caseSensitive = caseSensitive->isChecked();
    options.wholeWords = wholeWords->isChecked();
    options.prompt = prompt->isChecked();
    options.backward = backward->isChecked();
    options.selectedOnly = selectedText->isChecked();
    options.entireScope = entireScope->isChecked();
    lastFind_ = options;
    const bool found = everywhere ? replaceAll(options) > 0 : replace ? replaceNext(options) : find(options);
    if (!found)
        QMessageBox::information(this, windowTitle(), tr("Search string '%1' not found.").arg(options.text));
    return found;
}

void AssemblerDialog::gotoNextBreakpoint()
{
    CodeEditor* current = editor();
    if (!current)
        return;
    const QList<int> lines = current->breakpoints();
    if (lines.isEmpty())
        return;
    int next = lines.first();  // round to the top after the last one
    for (const int line : lines) {
        if (line > current->line()) {
            next = line;
            break;
        }
    }
    current->gotoLine(next);
}

// ---- Options ------------------------------------------------------------------

void AssemblerDialog::setOptions(const QString& libraryPath, bool pushPcOnRun, bool hideOutput)
{
    libraryPath_ = libraryPath;
    pushPc_ = pushPcOnRun;
    hideOutput_ = hideOutput;
}

void AssemblerDialog::showOptions()
{
    QDialog box(this);
    box.setObjectName("AsmOptions");
    box.setWindowTitle(tr("Assembler Options"));
    auto* layout = new QVBoxLayout(&box);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Library Path")));
    auto* path = new QLineEdit(libraryPath_);
    path->setObjectName("edLibraryPath");
    path->setMinimumWidth(320);
    path->setToolTip(tr("Folders where files named by read and incbin are looked for, between semicolons"));
    row->addWidget(path);
    layout->addLayout(row);
    auto* pushPc = new QCheckBox(tr("Push PC to stack when running program"));
    pushPc->setObjectName("ckPushPC");
    pushPc->setChecked(pushPc_);
    layout->addWidget(pushPc);
    auto* hide = new QCheckBox(tr("Automatically hide assembler output on success"));
    hide->setObjectName("ckHideOutput");
    hide->setChecked(hideOutput_);
    layout->addWidget(hide);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("bOK");
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    layout->addWidget(buttons);
    if (box.exec() != QDialog::Accepted)
        return;
    setOptions(path->text(), pushPc->isChecked(), hide->isChecked());
    emit optionsChanged();
}
