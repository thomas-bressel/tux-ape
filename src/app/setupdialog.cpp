#include "setupdialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/setup.h"
#include "core/version.h"

namespace {

// A control of WinAPE's that TuxAPE has no use for yet.
template <class Widget>
Widget* notYet(Widget* widget)
{
    widget->setEnabled(false);
    widget->setToolTip(SetupDialog::tr("Not available yet"));
    return widget;
}

QString versionInformation()
{
    return SetupDialog::tr(
               "<h3>TuxAPE - What's New</h3>"
               "<p><b>Version %1</b></p>"
               "<p>TuxAPE is a free replica of the WinAPE 2.0 Beta 2 Amstrad CPC emulator. It is unfinished: "
               "the menu entries and settings shown greyed out are still to come.</p>"
               "<p><b><u>Credits</u></b></p>"
               "<p>WinAPE, whose look and behaviour TuxAPE reproduces, is the work of Richard Wilson.</p>"
               "<p>Technical information from the \"Amstrad CPC CRTC Compendium\" by Longshot / Logon System "
               "(CC BY-NC-ND).</p>"
               "<p>The emulation is checked with the test programs Kevin Thacker wrote for the Arnold emulator "
               "and with the \"Shaker\" by Longshot / Logon System, whose results on real machines are "
               "published at shaker.logonsystem.eu.</p>")
        .arg(QString::fromLatin1(tuxape::versionString()));
}

// The list of ROMs keeps each name in the item's UserRole and shows it, or
// the words WinAPE has for an empty place. The cell is edited with a combo
// box offering the images of the ROM folder and, last, a file of the
// user's choosing.
class RomDelegate : public QStyledItemDelegate {
public:
    explicit RomDelegate(SetupDialog* dialog)
        : QStyledItemDelegate(dialog)
        , dialog_(dialog)
    {
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        const int row = index.row();
        const QString current = dialog_->rom(row);
        auto* combo = new QComboBox(parent);
        combo->addItem(SetupDialog::emptyRomText(row), QString());
        bool listed = current.isEmpty();
        for (const std::string& name : tuxape::romNames(tuxape::defaultRomDir())) {
            const QString text = QString::fromStdString(name);
            combo->addItem(text, text);
            listed = listed || text.compare(current, Qt::CaseInsensitive) == 0;
        }
        if (!listed)
            combo->addItem(QDir::toNativeSeparators(current), current);
        combo->addItem(SetupDialog::tr("Select File..."), kSelectFile);
        // The choice is taken as soon as it is made.
        connect(combo, &QComboBox::activated, this, [this, combo, row](int choice) {
            QString name = combo->itemData(choice).toString();
            if (name == kSelectFile) {
                name = QFileDialog::getOpenFileName(dialog_, SetupDialog::tr("Select File..."),
                                                    QString::fromStdString(tuxape::defaultRomDir().string()),
                                                    SetupDialog::tr("ROM files (*.rom *.ROM);;All files (*)"));
                if (name.isEmpty())
                    name = dialog_->rom(row);  // cancelled: as it was
            }
            dialog_->setRom(row, name);
            emit const_cast<RomDelegate*>(this)->closeEditor(combo);
        });
        return combo;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* combo = static_cast<QComboBox*>(editor);
        const QString current = dialog_->rom(index.row());
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemData(i).toString().compare(current, Qt::CaseInsensitive) == 0) {
                combo->setCurrentIndex(i);
                break;
            }
    }

    // The combo box has already said what it had to say.
    void setModelData(QWidget*, QAbstractItemModel*, const QModelIndex&) const override {}

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const QString name = dialog_->rom(index.row());
        option->text = name.isEmpty() ? SetupDialog::emptyRomText(index.row()) : QDir::toNativeSeparators(name);
    }

private:
    static inline const QString kSelectFile = QStringLiteral("<select file>");
    SetupDialog* dialog_;
};

}  // namespace

