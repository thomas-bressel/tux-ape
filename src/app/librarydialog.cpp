#include "librarydialog.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/files.h"
#include "core/zip.h"

namespace {

bool isProgram(const QString& name)
{
    return name.endsWith(".dsk", Qt::CaseInsensitive) || name.endsWith(".cdt", Qt::CaseInsensitive)
           || name.endsWith(".cpr", Qt::CaseInsensitive) || name.endsWith(".sna", Qt::CaseInsensitive);
}

constexpr int kReleaseColumn = 3;
constexpr int kAiColumn = 4;

// The kind of release a note of a file name tells, or nothing. A note
// that is only that is not one of the other notes; TOSEC's "cr XYZ", which
// also names who did it, stays among them.
QString releaseOf(const QString& note, bool* whole)
{
    const QString text = note.toLower();
    static const struct {
        const char* release;
        QStringList words;
    } kKinds[] = {{"Original", {"original"}},
                  {"Crack", {"crack", "cracked", "cr"}},
                  {"Hack", {"hack", "hacked", "h"}},
                  {"File", {"file"}}};
    for (const auto& kind : kKinds) {
        for (const QString& word : kind.words) {
            *whole = text == word;
            if (*whole || text.startsWith(word + ' '))
                return kind.release;
        }
    }
    return {};
}

LibraryEntry::Kind kindOf(const QString& name)
{
    return name.endsWith(".sna", Qt::CaseInsensitive)   ? LibraryEntry::Snapshot
           : name.endsWith(".cdt", Qt::CaseInsensitive) ? LibraryEntry::Tape
           : name.endsWith(".cpr", Qt::CaseInsensitive) ? LibraryEntry::Cartridge
                                                        : LibraryEntry::Disc;
}

// A name out of an archive: UTF-8 in those of today, the PC's old
// character set in those of yesterday, of which the accents are let go.
QString memberName(const std::string& name)
{
    const QString text = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    return text.contains(QChar::ReplacementCharacter) ? QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()))
                                                      : text;
}

}  // namespace

LibraryEntry libraryEntry(const QString& path)
{
    LibraryEntry entry;
    entry.path = path;
    entry.kind = kindOf(path);
    const QString name = QFileInfo(path).completeBaseName().replace('_', ' ');

    // The title runs up to the first bracket.
    static const QRegularExpression bracket("[(\\[]");
    const qsizetype notes = name.indexOf(bracket);
    entry.title = (notes > 0 ? name.left(notes) : name).simplified();
    // "Last Ninja, The" is "The Last Ninja".
    static const QRegularExpression article("^(.*), (The|A|An|Le|La|Les|L'|El|Der|Die|Das)$",
                                            QRegularExpression::CaseInsensitiveOption);
    if (const auto match = article.match(entry.title); match.hasMatch())
        entry.title = match.captured(2) + (match.captured(2).endsWith('\'') ? "" : " ") + match.captured(1);

    if (notes > 0) {
        static const QRegularExpression note("[(\\[]([^)\\]]*)[)\\]]");
        static const QRegularExpression year("^((19|20)[0-9]{2})(-.*)?$");
        QStringList details;
        for (auto it = note.globalMatch(name, notes); it.hasNext();) {
            const QString text = it.next().captured(1).simplified();
            bool whole = false;
            const QString release = releaseOf(text, &whole);
            if (!release.isEmpty() && entry.release.isEmpty())
                entry.release = release;
            if (!release.isEmpty() && whole)
                continue;
            if (text.compare("AI", Qt::CaseInsensitive) == 0)
                entry.ai = true;
            else if (const auto match = year.match(text); match.hasMatch() && entry.year.isEmpty())
                entry.year = match.captured(1);
            else if (!text.isEmpty() && text != "-")
                details << text;
        }
        entry.details = details.join(", ");
    }
    return entry;
}

QList<LibraryEntry> scanLibrary(const QStringList& folders)
{
    QList<LibraryEntry> entries;
    QSet<QString> seen;
    for (const QString& folder : folders) {
        QDirIterator it(folder, {"*.dsk", "*.cdt", "*.cpr", "*.sna", "*.zip"}, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString path = QDir::cleanPath(it.next());
            // A folder named twice, or inside another, gives each file once.
            if (seen.contains(path))
                continue;
            seen.insert(path);
            if (!path.endsWith(".zip", Qt::CaseInsensitive)) {
                entries << libraryEntry(path);
                continue;
            }
            const auto archive = tuxape::ZipArchive::open(path.toStdString());
            if (!archive)
                continue;
            QStringList programs;
            for (const tuxape::ZipArchive::Entry& file : archive->entries())
                if (const QString name = memberName(file.name); isProgram(name))
                    programs << name;
            // The archive's own "(AI)" and kind of release stand for all
            // it holds.
            const LibraryEntry whole = libraryEntry(path);
            for (const QString& name : programs) {
                LibraryEntry entry = libraryEntry(programs.size() == 1 ? path : name);
                entry.ai = entry.ai || whole.ai;
                if (entry.release.isEmpty())
                    entry.release = whole.release;
                entry.path = path;
                entry.member = name;
                entry.kind = kindOf(name);
                entries << entry;
            }
        }
    }
    std::sort(entries.begin(), entries.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        const int order = a.title.compare(b.title, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : a.path != b.path ? a.path < b.path : a.member < b.member;
    });
    return entries;
}

