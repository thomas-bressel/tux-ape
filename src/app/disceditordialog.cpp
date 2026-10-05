#include "disceditordialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "emulator.h"

#include "core/files.h"

using tuxape::DiscFile;
using tuxape::DiscFiles;

namespace {

QString hex(unsigned value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

}  // namespace

template <class Work>
bool DiscEditorDialog::withFiles(Work&& work) const
{
    return emulator_->withMachine([&](tuxape::Cpc& cpc) {
        tuxape::Disc* disc = cpc.fdc().drive(drive_).disc.get();
        if (!disc)
            return false;
        DiscFiles files(*disc);
        if (!files.valid())
            return false;
        return work(files);
    });
}

DiscEditorDialog::DiscEditorDialog(Emulator* emulator, int drive, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
    , drive_(drive)
{
    setWindowTitle(tr("Disc Editor - Drive %1:").arg(QChar('A' + drive)));
    writable_ = emulator_->withMachine([&](tuxape::Cpc& cpc) {
        const tuxape::Disc* disc = cpc.fdc().drive(drive_).disc.get();
        return disc && !disc->writeProtected;
    });

    // ---- File Editor ----
    files_ = new QTreeWidget;
    files_->setObjectName("lvFiles");
    files_->setHeaderLabels({tr("Name"), tr("Size"), tr("Attributes"), tr("User")});
    files_->setRootIsDecorated(false);
    files_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files_->setColumnWidth(0, 160);
    files_->setColumnWidth(1, 70);
    files_->setColumnWidth(2, 150);
    showSystemBox_ = new QCheckBox(tr("Show System files"));
    showSystemBox_->setObjectName("ckShowSystem");
    headersBox_ = new QCheckBox(tr("Add/Remove AMSDOS headers"));
    headersBox_->setObjectName("ckHeaders");
    headersBox_->setChecked(true);
    free_ = new QLabel;
    free_->setObjectName("lFree");
    auto button = [this](const QString& text, const char* name, void (DiscEditorDialog::*slot)(), bool changes) {
        auto* created = new QPushButton(text);
        created->setObjectName(name);
        created->setAutoDefault(false);
        created->setEnabled(writable_ || !changes);
        connect(created, &QPushButton::clicked, this, slot);
        return created;
    };
    auto* fileButtons = new QHBoxLayout;
    fileButtons->addWidget(button(tr("&Add Files..."), "bAdd", &DiscEditorDialog::addClicked, true));
    fileButtons->addWidget(button(tr("&Extract..."), "bExtract", &DiscEditorDialog::extractClicked, false));
    fileButtons->addWidget(button(tr("&Rename"), "bRename", &DiscEditorDialog::renameClicked, true));
    fileButtons->addWidget(button(tr("&Delete"), "bDelete", &DiscEditorDialog::deleteClicked, true));
    fileButtons->addWidget(button(tr("&Properties"), "bProperties", &DiscEditorDialog::propertiesClicked, true));
    fileButtons->addStretch(1);
    auto* options = new QHBoxLayout;
    options->addWidget(showSystemBox_);
    options->addWidget(headersBox_);
    options->addStretch(1);
    options->addWidget(free_);
    auto* filePage = new QWidget;
    auto* fileLayout = new QVBoxLayout(filePage);
    fileLayout->addWidget(files_, 1);
    fileLayout->addLayout(options);
    fileLayout->addLayout(fileButtons);

    // ---- Sector Editor ----
    track_ = new QSpinBox;
    track_->setObjectName("seTrack");
    side_ = new QSpinBox;
    side_->setObjectName("seSide");
    sector_ = new QComboBox;
    sector_->setObjectName("cbSector");
    sectorInfo_ = new QLabel;
    sectorInfo_->setObjectName("lSector");
    const auto [cylinders, sides] = emulator_->withMachine([&](tuxape::Cpc& cpc) {
        const tuxape::Disc* disc = cpc.fdc().drive(drive_).disc.get();
        return std::make_pair(disc ? disc->cylinders() : 0, disc ? disc->sides() : 1);
    });
    track_->setRange(0, std::max(0, cylinders - 1));
    side_->setRange(0, sides - 1);
    side_->setEnabled(sides > 1);
    sectorView_ = new MemoryDumpView;
    sectorView_->setObjectName("SectorView");
    sectorView_->setMemory(&sectorBytes_);
    auto* where = new QHBoxLayout;
    where->addWidget(new QLabel(tr("Track:")));
    where->addWidget(track_);
    where->addWidget(new QLabel(tr("Side:")));
    where->addWidget(side_);
    where->addWidget(new QLabel(tr("Sector:")));
    where->addWidget(sector_);
    where->addWidget(sectorInfo_, 1);
    auto* sectorPage = new QWidget;
    auto* sectorLayout = new QVBoxLayout(sectorPage);
    sectorLayout->addLayout(where);
    sectorLayout->addWidget(sectorView_, 1);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("PageControl");
    tabs_->addTab(filePage, tr("File Editor"));
    tabs_->addTab(sectorPage, tr("Sector Editor"));
    auto* close = new QPushButton(tr("Close"));
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    bottom->addWidget(close);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs_, 1);
    layout->addLayout(bottom);
    resize(640, 460);

    connect(showSystemBox_, &QCheckBox::toggled, this, &DiscEditorDialog::setShowSystem);
    connect(headersBox_, &QCheckBox::toggled, this, &DiscEditorDialog::setHeaders);
    connect(track_, &QSpinBox::valueChanged, this, [this] { fillSectors(); });
    connect(side_, &QSpinBox::valueChanged, this, [this] { fillSectors(); });
    connect(sector_, &QComboBox::currentIndexChanged, this, [this] { showSector(); });
    connect(sectorView_, &MemoryDumpView::byteEdited, this, [this](uint16_t offset, uint8_t value) {
        if (!writable_)
            return;
        const int index = sector_->currentIndex();
        emulator_->withMachine([&](tuxape::Cpc& cpc) {
            tuxape::Disc* disc = cpc.fdc().drive(drive_).disc.get();
            tuxape::DiscTrack* track = disc ? disc->track(track_->value(), side_->value()) : nullptr;
            if (track && index >= 0 && index < static_cast<int>(track->sectors.size())
                && offset < track->sectors[static_cast<size_t>(index)].data.size()) {
                track->sectors[static_cast<size_t>(index)].data[offset] = value;
                disc->modified = true;
            }
        });
        showSector();
        fillFiles();
    });

    fillFiles();
    fillSectors();
}

// ---- files -------------------------------------------------------------------

void DiscEditorDialog::fillFiles()
{
    files_->clear();
    QString freeText = tr("Not a disc whose files can be listed");
    withFiles([&](DiscFiles& files) {
        for (const DiscFile& file : files.list()) {
            if (file.system && !showSystem_)
                continue;
            QStringList attributes;
            if (file.readOnly)
                attributes << tr("Read-only");
            if (file.system)
                attributes << tr("System");
            auto* item = new QTreeWidgetItem(files_, {QString::fromLatin1(file.name.c_str()), tr("%1K").arg((file.size + 1023) / 1024),
                                                      attributes.join(", "), QString::number(file.user)});
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
        freeText = tr("%1 - %2K free").arg(QString::fromLatin1(files.format()->name)).arg(files.freeBytes() / 1024);
        return true;
    });
    free_->setText(freeText);
}

QStringList DiscEditorDialog::listedFiles() const
{
    QStringList names;
    for (int i = 0; i < files_->topLevelItemCount(); ++i)
        names << files_->topLevelItem(i)->text(0);
    return names;
}

QString DiscEditorDialog::freeText() const
{
    return free_->text();
}

void DiscEditorDialog::setShowSystem(bool show)
{
    showSystem_ = show;
    showSystemBox_->setChecked(show);
    fillFiles();
}

void DiscEditorDialog::setHeaders(bool on)
{
    headers_ = on;
    headersBox_->setChecked(on);
}

std::optional<DiscFile> DiscEditorDialog::fileNamed(const QString& name) const
{
    std::optional<DiscFile> found;
    withFiles([&](DiscFiles& files) {
        for (const DiscFile& file : files.list())
            if (QString::fromLatin1(file.name.c_str()).compare(name, Qt::CaseInsensitive) == 0 && !found)
                found = file;
        return true;
    });
    return found;
}

bool DiscEditorDialog::addFile(const QString& hostPath, QString* error)
{
    auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!writable_)
        return fail(tr("The disc cannot be written to."));
    auto data = tuxape::readFile(hostPath.toStdString());
    if (!data)
        return fail(tr("Cannot read %1.").arg(QDir::toNativeSeparators(hostPath)));
    // The name is cut to what a disc's directory takes.
    const QFileInfo info(hostPath);
    QString stem = info.completeBaseName().left(8), extension = info.suffix().left(3);
    stem.remove(' ');
    const QString name = extension.isEmpty() ? stem : stem + '.' + extension;
    if (!DiscFiles::directoryName(name.toStdString()))
        return fail(tr("%1 is not a name a disc can hold.").arg(info.fileName()));
    // A file that is not text, brought without AMSDOS's header, is given
    // one: a binary that loads where it was put.
    const bool text = std::all_of(data->begin(), data->end(), [](uint8_t b) { return b == 9 || b == 10 || b == 13 || b == 26 || (b >= 32 && b < 127); });
    if (headers_ && !text && !tuxape::amsdosHeader(*data)) {
        tuxape::AmsdosHeader header;
        header.length = static_cast<int>(data->size());
        const std::vector<uint8_t> bytes = tuxape::makeAmsdosHeader(name.toStdString(), header);
        data->insert(data->begin(), bytes.begin(), bytes.end());
    }
    bool written = false;
    const bool known = withFiles([&](DiscFiles& files) {
        written = files.write(name.toStdString(), *data);
        return true;
    });
    fillFiles();
    if (!known)
        return fail(tr("The disc's format is not one files can be put on."));
    return written || fail(tr("There is no room on the disc for %1.").arg(info.fileName()));
}

bool DiscEditorDialog::extractFile(const QString& name, const QString& hostPath, QString* error)
{
    auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const auto file = fileNamed(name);
    std::optional<std::vector<uint8_t>> data;
    if (file)
        withFiles([&](DiscFiles& files) {
            data = files.read(*file);
            return true;
        });
    if (!data)
        return fail(tr("%1 is not on the disc.").arg(name));
    // The header says how long the file really is; without one, a text
    // ends at its end-of-file mark.
    if (const auto header = tuxape::amsdosHeader(*data)) {
        if (headers_) {
            data->erase(data->begin(), data->begin() + 128);
            if (header->length > 0 && static_cast<size_t>(header->length) < data->size())
                data->resize(static_cast<size_t>(header->length));
        } else if (header->length > 0 && static_cast<size_t>(header->length) + 128 < data->size()) {
            data->resize(static_cast<size_t>(header->length) + 128);
        }
    } else {
        // A text ends at its end-of-file mark, which is in its last
        // record; anything else is taken whole.
        const auto end = std::find(data->begin(), data->end(), 0x1A);
        const bool text = std::all_of(data->begin(), end, [](uint8_t b) { return b == 9 || b == 10 || b == 13 || (b >= 32 && b < 127); });
        if (text && end != data->end() && data->end() - end <= 128)
            data->erase(end, data->end());
    }
    return tuxape::writeFile(hostPath.toStdString(), *data) || fail(tr("Cannot write %1.").arg(QDir::toNativeSeparators(hostPath)));
}

bool DiscEditorDialog::renameFile(const QString& name, const QString& newName)
{
    const auto file = fileNamed(name);
    bool done = false;
    if (file && writable_)
        withFiles([&](DiscFiles& files) {
            done = files.rename(*file, newName.toStdString());
            return true;
        });
    fillFiles();
    return done;
}

bool DiscEditorDialog::deleteFile(const QString& name)
{
    const auto file = fileNamed(name);
    bool done = false;
    if (file && writable_)
        withFiles([&](DiscFiles& files) {
            done = files.remove(*file);
            return true;
        });
    fillFiles();
    return done;
}

bool DiscEditorDialog::setFileAttributes(const QString& name, bool readOnly, bool system)
{
    const auto file = fileNamed(name);
    bool done = false;
    if (file && writable_)
        withFiles([&](DiscFiles& files) {
            done = files.setAttributes(*file, readOnly, system);
            return true;
        });
    fillFiles();
    return done;
}

QStringList DiscEditorDialog::selectedFiles() const
{
    QStringList names;
    for (const QTreeWidgetItem* item : files_->selectedItems())
        names << item->text(0);
    return names;
}

void DiscEditorDialog::addClicked()
{
    for (const QString& path : QFileDialog::getOpenFileNames(this, tr("Add Files"))) {
        QString error;
        if (!addFile(path, &error)) {
            QMessageBox::warning(this, windowTitle(), error);
            break;
        }
    }
}

void DiscEditorDialog::extractClicked()
{
    const QStringList names = selectedFiles();
    if (names.isEmpty())
        return;
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Extract To"));
    if (folder.isEmpty())
        return;
    for (const QString& name : names) {
        QString error;
        if (!extractFile(name, folder + '/' + name, &error)) {
            QMessageBox::warning(this, windowTitle(), error);
            break;
        }
    }
}

void DiscEditorDialog::renameClicked()
{
    const QStringList names = selectedFiles();
    if (names.size() != 1)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal, names[0], &ok);
    if (ok && !renameFile(names[0], name))
        QMessageBox::warning(this, windowTitle(), tr("%1 cannot be renamed to %2.").arg(names[0], name));
}