SetupDialog::SetupDialog(const Settings& settings, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("TuxAPE - Setup"));

    auto* profileRow = new QHBoxLayout;
    auto* profileLabel = new QLabel(tr("&Profile:"));
    profile_ = new QComboBox;
    profile_->setObjectName("cbProfile");
    fillProfiles();
    profileLabel->setBuddy(profile_);
    connect(profile_, &QComboBox::activated, this, &SetupDialog::profileChosen);
    auto* saveProfile = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save"));
    saveProfile->setObjectName("bSaveProfile");
    connect(saveProfile, &QPushButton::clicked, this, &SetupDialog::saveProfileAs);
    profileRow->addWidget(profileLabel);
    profileRow->addWidget(profile_, 1);
    profileRow->addSpacing(40);
    profileRow->addWidget(saveProfile);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("PageControl");
    // Pages that are still to come are empty and cannot be chosen.
    auto addEmptyPage = [this](const QString& name) {
        const int index = tabs_->addTab(new QWidget, name);
        tabs_->setTabEnabled(index, false);
        tabs_->setTabToolTip(index, tr("Not available yet"));
    };
    tabs_->addTab(createGeneralPage(), tr("General"));
    addEmptyPage(tr("Display"));
    addEmptyPage(tr("Sound"));
    tabs_->addTab(createMemoryPage(), tr("Memory"));
    addEmptyPage(tr("Input"));
    addEmptyPage(tr("Other"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    notYet(buttons->button(QDialogButtonBox::Help));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(profileRow);
    layout->addWidget(tabs_, 1);
    layout->addWidget(buttons);
    resize(640, 400);

    setSettings(settings);
}

QWidget* SetupDialog::createGeneralPage()
{
    auto* page = new QWidget;

    // ---- Version Information ----
    auto* versionBox = new QGroupBox(tr("Version Information"));
    auto* version = new QTextBrowser;
    version->setObjectName("reVersion");
    version->setOpenExternalLinks(false);
    version->setHtml(versionInformation());
    auto* versionLayout = new QVBoxLayout(versionBox);
    versionLayout->addWidget(version);

    // ---- Options ----
    auto* optionsBox = new QGroupBox(tr("Options"));
    auto* crtcLabel = new QLabel(tr("CRTC &Type:"));
    crtcType_ = new QComboBox;
    crtcType_->setObjectName("cbCRTCType");
    crtcType_->addItems({tr("0 - HD6845S/UM6845"), tr("1 - UM6845R"), tr("2 - MC6845"), tr("3 - CPC+ ASIC"),
                         tr("4 - Pre ASIC")});
    crtcLabel->setBuddy(crtcType_);
    auto* crtcRow = new QHBoxLayout;
    crtcRow->addWidget(crtcLabel);
    crtcRow->addWidget(crtcType_, 1);

    auto* enablePlus = notYet(new QCheckBox(tr("Enable Plus Features")));
    enablePlus->setObjectName("ckEnablePlus");
    auto* plusPpi = notYet(new QCheckBox(tr("Plus PPI Emulation")));
    plusPpi->setObjectName("ckPlusPPI");
    auto* fourDrives = notYet(new QCheckBox(tr("Enable Four Drives (Non-Standard)")));
    fourDrives->setObjectName("ckFourDrives");
    fastDisc_ = new QCheckBox(tr("&Fast Disc Emulation"));
    fastDisc_->setObjectName("ckFastDisc");
    auto* flyback = notYet(new QCheckBox(tr("Save Screenshot on Frame Flyback")));
    flyback->setObjectName("ckFlyback");
    flyback->setChecked(true);
    // TuxAPE never looks for updates by itself.
    auto* disableUpdate = notYet(new QCheckBox(tr("Disable Automatic Update")));
    disableUpdate->setObjectName("ckDisableUpdate");
    disableUpdate->setChecked(true);
    auto* update = notYet(new QPushButton(tr("Check for updates now")));
    update->setObjectName("bUpdate");

    auto* optionsLayout = new QVBoxLayout(optionsBox);
    optionsLayout->setSpacing(3);
    optionsLayout->addLayout(crtcRow);
    for (QWidget* widget : {static_cast<QWidget*>(enablePlus), static_cast<QWidget*>(plusPpi),
                            static_cast<QWidget*>(fourDrives), static_cast<QWidget*>(fastDisc_),
                            static_cast<QWidget*>(flyback), static_cast<QWidget*>(disableUpdate)})
        optionsLayout->addWidget(widget);
    optionsLayout->addWidget(update, 0, Qt::AlignLeft);

    // ---- Timing ----
    auto* timingBox = new QGroupBox(tr("Timing"));
    speed_ = new QSlider(Qt::Horizontal);
    speed_->setObjectName("slSpeed");
    speed_->setRange(5, 1000);
    speed_->setPageStep(50);
    speedLabel_ = new QLabel;
    speedLabel_->setObjectName("lbSpeed");
    speedLabel_->setMinimumWidth(fontMetrics().horizontalAdvance("1000%"));
    speedLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(speed_, 1);
    speedRow->addWidget(speedLabel_);

    displayEvery_ = new QCheckBox(tr("&Display Every"));
    displayEvery_->setObjectName("ckUseRate");
    displayEveryFrames_ = new QSpinBox;
    displayEveryFrames_->setObjectName("edRate");
    displayEveryFrames_->setRange(1, 50);
    auto* everyRow = new QHBoxLayout;
    everyRow->addWidget(displayEvery_);
    everyRow->addWidget(displayEveryFrames_);
    everyRow->addWidget(new QLabel(tr("frame(s)")));
    everyRow->addStretch(1);

    auto* turbo = notYet(new QCheckBox(tr("Turbo Mode")));
    turbo->setObjectName("ckTurbo");

    auto* timingLayout = new QVBoxLayout(timingBox);
    timingLayout->setSpacing(3);
    timingLayout->addLayout(speedRow);
    timingLayout->addLayout(everyRow);
    timingLayout->addWidget(turbo);

    connect(speed_, &QSlider::valueChanged, this, &SetupDialog::updateTiming);
    connect(displayEvery_, &QCheckBox::toggled, this, &SetupDialog::updateTiming);
    updateTiming();

    auto* right = new QVBoxLayout;
    right->addWidget(optionsBox);
    right->addWidget(timingBox);
    right->addStretch(1);

    auto* layout = new QHBoxLayout(page);
    layout->addWidget(versionBox, 1);
    layout->addLayout(right);
    return page;
}

QWidget* SetupDialog::createMemoryPage()
{
    auto* page = new QWidget;

    // ---- RAM ----
    auto* ramBox = new QGroupBox(tr("RAM"));
    const char* const ramNames[4] = {"rb64K", "rb128K", "rb256K", "rb4M"};
    const QString ramTexts[4] = {tr("64K"), tr("128K"), tr("64K + 256K RAM Expansion"), tr("4M Expansion")};
    auto* ramLayout = new QVBoxLayout(ramBox);
    ramLayout->setSpacing(3);
    for (int i = 0; i < 4; ++i) {
        ram_[i] = new QRadioButton(ramTexts[i]);
        ram_[i]->setObjectName(ramNames[i]);
        connect(ram_[i], &QRadioButton::toggled, this, &SetupDialog::updateTotalRam);
        ramLayout->addWidget(ram_[i]);
    }
    siliconDisc_ = new QCheckBox(tr("256K Silicon Disc"));
    siliconDisc_->setObjectName("ckSiliDisc");
    connect(siliconDisc_, &QCheckBox::toggled, this, &SetupDialog::updateTotalRam);
    ramLayout->addWidget(siliconDisc_);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    ramLayout->addWidget(line);
    totalRam_ = new QLabel;
    totalRam_->setObjectName("lTotalRAM");
    auto* totalRow = new QHBoxLayout;
    totalRow->addWidget(new QLabel(tr("Total:")));
    totalRow->addWidget(totalRam_);
    totalRow->addStretch(1);
    ramLayout->addLayout(totalRow);

    // ---- Cartridge and Multiface: to come ----
    auto fileRow = [](const QString& label, const QString& file, const char* buttonName) {
        auto* row = new QHBoxLayout;
        row->addWidget(notYet(new QLabel(label)));
        row->addWidget(notYet(new QLabel(file)), 1);
        auto* browse = notYet(new QToolButton);
        browse->setObjectName(buttonName);
        browse->setText("...");
        row->addWidget(browse);
        return row;
    };
    auto* cartridgeBox = new QGroupBox(tr("Cartridge"));
    auto* enableCartridge = notYet(new QCheckBox(tr("Enable Cartridge")));
    enableCartridge->setObjectName("ckEnableCart");
    auto* cartridgeLayout = new QVBoxLayout(cartridgeBox);
    cartridgeLayout->setSpacing(3);
    cartridgeLayout->addWidget(enableCartridge);
    cartridgeLayout->addLayout(fileRow(tr("File:"), QString(), "sbCartridge"));

    auto* multifaceBox = new QGroupBox(tr("Multiface"));
    auto* enableMultiface = notYet(new QCheckBox(tr("Enable Multiface")));
    enableMultiface->setObjectName("ckEnableMultiface");
    auto* multifaceLayout = new QVBoxLayout(multifaceBox);
    multifaceLayout->setSpacing(3);
    multifaceLayout->addWidget(enableMultiface);
    multifaceLayout->addLayout(fileRow(tr("ROM:"), QString(), "sbMultiface"));

    // ---- ROMs ----
    auto* romsBox = new QGroupBox(tr("ROMs"));
    roms_ = new QTableWidget(0, 2);
    roms_->setObjectName("ogROMs");
    roms_->horizontalHeader()->hide();
    roms_->verticalHeader()->hide();
    roms_->horizontalHeader()->setStretchLastSection(true);
    roms_->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 6);
    roms_->setColumnWidth(0, fontMetrics().horizontalAdvance("Upper 00") + 16);
    roms_->setShowGrid(false);
    roms_->setSelectionBehavior(QAbstractItemView::SelectRows);
    roms_->setSelectionMode(QAbstractItemView::SingleSelection);
    roms_->setEditTriggers(QAbstractItemView::AllEditTriggers);
    roms_->setItemDelegateForColumn(1, new RomDelegate(this));

    rom32_ = new QCheckBox(tr("Enable 32 ROMs"));
    rom32_->setObjectName("ckEnable32");
    disableRoms_ = new QCheckBox(tr("Disable all ROMS"));
    disableRoms_->setObjectName("ckDisableROMs");
    onlyLower0And7_ = new QCheckBox(tr("Disable all but Lower, 0 and 7"));
    onlyLower0And7_->setObjectName("ckEnableL07");
    connect(rom32_, &QCheckBox::toggled, this, &SetupDialog::updateRomRows);
    auto* romChecks = new QHBoxLayout;
    romChecks->addWidget(rom32_);
    romChecks->addWidget(disableRoms_);
    romChecks->addWidget(onlyLower0And7_);
    romChecks->addStretch(1);
    auto* romsLayout = new QVBoxLayout(romsBox);
    romsLayout->addWidget(roms_, 1);
    romsLayout->addLayout(romChecks);

    updateTotalRam();
    updateRomRows();

    auto* left = new QVBoxLayout;
    left->addWidget(ramBox);
    left->addWidget(cartridgeBox);
    left->addWidget(multifaceBox);
    left->addStretch(1);
    auto* layout = new QHBoxLayout(page);
    layout->addLayout(left);
    layout->addWidget(romsBox, 1);
    return page;
}