std::optional<std::vector<uint8_t>> libraryData(const LibraryEntry& entry)
{
    if (entry.member.isEmpty())
        return tuxape::readFile(entry.path.toStdString());
    const auto archive = tuxape::ZipArchive::open(entry.path.toStdString());
    if (!archive)
        return std::nullopt;
    for (const tuxape::ZipArchive::Entry& file : archive->entries())
        if (memberName(file.name) == entry.member)
            return archive->read(file);
    return std::nullopt;
}

QString libraryDisplayPath(const LibraryEntry& entry)
{
    const QString path = QDir::toNativeSeparators(entry.path);
    return entry.member.isEmpty() ? path : path + QString::fromUtf8(" \u00BB ") + entry.member;
}

LibraryDialog::LibraryDialog(const QStringList& folders, QWidget* parent)
    : QDialog(parent)
    , folders_(folders)
{
    setWindowTitle(tr("Library"));

    search_ = new QLineEdit;
    search_->setObjectName("edSearch");
    search_->setPlaceholderText(tr("Search"));
    search_->setClearButtonEnabled(true);
    list_ = new QTreeWidget;
    list_->setObjectName("lvLibrary");
    list_->setHeaderLabels({tr("Title"), tr("Year"), tr("Type"), tr("Release Type"), tr("AI"), tr("Notes")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAlternatingRowColors(true);
    list_->header()->setStretchLastSection(true);
    list_->setColumnWidth(0, 300);
    list_->setColumnWidth(1, 50);
    list_->setColumnWidth(2, 80);
    list_->setColumnWidth(kReleaseColumn, 100);
    list_->setColumnWidth(kAiColumn, 36);
    count_ = new QLabel;
    count_->setObjectName("lCount");
    count_->setWordWrap(true);

    auto* foldersButton = new QPushButton(tr("&Folders..."));
    foldersButton->setObjectName("bFolders");
    foldersButton->setAutoDefault(false);
    insertA_ = new QPushButton;
    insertA_->setObjectName("bInsertA");
    insertA_->setDefault(true);
    insertB_ = new QPushButton(tr("Insert in &B:"));
    insertB_->setObjectName("bInsertB");
    insertB_->setAutoDefault(false);
    auto* close = new QPushButton(tr("Close"));
    close->setAutoDefault(false);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(foldersButton);
    buttons->addWidget(count_, 1);
    buttons->addWidget(insertA_);
    buttons->addWidget(insertB_);
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(search_);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);
    resize(780, 480);

    connect(search_, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(list_, &QTreeWidget::itemActivated, this, [this] { choose(0); });
    connect(insertA_, &QPushButton::clicked, this, [this] { choose(0); });
    connect(insertB_, &QPushButton::clicked, this, [this] { choose(1); });
    connect(foldersButton, &QPushButton::clicked, this, [this] { editFolders(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    fill();
    search_->setFocus();
}

void LibraryDialog::setFolders(const QStringList& folders)
{
    folders_ = folders;
    fill();
}

QString LibraryDialog::search() const
{
    return search_->text();
}

void LibraryDialog::setSearch(const QString& text)
{
    search_->setText(text);
}

void LibraryDialog::fill()
{
    entries_ = scanLibrary(folders_);
    chosen_ = -1;
    list_->clear();
    for (int i = 0; i < entries_.size(); ++i) {
        const LibraryEntry& entry = entries_[i];
        const QString kind = entry.kind == LibraryEntry::Disc        ? tr("Disc")
                             : entry.kind == LibraryEntry::Tape      ? tr("Tape")
                             : entry.kind == LibraryEntry::Cartridge ? tr("Cartridge")
                                                                     : tr("Snapshot");
        // The kinds of release are names, not words to translate.
        auto* item = new QTreeWidgetItem(list_, {entry.title, entry.year, kind, entry.release, QString(), entry.details});
        item->setData(0, Qt::UserRole, i);
        // A box that shows, ticked or not, and is not for clicking.
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        item->setCheckState(kAiColumn, entry.ai ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(kAiColumn, entry.ai ? tr("\"(AI)\" is in the file's name") : QString());
        item->setToolTip(0, libraryDisplayPath(entry));
    }
    filter();
}

void LibraryDialog::filter()
{
    const QStringList words = search_->text().split(' ', Qt::SkipEmptyParts);
    int shown = 0;
    QTreeWidgetItem* first = nullptr;
    for (int row = 0; row < list_->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = list_->topLevelItem(row);
        const LibraryEntry& entry = entries_[item->data(0, Qt::UserRole).toInt()];
        const QString text = entry.title + ' ' + entry.year + ' ' + entry.details + ' ' + entry.release + ' '
                             + QFileInfo(entry.path).fileName()
                             + ' ' + entry.member;
        const bool match = std::all_of(words.begin(), words.end(),
                                       [&](const QString& word) { return text.contains(word, Qt::CaseInsensitive); });
        item->setHidden(!match);
        if (match && ++shown == 1)
            first = item;
    }
    // The first of what is left is ready for Enter.
    if (!list_->currentItem() || list_->currentItem()->isHidden())
        list_->setCurrentItem(first);
    if (folders_.isEmpty())
        count_->setText(tr("Click Folders... to say where your discs, tapes, cartridges and snapshots are."));
    else
        count_->setText(tr("%1 of %2").arg(shown).arg(entries_.size()));
    updateButtons();
}

int LibraryDialog::current() const
{
    const QTreeWidgetItem* item = list_->currentItem();
    return item && !item->isHidden() ? item->data(0, Qt::UserRole).toInt() : -1;
}

void LibraryDialog::updateButtons()
{
    const int index = current();
    const LibraryEntry::Kind kind = index >= 0 ? entries_[index].kind : LibraryEntry::Disc;
    insertA_->setText(kind == LibraryEntry::Snapshot ? tr("&Load") : kind == LibraryEntry::Disc ? tr("Insert in &A:") : tr("&Insert"));
    insertA_->setEnabled(index >= 0);
    insertB_->setEnabled(index >= 0 && kind == LibraryEntry::Disc);
}

QStringList LibraryDialog::listedTitles() const
{
    QStringList titles;
    for (int row = 0; row < list_->topLevelItemCount(); ++row)
        if (!list_->topLevelItem(row)->isHidden())
            titles << list_->topLevelItem(row)->text(0);
    return titles;
}

QStringList LibraryDialog::listedAiTitles() const
{
    QStringList titles;
    for (int row = 0; row < list_->topLevelItemCount(); ++row)
        if (!list_->topLevelItem(row)->isHidden() && list_->topLevelItem(row)->checkState(kAiColumn) == Qt::Checked)
            titles << list_->topLevelItem(row)->text(0);
    return titles;
}

QStringList LibraryDialog::listedReleases() const
{
    QStringList releases;
    for (int row = 0; row < list_->topLevelItemCount(); ++row)
        if (!list_->topLevelItem(row)->isHidden())
            releases << list_->topLevelItem(row)->text(kReleaseColumn);
    return releases;
}

bool LibraryDialog::select(const QString& title)
{
    for (int row = 0; row < list_->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = list_->topLevelItem(row);
        if (!item->isHidden() && item->text(0) == title) {
            list_->setCurrentItem(item);
            return true;
        }
    }
    return false;
}

bool LibraryDialog::choose(int drive)
{
    const int index = current();
    if (index < 0 || (drive != 0 && entries_[index].kind != LibraryEntry::Disc))
        return false;
    chosen_ = index;
    drive_ = drive;
    accept();
    return true;
}

// The Folders button: a list of the folders, to add to and take from.
void LibraryDialog::editFolders()
{
    QDialog dialog(this);
    dialog.setObjectName("frmLibraryFolders");
    dialog.setWindowTitle(tr("Library Folders"));
    auto* list = new QListWidget;
    for (const QString& folder : folders_)
        list->addItem(QDir::toNativeSeparators(folder));
    auto* add = new QPushButton(tr("&Add..."));
    auto* remove = new QPushButton(tr("&Remove"));
    auto* ok = new QPushButton(tr("OK"));
    ok->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Discs (.dsk), tapes (.cdt), cartridges (.cpr) and snapshots (.sna), zipped or not, are looked for in:")));
    layout->addWidget(list, 1);
    layout->addLayout(buttons);
    dialog.resize(480, 260);
    connect(add, &QPushButton::clicked, &dialog, [&] {
        const QString folder = QFileDialog::getExistingDirectory(&dialog, tr("Add a Folder to the Library"));
        if (!folder.isEmpty() && list->findItems(QDir::toNativeSeparators(folder), Qt::MatchExactly).isEmpty())
            list->addItem(QDir::toNativeSeparators(folder));
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] { delete list->takeItem(list->currentRow()); });
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QStringList folders;
    for (int row = 0; row < list->count(); ++row)
        folders << QDir::fromNativeSeparators(list->item(row)->text());
    setFolders(folders);
}
