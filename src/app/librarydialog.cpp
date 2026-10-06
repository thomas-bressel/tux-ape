#include "librarydialog.h"

#include "icons.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QCoreApplication>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QTabBar>
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
           || name.endsWith(".cpr", Qt::CaseInsensitive) || name.endsWith(".sna", Qt::CaseInsensitive)
           || name.endsWith(".snr", Qt::CaseInsensitive);
}

constexpr int kSubcategoryColumn = 1;
constexpr int kTypeColumn = 3;
constexpr int kReleaseColumn = 4;
constexpr int kAiColumn = 5;
constexpr int kThumbnailColumn = 6;

// A row of the list. A click on a column's heading sorts by that column,
// from A to Z, and a second click from Z to A: text without regard to
// case, the Type by its name, the boxes unticked first. Rows that say
// the same there stay in the order of their titles, whichever way.
class LibraryItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem& other) const override
    {
        const QTreeWidget* list = treeWidget();
        const int column = list ? list->sortColumn() : 0;
        const auto key = [column](const QTreeWidgetItem& item) {
            return column == kTypeColumn ? item.toolTip(column)
                   : column == kAiColumn || column == kThumbnailColumn
                       ? QString(item.checkState(column) == Qt::Checked ? '1' : '0')
                                         : item.text(column);
        };
        if (const int order = key(*this).compare(key(other), Qt::CaseInsensitive))
            return order < 0;
        int tie = column == 0 ? 0 : text(0).compare(other.text(0), Qt::CaseInsensitive);
        if (!tie)
            tie = data(0, Qt::UserRole).toInt() - other.data(0, Qt::UserRole).toInt();
        // From Z to A the list asks the question the other way round.
        return list && list->header()->sortIndicatorOrder() == Qt::DescendingOrder ? tie > 0 : tie < 0;
    }
};

// The category and the sub-category the folders give a program found in
// a library folder: "<library>/Games/Racing/Outrun.dsk". The library
// folder may be a category's own.
void classify(LibraryEntry& entry, const QString& folder)
{
    const QDir root(QDir::cleanPath(folder));
    entry.root = root.path();
    QStringList parts = root.relativeFilePath(entry.path).split('/', Qt::SkipEmptyParts);
    if (!parts.isEmpty())
        parts.removeLast();  // the file itself
    parts.prepend(root.dirName());
    for (int i = 0; i < 2 && i < parts.size(); ++i) {
        for (const QString& known : libraryCategories()) {
            if (parts[i].compare(known, Qt::CaseInsensitive) == 0) {
                entry.category = known;
                entry.subcategory = i + 1 < parts.size() ? parts[i + 1] : QString();
                return;
            }
        }
    }
}

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
    // A port from another machine names it: "Atari ST Port".
    static const QRegularExpression port("^\\S.* port$", QRegularExpression::CaseInsensitiveOption);
    *whole = port.match(note).hasMatch();
    return *whole ? note : QString();
}

LibraryEntry::Kind kindOf(const QString& name)
{
    return name.endsWith(".snr", Qt::CaseInsensitive)   ? LibraryEntry::Session
           : name.endsWith(".sna", Qt::CaseInsensitive) ? LibraryEntry::Snapshot
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

const char* kindName(LibraryEntry::Kind kind)
{
    static const char* const kNames[] = {"Disc", "Snapshot", "Tape", "Cartridge", "Session"};
    return kNames[kind];
}

// A text as part of a file's name.
QString asFileName(QString text)
{
    static const QRegularExpression unfit("[\\\\/:*?\"<>|\\x00-\\x1F]");
    return text.replace(unfit, "_").simplified();
}

// The pictures of a folder, in the order of their names.
QStringList picturesIn(const QString& folder)
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << "*." + QString::fromLatin1(format);
    return QDir(folder).entryList(patterns, QDir::Files, QDir::Name | QDir::IgnoreCase);
}