tuxape::RamExpansion SetupDialog::chosenRam() const
{
    for (int i = 0; i < 4; ++i)
        if (ram_[i]->isChecked())
            return static_cast<tuxape::RamExpansion>(i);
    return tuxape::RamExpansion::None;
}

void SetupDialog::updateTotalRam()
{
    totalRam_->setText(tr("%1K").arg(tuxape::Memory::ramSizeKb(chosenRam(), siliconDisc_->isChecked())));
}

// One row for the firmware ROM and one for each slot of the ROM board.
void SetupDialog::updateRomRows()
{
    const int rows = 1 + (rom32_->isChecked() ? 32 : 16);
    const int before = roms_->rowCount();
    roms_->setRowCount(rows);
    for (int row = before; row < rows; ++row) {
        auto* slot = new QTableWidgetItem(row == 0 ? tr("Lower") : tr("Upper %1").arg(row - 1));
        slot->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        roms_->setItem(row, 0, slot);
        roms_->setItem(row, 1, new QTableWidgetItem);
    }
}

QString SetupDialog::emptyRomText(int row)
{
    // With a cartridge WinAPE names the cartridge bank that shows instead;
    // TuxAPE has no cartridges yet.
    return row <= 1 ? tr("(Empty)") : tr("(Empty - Same as Upper 0)");
}