void DiscEditorDialog::deleteClicked()
{
    const QStringList names = selectedFiles();
    if (names.isEmpty()
        || QMessageBox::question(this, windowTitle(), tr("Delete %n file(s)?", nullptr, static_cast<int>(names.size()))) != QMessageBox::Yes)
        return;
    for (const QString& name : names)
        deleteFile(name);
}

// The file's name and its two attributes.
void DiscEditorDialog::propertiesClicked()
{
    const QStringList names = selectedFiles();
    const auto file = names.size() == 1 ? fileNamed(names[0]) : std::nullopt;
    if (!file)
        return;
    QDialog dialog(this);
    dialog.setObjectName("frmFileProperties");
    dialog.setWindowTitle(tr("File Properties"));
    auto* name = new QLineEdit(names[0]);
    name->setObjectName("edName");
    auto* readOnly = new QCheckBox(tr("Read-only"));
    readOnly->setObjectName("ckReadOnly");
    readOnly->setChecked(file->readOnly);
    auto* system = new QCheckBox(tr("System (Hidden)"));
    system->setObjectName("ckSystem");
    system->setChecked(file->system);
    auto* ok = new QPushButton(tr("OK"));
    auto* cancel = new QPushButton(tr("Cancel"));
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(name);
    layout->addWidget(new QLabel(tr("%1 bytes, user %2").arg(file->size).arg(file->user)));
    layout->addWidget(readOnly);
    layout->addWidget(system);
    layout->addLayout(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QString current = names[0];
    if (name->text() != current && renameFile(current, name->text()))
        current = QString::fromStdString(DiscFiles::directoryName(name->text().toStdString()).value_or(current.toStdString()));
    setFileAttributes(current, readOnly->isChecked(), system->isChecked());
}

// ---- sectors -----------------------------------------------------------------

void DiscEditorDialog::fillSectors()
{
    QStringList names;
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        const tuxape::Disc* disc = cpc.fdc().drive(drive_).disc.get();
        const tuxape::DiscTrack* track = disc ? disc->track(track_->value(), side_->value()) : nullptr;
        if (track)
            for (const tuxape::DiscSector& sector : track->sectors)
                names << hex(sector.r, 2);
    });
    {
        const QSignalBlocker blocker(sector_);
        sector_->clear();
        sector_->addItems(names);
    }
    showSector();
}