// A program's picture among those of its thumbnails folder.
QString pictureOf(const LibraryEntry& entry, const QStringList& pictures)
{
    const QString name = libraryThumbnailName(entry);
    for (const QString& picture : pictures)
        if (QFileInfo(picture).completeBaseName().compare(name, Qt::CaseInsensitive) == 0)
            return picture;
    // Failing that, one of the same title and year.
    const QString start = asFileName(entry.title) + (entry.year.isEmpty() ? QString() : " (" + entry.year + ")");
    for (const QString& picture : pictures) {
        const QString base = QFileInfo(picture).completeBaseName();
        if (base.compare(start, Qt::CaseInsensitive) == 0 || base.startsWith(start + " (", Qt::CaseInsensitive) ||
            base.startsWith(start + " [", Qt::CaseInsensitive))
            return picture;
    }
    return {};
}

}  // namespace

QString libraryThumbnailName(const LibraryEntry& entry)
{
    QString name = entry.title;
    if (!entry.year.isEmpty())
        name += " (" + entry.year + ")";
    if (const QString& genre = entry.subcategory.isEmpty() ? entry.category : entry.subcategory; !genre.isEmpty())
        name += " (" + genre + ")";
    name += QStringLiteral(" (") + kindName(entry.kind) + ")";
    if (!entry.release.isEmpty())
        name += " [" + entry.release + "]";
    return asFileName(name);
}

QString libraryThumbnailFolder(const LibraryEntry& entry)
{
    return (entry.root.isEmpty() ? QFileInfo(entry.path).absolutePath() : entry.root) + "/thumbnails";
}

QString libraryThumbnail(const LibraryEntry& entry)
{
    const QString folder = libraryThumbnailFolder(entry);
    const QString picture = pictureOf(entry, picturesIn(folder));
    return picture.isEmpty() ? QString() : folder + '/' + picture;
}

QString setLibraryThumbnail(const LibraryEntry& entry, const QString& picture)
{
    QImageReader reader(picture);
    if (!reader.canRead())
        return LibraryDialog::tr("%1 is not a picture.").arg(QDir::toNativeSeparators(picture));
    const QString folder = libraryThumbnailFolder(entry);
    if (!QDir().mkpath(folder))
        return LibraryDialog::tr("Cannot make the folder %1.").arg(QDir::toNativeSeparators(folder));
    const QString name = libraryThumbnailName(entry);
    QString suffix = QFileInfo(picture).suffix().toLower();
    if (suffix.isEmpty())
        suffix = QString::fromLatin1(reader.format());
    const QString target = folder + '/' + name + '.' + suffix;
    // Its own picture given again: there is nothing to do.
    const QString source = QFileInfo(picture).canonicalFilePath();
    if (source == QFileInfo(target).canonicalFilePath())
        return {};
    // The one it had goes, whatever kind of picture it was.
    for (const QString& old : picturesIn(folder))
        if (QFileInfo(old).completeBaseName().compare(name, Qt::CaseInsensitive) == 0 &&
            QFileInfo(folder + '/' + old).canonicalFilePath() != source)
            QFile::remove(folder + '/' + old);
    if (!QFile::copy(picture, target))
        return LibraryDialog::tr("Cannot write %1.").arg(QDir::toNativeSeparators(target));
    return {};
}

QString defaultLibraryFolder()
{
    // Said from outside, it is that folder or none.
    if (qEnvironmentVariableIsSet("TUXAPE_LIBRARY_DIR")) {
        const QString named = qEnvironmentVariable("TUXAPE_LIBRARY_DIR");
        return QDir(named).exists() && !named.isEmpty() ? QDir::cleanPath(named) : QString();
    }
    for (const QString& folder : {QCoreApplication::applicationDirPath() + "/library", QStringLiteral(TUXAPE_DEV_LIBRARY_DIR)})
        if (QDir(folder).exists())
            return QDir::cleanPath(folder);
    return {};
}

QStringList libraryFoldersOrDefault(const QStringList& named)
{
    if (std::any_of(named.begin(), named.end(), [](const QString& folder) { return QDir(folder).exists(); }))
        return named;
    const QString own = defaultLibraryFolder();
    return own.isEmpty() ? named : QStringList{own};
}