QString SetupDialog::rom(int row) const
{
    return row >= 0 && row <= tuxape::Memory::kRomSlots ? romNames_[row] : QString();
}

void SetupDialog::setRom(int row, const QString& name)
{
    if (row < 0 || row > tuxape::Memory::kRomSlots)
        return;
    // An image of the ROM folder goes by its bare name, as in WinAPE.ini.
    const QFileInfo file(name);
    const QString romDir = QDir::cleanPath(QString::fromStdString(tuxape::defaultRomDir().string()));
    const bool inRomDir = file.isAbsolute() && QDir::cleanPath(file.absolutePath()) == romDir
                          && file.suffix().compare("rom", Qt::CaseInsensitive) == 0;
    romNames_[row] = inRomDir ? file.completeBaseName() : name;
    if (roms_ && row < roms_->rowCount())
        roms_->viewport()->update();
}

// The speed slider or the frame count: whichever "Display Every" leaves in
// charge is the one that can be changed.
void SetupDialog::updateTiming()
{
    speedLabel_->setText(tr("%1%").arg(speed_->value()));
    const bool every = displayEvery_->isChecked();
    speed_->setEnabled(!every);
    speedLabel_->setEnabled(!every);
    displayEveryFrames_->setEnabled(every);
}

void SetupDialog::setSettings(const Settings& settings)
{
    crtcType_->setCurrentIndex(settings.crtcType);
    fastDisc_->setChecked(settings.fastDisc);
    speed_->setValue(settings.speedPercent);
    displayEvery_->setChecked(settings.displayEvery);
    displayEveryFrames_->setValue(settings.displayEveryFrames);
    updateTiming();

    const tuxape::MachineConfig& machine = settings.machine;
    ram_[static_cast<int>(machine.ram)]->setChecked(true);
    siliconDisc_->setChecked(machine.siliconDisc);
    romNames_[0] = QString::fromStdString(machine.lowerRom);
    for (int slot = 0; slot < tuxape::Memory::kRomSlots; ++slot)
        romNames_[1 + slot] = QString::fromStdString(machine.upperRoms[static_cast<size_t>(slot)]);
    rom32_->setChecked(machine.rom32);
    disableRoms_->setChecked(machine.disableAllRoms);
    onlyLower0And7_->setChecked(machine.onlyLower0And7);
    updateTotalRam();
    updateRomRows();
    roms_->viewport()->update();
}