QStringList DiscEditorDialog::sectorNames() const
{
    QStringList names;
    for (int i = 0; i < sector_->count(); ++i)
        names << sector_->itemText(i);
    return names;
}

void DiscEditorDialog::showSector()
{
    const int index = sector_->currentIndex();
    QString info = tr("No sector");
    int size = 0;
    sectorBytes_.fill(0);
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        const tuxape::Disc* disc = cpc.fdc().drive(drive_).disc.get();
        const tuxape::DiscTrack* track = disc ? disc->track(track_->value(), side_->value()) : nullptr;
        if (!track || index < 0 || index >= static_cast<int>(track->sectors.size()))
            return;
        const tuxape::DiscSector& sector = track->sectors[static_cast<size_t>(index)];
        size = static_cast<int>(std::min<size_t>(sector.data.size(), sectorBytes_.size()));
        std::copy_n(sector.data.begin(), size, sectorBytes_.begin());
        // The four bytes of its ID, its size, and what the controller
        // would say of it.
        info = QStringLiteral("(%1 %2 %3 %4)  %5 bytes").arg(hex(sector.c, 2), hex(sector.h, 2), hex(sector.r, 2), hex(sector.n, 2)).arg(size);
        if (sector.deleted())
            info += tr("  Deleted Data");
        if (sector.st1 & 0x20)
            info += sector.st2 & 0x20 ? tr("  Data Error") : tr("  ID Error");
    });
    sectorInfo_->setText(info);
    sectorView_->setLimit(std::max(size, 16));
    sectorView_->viewport()->update();
}

bool DiscEditorDialog::selectSector(int track, int side, int index)
{
    if (track < track_->minimum() || track > track_->maximum() || side < side_->minimum() || side > side_->maximum())
        return false;
    track_->setValue(track);
    side_->setValue(side);
    fillSectors();
    if (index < 0 || index >= sector_->count())
        return false;
    sector_->setCurrentIndex(index);
    showSector();
    return true;
}

QString DiscEditorDialog::sectorInfo() const
{
    return sectorInfo_->text();
}