QStringList libraryCategories()
{
    return {"Games", "Educational", "Utilities", "Demos", "Compilations", "Miscellaneous", "SNR"};
}

QString libraryCategoryTitle(const QString& category)
{
    return category == "SNR" ? LibraryDialog::tr("Let's Play") : category;
}

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
        QDirIterator it(folder, {"*.dsk", "*.cdt", "*.cpr", "*.sna", "*.snr", "*.zip"}, QDir::Files,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString path = QDir::cleanPath(it.next());
            // A folder named twice, or inside another, gives each file once.
            if (seen.contains(path))
                continue;
            seen.insert(path);
            if (!path.endsWith(".zip", Qt::CaseInsensitive)) {
                LibraryEntry entry = libraryEntry(path);
                classify(entry, folder);
                entries << entry;
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
                classify(entry, folder);
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
    tabs_ = new QTabBar;
    tabs_->setObjectName("tabCategories");
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    list_->setHeaderLabels({tr("Title"), tr("Sub-category"), tr("Year"), tr("Type"), tr("Release Type"), tr("AI"),
                            tr("Thumbnail"), tr("Notes")});
    // Several programs at once, to give them a picture.
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAlternatingRowColors(true);
    list_->header()->setStretchLastSection(true);
    list_->setColumnWidth(0, 280);
    list_->setColumnWidth(kSubcategoryColumn, 130);
    list_->setColumnWidth(2, 50);
    list_->setColumnWidth(kTypeColumn, 44);
    list_->setIconSize(QSize(18, 18));
    list_->setColumnWidth(kReleaseColumn, 100);
    list_->setColumnWidth(kAiColumn, 36);
    list_->setColumnWidth(kThumbnailColumn, 84);
    // A program's picture, beside the pointer while it is over the program.
    preview_ = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint);
    preview_->setObjectName("lPreview");
    preview_->setFrameShape(QFrame::Box);
    preview_->setAttribute(Qt::WA_ShowWithoutActivating);
    preview_->setAttribute(Qt::WA_TransparentForMouseEvents);
    preview_->hide();
    list_->viewport()->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    // The headings are for clicking: by title to start with.
    list_->setSortingEnabled(true);
    list_->sortByColumn(0, Qt::AscendingOrder);
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
    thumbnail_ = new QPushButton(tr("&Thumbnail..."));
    thumbnail_->setObjectName("bThumbnail");
    thumbnail_->setAutoDefault(false);
    thumbnail_->setToolTip(tr("Gives a picture to the programs selected"));
    auto* close = new QPushButton(tr("Close"));
    close->setAutoDefault(false);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(foldersButton);
    buttons->addWidget(thumbnail_);
    buttons->addWidget(count_, 1);
    buttons->addWidget(insertA_);
    buttons->addWidget(insertB_);
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(search_);
    layout->addWidget(tabs_);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);
    resize(900, 520);

    connect(search_, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(tabs_, &QTabBar::currentChanged, this, [this] { filter(); });
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    connect(thumbnail_, &QPushButton::clicked, this, [this] { chooseThumbnail(); });
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
    // The tab in front stays in front, if it is still there.
    const bool hadTabs = tabs_->count() > 0;
    const QString wanted = category();
    entries_ = scanLibrary(folders_);
    chosen_ = -1;
    // A tab for each category that has a folder, and one for what is in
    // none.
    {
        const QSignalBlocker blocker(tabs_);
        while (tabs_->count())
            tabs_->removeTab(0);
        const auto filed = [this](const QString& name) {
            return std::any_of(entries_.begin(), entries_.end(), [&](const LibraryEntry& entry) { return entry.category == name; });
        };
        for (const QString& name : libraryCategories()) {
            bool there = filed(name);
            for (const QString& folder : folders_)
                there = there || QDir(folder).dirName().compare(name, Qt::CaseInsensitive) == 0 || QDir(folder).exists(name);
            if (there)
                tabs_->setTabData(tabs_->addTab(libraryCategoryTitle(name)), name);
        }
        if (filed(QString()) || tabs_->count() == 0)
            tabs_->setTabData(tabs_->addTab(tr("Unsorted")), QString());
        // The window opens on the first category that has something in it.
        for (int tab = tabs_->count() - 1; !hadTabs && tab >= 0; --tab)
            if (filed(tabs_->tabData(tab).toString()))
                tabs_->setCurrentIndex(tab);
        for (int tab = 0; hadTabs && tab < tabs_->count(); ++tab)
            if (tabs_->tabData(tab).toString() == wanted)
                tabs_->setCurrentIndex(tab);
        tabs_->setVisible(tabs_->count() > 1);
    }
    list_->clear();
    // Sorted once they are all there, not one by one.
    list_->setSortingEnabled(false);
    for (int i = 0; i < entries_.size(); ++i) {
        const LibraryEntry& entry = entries_[i];
        const QString kind = entry.kind == LibraryEntry::Disc        ? tr("Disc")
                             : entry.kind == LibraryEntry::Tape      ? tr("Tape")
                             : entry.kind == LibraryEntry::Session   ? tr("Session")
                             : entry.kind == LibraryEntry::Cartridge ? tr("Cartridge")
                                                                     : tr("Snapshot");
        // The kinds of release are names, not words to translate.
        // The type shows as a picture: a disc, a tape, a cartridge, or a
        // snapshot; its name is there for whoever points at it.
        static const QIcon kIcons[] = {makeIcon(IconId::Disc), makeIcon(IconId::LoadSnapshot), makeIcon(IconId::Tape),
                                       makeIcon(IconId::Cartridge), makeIcon(IconId::Run)};
        auto* item = new LibraryItem(list_, {entry.title, entry.subcategory, entry.year, QString(), entry.release, QString(),
                                             QString(), entry.details});
        item->setIcon(kTypeColumn, kIcons[entry.kind]);
        item->setToolTip(kTypeColumn, kind);
        item->setData(0, Qt::UserRole, i);
        // A box that shows, ticked or not, and is not for clicking.
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        item->setCheckState(kAiColumn, entry.ai ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(kAiColumn, entry.ai ? tr("\"(AI)\" is in the file's name") : QString());
        item->setToolTip(0, libraryDisplayPath(entry));
    }
    updateThumbnails();
    list_->setSortingEnabled(true);
    filter();
}

// Looks for each program's picture, and ticks the boxes of those that
// have one.
void LibraryDialog::updateThumbnails()
{
    QHash<QString, QStringList> pictures;  // of each thumbnails folder
    thumbnails_.clear();
    for (const LibraryEntry& entry : entries_) {
        const QString folder = libraryThumbnailFolder(entry);
        if (!pictures.contains(folder))
            pictures.insert(folder, picturesIn(folder));
        const QString picture = pictureOf(entry, pictures.value(folder));
        thumbnails_ << (picture.isEmpty() ? QString() : folder + '/' + picture);
    }
    for (int row = 0; row < list_->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = list_->topLevelItem(row);
        const QString& picture = thumbnails_[item->data(0, Qt::UserRole).toInt()];
        item->setCheckState(kThumbnailColumn, picture.isEmpty() ? Qt::Unchecked : Qt::Checked);
        item->setToolTip(kThumbnailColumn, QFileInfo(picture).fileName());
    }
    previewed_.clear();
    preview_->hide();
}

// The rows selected, of those listed.
QList<int> LibraryDialog::selection() const
{
    QList<int> rows;
    for (int row = 0; row < list_->topLevelItemCount(); ++row) {
        const QTreeWidgetItem* item = list_->topLevelItem(row);
        if (item->isSelected() && !item->isHidden())
            rows << item->data(0, Qt::UserRole).toInt();
    }
    return rows;
}

QStringList LibraryDialog::selectedTitles() const
{
    QStringList titles;
    for (int index : selection())
        titles << entries_[index].title;
    return titles;
}

QStringList LibraryDialog::listedThumbnailTitles() const
{
    QStringList titles;
    for (int row = 0; row < list_->topLevelItemCount(); ++row)
        if (!list_->topLevelItem(row)->isHidden() && list_->topLevelItem(row)->checkState(kThumbnailColumn) == Qt::Checked)
            titles << list_->topLevelItem(row)->text(0);
    return titles;
}

bool LibraryDialog::setThumbnail(const QString& picture)
{
    QString error;
    for (int index : selection()) {
        error = setLibraryThumbnail(entries_[index], picture);
        if (!error.isEmpty())
            break;
    }
    // The list keeps its order while the boxes change.
    list_->setSortingEnabled(false);
    updateThumbnails();
    list_->setSortingEnabled(true);
    if (!error.isEmpty())
        QMessageBox::warning(this, windowTitle(), error);
    return error.isEmpty();
}

// The Thumbnail button: the picture is chosen among the user's files.
void LibraryDialog::chooseThumbnail()
{
    if (selection().isEmpty())
        return;
    static QString folder;  // where the last one was taken
    QStringList patterns;
    for (const char* format : {"png", "jpg", "jpeg", "gif", "bmp", "webp"})
        if (QImageReader::supportedImageFormats().contains(format))
            patterns << QStringLiteral("*.") + format;
    const QString picture = QFileDialog::getOpenFileName(this, tr("Thumbnail"), folder,
                                                         tr("Pictures (%1);;All files (*)").arg(patterns.join(' ')));
    if (picture.isEmpty())
        return;
    folder = QFileInfo(picture).absolutePath();
    setThumbnail(picture);
}

// Shows the picture of the program at a place of the list, beside the
// pointer; or none if the program has none.
void LibraryDialog::showPreview(const QPoint& at)
{
    const QTreeWidgetItem* item = list_->itemAt(at);
    const QString picture = item ? thumbnails_.value(item->data(0, Qt::UserRole).toInt()) : QString();
    if (picture.isEmpty()) {
        preview_->hide();
        return;
    }
    if (picture != previewed_) {
        // Read at the size it shows at: no larger than this, and never
        // made larger than it is.
        constexpr QSize kLargest(320, 320);
        QImageReader reader(picture);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid() && (size.width() > kLargest.width() || size.height() > kLargest.height()))
            reader.setScaledSize(size.scaled(kLargest, Qt::KeepAspectRatio));
        QImage image = reader.read();
        if (image.isNull()) {
            preview_->hide();
            return;
        }
        if (image.width() > kLargest.width() || image.height() > kLargest.height())
            image = image.scaled(kLargest, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        preview_->setPixmap(QPixmap::fromImage(image));
        preview_->adjustSize();
        previewed_ = picture;
    }
    // Below the pointer and to its right, or on the other side where the
    // screen ends.
    const QPoint pointer = list_->viewport()->mapToGlobal(at);
    const QRect area = screen() ? screen()->availableGeometry() : QRect(pointer, preview_->size() * 2);
    QPoint corner = pointer + QPoint(18, 18);
    if (corner.x() + preview_->width() > area.right())
        corner.setX(pointer.x() - 18 - preview_->width());
    if (corner.y() + preview_->height() > area.bottom())
        corner.setY(pointer.y() - 18 - preview_->height());
    preview_->move(corner);
    preview_->show();
}

bool LibraryDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == list_->viewport()) {
        switch (event->type()) {
        case QEvent::MouseMove:
            showPreview(static_cast<QMouseEvent*>(event)->position().toPoint());
            break;
        case QEvent::Leave:
        case QEvent::Wheel:
        case QEvent::MouseButtonPress:
            preview_->hide();
            break;
        case QEvent::ToolTip:
            // The picture is what there is to say of the program.
            if (preview_->isVisible())
                return true;
            break;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void LibraryDialog::hideEvent(QHideEvent* event)
{
    preview_->hide();
    QDialog::hideEvent(event);
}

int LibraryDialog::sortColumn() const
{
    return list_->sortColumn();
}

Qt::SortOrder LibraryDialog::sortOrder() const
{
    return list_->header()->sortIndicatorOrder();
}

void LibraryDialog::setSort(int column, Qt::SortOrder order)
{
    if (column >= 0 && column < list_->columnCount())
        list_->sortByColumn(column, order);
}

void LibraryDialog::filter()
{
    const QStringList words = search_->text().split(' ', Qt::SkipEmptyParts);
    const QString tab = category();
    int shown = 0, filed = 0;
    QTreeWidgetItem* first = nullptr;
    for (int row = 0; row < list_->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = list_->topLevelItem(row);
        const LibraryEntry& entry = entries_[item->data(0, Qt::UserRole).toInt()];
        // Only the programs of the category in front.
        if (entry.category != tab) {
            item->setHidden(true);
            continue;
        }
        ++filed;
        const QString text = entry.title + ' ' + entry.subcategory + ' ' + entry.year + ' ' + entry.details + ' ' + entry.release
                             + ' ' + QFileInfo(entry.path).fileName()
                             + ' ' + entry.member;
        const bool match = std::all_of(words.begin(), words.end(),
                                       [&](const QString& word) { return text.contains(word, Qt::CaseInsensitive); });
        item->setHidden(!match);
        if (match && ++shown == 1)
            first = item;
    }
    // The first of what is left is ready for Enter.
    // It alone is selected, whatever keys are held down.
    if (!list_->currentItem() || list_->currentItem()->isHidden())
        list_->setCurrentItem(first, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (folders_.isEmpty())
        count_->setText(tr("Click Folders... to say where your discs, tapes, cartridges and snapshots are."));
    else
        count_->setText(tr("%1 of %2").arg(shown).arg(filed));
    updateButtons();
}

// The program to put in the machine: the one selected, when there is
// only one.
int LibraryDialog::current() const
{
    const QList<int> rows = selection();
    return rows.size() == 1 ? rows.first() : -1;
}

void LibraryDialog::updateButtons()
{
    const int index = current();
    const LibraryEntry::Kind kind = index >= 0 ? entries_[index].kind : LibraryEntry::Disc;
    insertA_->setText(kind == LibraryEntry::Snapshot  ? tr("&Load")
                      : kind == LibraryEntry::Session ? tr("&Play")
                      : kind == LibraryEntry::Disc    ? tr("Insert in &A:")
                                                      : tr("&Insert"));
    insertA_->setEnabled(index >= 0);
    insertB_->setEnabled(index >= 0 && kind == LibraryEntry::Disc);
    thumbnail_->setEnabled(!selection().isEmpty());
}

QStringList LibraryDialog::categories() const
{
    QStringList names;
    for (int tab = 0; tab < tabs_->count(); ++tab)
        names << tabs_->tabText(tab);
    return names;
}

QString LibraryDialog::category() const
{
    return tabs_->count() ? tabs_->tabData(tabs_->currentIndex()).toString() : QString();
}

bool LibraryDialog::setCategory(const QString& name)
{
    for (int tab = 0; tab < tabs_->count(); ++tab) {
        if (tabs_->tabData(tab).toString().compare(name, Qt::CaseInsensitive) == 0) {
            tabs_->setCurrentIndex(tab);
            return true;
        }
    }
    return false;
}

QStringList LibraryDialog::listedSubcategories() const
{
    QStringList names;
    for (int row = 0; row < list_->topLevelItemCount(); ++row)
        if (!list_->topLevelItem(row)->isHidden())
            names << list_->topLevelItem(row)->text(kSubcategoryColumn);
    return names;
}

QStringList LibraryDialog::listedTypes() const
{
    QStringList names;
    for (int row = 0; row < list_->topLevelItemCount(); ++row)
        if (!list_->topLevelItem(row)->isHidden())
            names << list_->topLevelItem(row)->toolTip(kTypeColumn);
    return names;
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
            list_->setCurrentItem(item, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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
    layout->addWidget(new QLabel(tr("Discs (.dsk), tapes (.cdt), cartridges (.cpr), snapshots (.sna) and sessions (.snr), zipped or not, are looked for in:")));
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