// ---- Profiles ---------------------------------------------------------

namespace {

const QString kSelectProfile = QStringLiteral("<select file>");

}  // namespace

ProfilePartsDialog::ProfilePartsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Profile Settings"));
    tree_ = new QTreeWidget;
    tree_->setObjectName("tvSettings");
    tree_->setHeaderHidden(true);

    auto group = [this](const QString& name) {
        auto* item = new QTreeWidgetItem(tree_, {name});
        // Ticking a group ticks what is in it.
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        return item;
    };
    auto leaf = [](QTreeWidgetItem* parent, const QString& name, unsigned part, bool ticked) {
        auto* item = new QTreeWidgetItem(parent, {name});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(0, Qt::UserRole, part);
        item->setCheckState(0, ticked ? Qt::Checked : Qt::Unchecked);
    };
    // A profile is first of all a machine: that is what is ticked to begin
    // with.
    auto* general = group(tr("General"));
    leaf(general, tr("CRTC Type"), Settings::CrtcPart, true);
    leaf(general, tr("Emulation Speed"), Settings::SpeedPart, false);
    leaf(general, tr("Fast Disc Emulation"), Settings::FastDiscPart, false);
    auto* memory = group(tr("Memory"));
    leaf(memory, tr("RAM"), Settings::RamPart, true);
    leaf(memory, tr("ROMs"), Settings::RomsPart, true);
    tree_->expandAll();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    notYet(buttons->button(QDialogButtonBox::Help));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tree_, 1);
    layout->addWidget(buttons);
    resize(300, 290);
}

unsigned ProfilePartsDialog::parts() const
{
    unsigned parts = 0;
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        const QTreeWidgetItem* top = tree_->topLevelItem(g);
        for (int i = 0; i < top->childCount(); ++i)
            if (top->child(i)->checkState(0) == Qt::Checked)
                parts |= top->child(i)->data(0, Qt::UserRole).toUInt();
    }
    return parts;
}

// "(Current Settings)", the profiles that come with the program and the
// user's own, then a file of the user's choosing. Profiles for machines
// TuxAPE cannot be yet are listed but cannot be chosen.
void SetupDialog::fillProfiles()
{
    profile_->clear();
    profile_->addItem(tr("(Current Settings)"), QString());
    // In the order of their names, not of their file names: "464 Plus"
    // comes before "464 Plus with ParaDOS".
    QFileInfoList files = QDir(Settings::profileFolder()).entryInfoList({"*.wpf"}, QDir::Files);
    files += QDir(Settings::userProfileFolder()).entryInfoList({"*.wpf"}, QDir::Files);
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.completeBaseName().compare(b.completeBaseName(), Qt::CaseInsensitive) < 0;
    });
    for (const QFileInfo& file : files) {
        profile_->addItem(file.completeBaseName(), file.absoluteFilePath());
        if (!Settings::profileUsable(file.absoluteFilePath())) {
            const int index = profile_->count() - 1;
            profile_->setItemData(index, 0, Qt::UserRole - 1);  // no flags: greyed out
            profile_->setItemData(index, tr("Not available yet"), Qt::ToolTipRole);
        }
    }
    profile_->addItem(tr("Select File..."), kSelectProfile);
}

void SetupDialog::profileChosen(int index)
{
    QString path = profile_->itemData(index).toString();
    if (path == kSelectProfile) {
        path = QFileDialog::getOpenFileName(this, tr("Select File..."), Settings::profileFolder(),
                                            tr("Profiles (*.wpf);;All files (*)"));
        if (path.isEmpty() || !loadProfile(path)) {
            profile_->setCurrentIndex(0);
            return;
        }
        // Listed from now on, just above "Select File...".
        const int place = profile_->count() - 1;
        profile_->insertItem(place, QDir::toNativeSeparators(path), path);
        profile_->setCurrentIndex(place);
    } else if (!path.isEmpty() && !loadProfile(path)) {
        profile_->setCurrentIndex(0);
    }
}

bool SetupDialog::loadProfile(const QString& path)
{
    Settings changed = settings();
    if (!changed.loadProfile(path))
        return false;
    setSettings(changed);
    return true;
}

bool SetupDialog::saveProfile(const QString& path, unsigned parts) const
{
    return settings().saveProfile(path, parts);
}

// The Save button: which settings, then which file.
void SetupDialog::saveProfileAs()
{
    ProfilePartsDialog partsDialog(this);
    if (partsDialog.exec() != QDialog::Accepted)
        return;
    // The user's profiles go beside the settings file: the folder of the
    // program's own may not be the user's to write in.
    QDir().mkpath(Settings::userProfileFolder());
    QString path = QFileDialog::getSaveFileName(this, tr("Save Profile"), Settings::userProfileFolder(),
                                                tr("Profiles (*.wpf);;All files (*)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".wpf";
    if (saveProfile(path, partsDialog.parts())) {
        const QString chosen = QFileInfo(path).absoluteFilePath();
        fillProfiles();
        int index = profile_->findData(chosen);
        if (index < 0) {
            index = profile_->count() - 1;
            profile_->insertItem(index, QDir::toNativeSeparators(chosen), chosen);
        }
        profile_->setCurrentIndex(index);
    }
}

void SetupDialog::showPage(Page page)
{
    if (tabs_->isTabEnabled(page))
        tabs_->setCurrentIndex(page);
}

Settings SetupDialog::settings() const
{
    Settings settings;
    settings.crtcType = crtcType_->currentIndex();
    settings.fastDisc = fastDisc_->isChecked();
    settings.speedPercent = speed_->value();
    settings.displayEvery = displayEvery_->isChecked();
    settings.displayEveryFrames = displayEveryFrames_->value();

    tuxape::MachineConfig& machine = settings.machine;
    machine.ram = chosenRam();
    machine.siliconDisc = siliconDisc_->isChecked();
    machine.lowerRom = romNames_[0].toStdString();
    for (int slot = 0; slot < tuxape::Memory::kRomSlots; ++slot)
        machine.upperRoms[static_cast<size_t>(slot)] = romNames_[1 + slot].toStdString();
    machine.rom32 = rom32_->isChecked();
    machine.disableAllRoms = disableRoms_->isChecked();
    machine.onlyLower0And7 = onlyLower0And7_->isChecked();
    return settings;
}
