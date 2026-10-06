#include "setupdialog.h"

#include "help.h"

#include <algorithm>

#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
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

#include "core/files.h"
#include "core/gate_array.h"
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
    , opened_(settings)
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
    tabs_->addTab(createGeneralPage(), tr("General"));
    tabs_->addTab(createDisplayPage(), tr("Display"));
    tabs_->addTab(createSoundPage(), tr("Sound"));
    tabs_->addTab(createMemoryPage(), tr("Memory"));
    tabs_->addTab(createInputPage(), tr("Input"));
    tabs_->addTab(createOtherPage(), tr("Other"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    wireHelp(buttons->button(QDialogButtonBox::Help));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // The keyboard layout's Load and Save sit beside OK, on the Input page
    // only.
    loadKeys_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Load"));
    loadKeys_->setObjectName("bLoadKeys");
    saveKeys_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save"));
    saveKeys_->setObjectName("bSaveKeys");
    connect(loadKeys_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Load Keyboard Layout"), keyboardFile_,
                                                          tr("Keyboard layouts (*.kbd);;All files (*)"));
        if (!path.isEmpty() && !loadKeyboard(path))
            QMessageBox::warning(this, windowTitle(), tr("%1 is not a keyboard layout.").arg(QDir::toNativeSeparators(path)));
    });
    connect(saveKeys_, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, tr("Save Keyboard Layout"), keyboardFile_,
                                                    tr("Keyboard layouts (*.kbd);;All files (*)"));
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".kbd";
        if (!saveKeyboard(path))
            QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
    });
    auto* bottomRow = new QHBoxLayout;
    bottomRow->addWidget(loadKeys_);
    bottomRow->addWidget(saveKeys_);
    bottomRow->addStretch(1);
    bottomRow->addWidget(buttons);
    auto showKeyButtons = [this](int page) {
        loadKeys_->setVisible(page == Input);
        saveKeys_->setVisible(page == Input);
    };
    connect(tabs_, &QTabWidget::currentChanged, this, showKeyButtons);
    showKeyButtons(tabs_->currentIndex());

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(profileRow);
    layout->addWidget(tabs_, 1);
    layout->addLayout(bottomRow);
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

    enablePlus_ = new QCheckBox(tr("Enable Plus Features"));
    enablePlus_->setObjectName("ckEnablePlus");
    enablePlus_->setToolTip(tr("Needs a cartridge, on the Memory page"));
    plusPpi_ = new QCheckBox(tr("Plus PPI Emulation"));
    plusPpi_->setObjectName("ckPlusPPI");
    auto* plusPpi = plusPpi_;
    fourDrives_ = new QCheckBox(tr("Enable Four Drives (Non-Standard)"));
    fourDrives_->setObjectName("ckFourDrives");
    fourDrives_->setToolTip(tr("Drives C: and D:, for CP/M Plus and the few programs that know of them"));
    auto* fourDrives = fourDrives_;
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
    // TuxAPE's own: its picture as it starts, for those who want it.
    welcome_ = new QCheckBox(tr("Show the welcome picture at start-up"));
    welcome_->setObjectName("ckWelcome");

    auto* optionsLayout = new QVBoxLayout(optionsBox);
    optionsLayout->setSpacing(3);
    optionsLayout->addLayout(crtcRow);
    for (QWidget* widget : {static_cast<QWidget*>(enablePlus_), static_cast<QWidget*>(plusPpi),
                            static_cast<QWidget*>(fourDrives), static_cast<QWidget*>(fastDisc_),
                            static_cast<QWidget*>(flyback), static_cast<QWidget*>(disableUpdate),
                            static_cast<QWidget*>(welcome_)})
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

    turbo_ = new QCheckBox(tr("Turbo Mode"));
    turbo_->setObjectName("ckTurbo");
    turbo_->setToolTip(tr("Every instruction takes a microsecond: faster, and unlike a real CPC"));
    auto* turbo = turbo_;

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

QWidget* SetupDialog::createDisplayPage()
{
    auto* page = new QWidget;

    // ---- The monitor and a piece of its picture ----
    const char* const monitorNames[3] = {"rbColour", "rbGreen", "rbGreyscale"};
    const QString monitorTexts[3] = {tr("Colour"), tr("Green"), tr("Greyscale")};
    auto* monitorRow = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        monitor_[i] = new QRadioButton(monitorTexts[i]);
        monitor_[i]->setObjectName(monitorNames[i]);
        connect(monitor_[i], &QRadioButton::toggled, this, &SetupDialog::updatePreview);
        monitorRow->addWidget(monitor_[i]);
    }
    monitorRow->addStretch(1);
    preview_ = new QLabel;
    preview_->setObjectName("iScreen");
    preview_->setFixedSize(300, 216);
    preview_->setFrameShape(QFrame::Panel);
    preview_->setFrameShadow(QFrame::Sunken);
    preview_->setStyleSheet("background-color: black;");
    auto* left = new QVBoxLayout;
    left->addLayout(monitorRow);
    left->addWidget(preview_);
    left->addStretch(1);

    // ---- V Hold ----
    auto* holdBox = new QGroupBox(tr("V Hold"));
    holdBox->setObjectName("gbVHold");
    verticalHold_ = new QSlider(Qt::Vertical);
    verticalHold_->setObjectName("slVSync");
    verticalHold_->setRange(-32, 32);
    verticalHoldLabel_ = new QLabel;
    verticalHoldLabel_->setObjectName("lVSync");
    verticalHoldLabel_->setAlignment(Qt::AlignHCenter);
    connect(verticalHold_, &QSlider::valueChanged, this,
            [this](int value) { verticalHoldLabel_->setText(QString::number(value)); });
    auto* holdLayout = new QVBoxLayout(holdBox);
    holdLayout->addWidget(verticalHold_, 1, Qt::AlignHCenter);
    holdLayout->addWidget(verticalHoldLabel_);

    // ---- What the window shows, for the window and for full screen ----
    const QString optionTexts[6] = {tr("Half Size Display"), tr("Render both lines"), tr("Hide Mouse Pointer"),
                                    tr("Hide Control Panel"), tr("Hide Menus"),       tr("No Right-Click Menu")};
    const char* const optionNames[6] = {"ckHalfSize", "ckRenderBoth", "ckHideMouse",
                                        "ckHidePanel", "ckHideMenus", "ckNoRightClick"};
    QGroupBox* optionBoxes[2] = {new QGroupBox(tr("Windowed")), new QGroupBox(tr("Full Screen"))};
    optionBoxes[0]->setObjectName("gbWindowed");
    optionBoxes[1]->setObjectName("gbFullScreen");
    for (int mode = 0; mode < 2; ++mode) {
        auto* layout = new QVBoxLayout(optionBoxes[mode]);
        layout->setSpacing(2);
        for (int i = 0; i < 6; ++i) {
            windowOptions_[mode][i] = new QCheckBox(optionTexts[i]);
            windowOptions_[mode][i]->setObjectName(QString::fromLatin1(optionNames[i]) + (mode ? "FS" : ""));
            layout->addWidget(windowOptions_[mode][i]);
        }
        if (mode == 0) {
            // Stretching is the window system's business here.
            auto* stretch = notYet(new QCheckBox(tr("DirectX Stretch")));
            stretch->setObjectName("ckDXStretch");
            layout->addWidget(stretch);
        }
        layout->addStretch(1);
    }

    // ---- Shared ----
    auto* sharedBox = new QGroupBox(tr("Shared"));
    pal_ = new QCheckBox(tr("PAL Emulation"));
    pal_->setObjectName("ckPAL");
    // TuxAPE's own.
    crtShader_ = new QCheckBox(tr("CTM644 Monitor Shader"));
    crtShader_->setObjectName("ckCrtShader");
    crtShader_->setToolTip(tr("The picture as an Amstrad CTM644 shows it: scan lines, the tube's mask, the glow of bright "
                              "areas and the curve of the glass. Needs a graphics card; best in full screen."));
    linearPalette_ = new QCheckBox(tr("Linear Palette"));
    linearPalette_->setObjectName("ckLinearPalette");
    connect(linearPalette_, &QCheckBox::toggled, this, &SetupDialog::updatePreview);
    driveLed_ = new QCheckBox(tr("On-Screen Drive LED"));
    driveLed_->setObjectName("ckDriveLED");
    showTrack_ = new QCheckBox(tr("Show Drive Cylinders"));
    showTrack_->setObjectName("ckShowTrack");
    auto* sharedLayout = new QVBoxLayout(sharedBox);
    sharedLayout->setSpacing(2);
    for (QCheckBox* box : {pal_, linearPalette_, driveLed_, showTrack_})
        sharedLayout->addWidget(box);

    // ---- The CTM644 shader: its box, and sliders to set it by hand, each
    // a percentage of TuxAPE's own setting ----
    auto* shaderBox = new QGroupBox(tr("CTM644 Monitor Shader"));
    shaderBox->setObjectName("gbCrtShader");
    auto* shaderGrid = new QGridLayout(shaderBox);
    shaderGrid->setVerticalSpacing(2);
    crtShader_->setText(tr("Enabled"));
    auto* defaults = new QPushButton(tr("Defaults"));
    defaults->setObjectName("bCrtDefaults");
    defaults->setAutoDefault(false);
    defaults->setToolTip(tr("Puts the six sliders back to TuxAPE's own setting"));
    shaderGrid->addWidget(crtShader_, 0, 0, 1, 3);
    shaderGrid->addWidget(defaults, 0, 4, 1, 2, Qt::AlignRight);
    const struct {
        const char* name;
        QString text;
        QString tip;
    } kSliders[6] = {{"slCrtCurvature", tr("Curvature:"), tr("The curve of the glass")},
                     {"slCrtScanLines", tr("Scan lines:"), tr("How much the scan lines show")},
                     {"slCrtMask", tr("Mask:"), tr("How dark the tube's mask is")},
                     {"slCrtGlow", tr("Glow:"), tr("The light bright areas throw around them")},
                     {"slCrtBlur", tr("Blur:"), tr("How far a pixel runs into its neighbours")},
                     {"slCrtFringe", tr("Colour fringes:"), tr("How far off the blue gun lands")}};
    const auto announce = [this] { emit shaderChanged(crtShader_->isChecked(), chosenLook()); };
    for (int i = 0; i < 6; ++i) {
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setObjectName(kSliders[i].name);
        slider->setRange(0, CrtLook::kMost);
        slider->setPageStep(10);
        slider->setValue(100);
        slider->setToolTip(kSliders[i].tip);
        auto* value = new QLabel("100 %");
        value->setObjectName(QString("l") + (kSliders[i].name + 2));
        value->setMinimumWidth(fontMetrics().horizontalAdvance("200 %"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(slider, &QSlider::valueChanged, this, [value, announce](int percent) {
            value->setText(QString("%1 %").arg(percent));
            announce();
        });
        // Two columns of three.
        const int row = 1 + i % 3, column = i / 3 * 3;
        shaderGrid->addWidget(new QLabel(kSliders[i].text), row, column);
        shaderGrid->addWidget(slider, row, column + 1);
        shaderGrid->addWidget(value, row, column + 2);
        crtSliders_[i] = slider;
    }
    displaySync_ = new QCheckBox(tr("In step with a 50 Hz screen"));
    displaySync_->setObjectName("ckDisplaySync");
    displaySync_->setToolTip(tr("On a screen set to 50 Hz (or 100 Hz) the machine runs one frame to each picture the screen "
                                "shows: what scrolls is as smooth as on a CTM644. On any other screen this does nothing."));
    shaderGrid->addWidget(displaySync_, 4, 0, 1, 6);
    shaderGrid->setColumnStretch(1, 1);
    shaderGrid->setColumnStretch(4, 1);
    connect(crtShader_, &QCheckBox::toggled, this, announce);
    connect(defaults, &QPushButton::clicked, this, [this] { setLook(CrtLook()); });

    // ---- Full Screen Colours: always true colour here ----
    auto* coloursBox = new QGroupBox(tr("Full Screen Colours"));
    auto* depth8 = notYet(new QRadioButton(tr("8 Bit (Performance)")));
    depth8->setObjectName("rb8bitFS");
    auto* depth16 = notYet(new QRadioButton(tr("16 Bit (Accuracy)")));
    depth16->setObjectName("rb16bitFS");
    depth16->setChecked(true);
    auto* coloursLayout = new QVBoxLayout(coloursBox);
    coloursLayout->setSpacing(2);
    coloursLayout->addWidget(depth8);
    coloursLayout->addWidget(depth16);

    // ---- Brightness ----
    auto* brightBox = new QGroupBox(tr("Brightness"));
    brightBox->setObjectName("gbBrightness");
    brightness_ = new QSlider(Qt::Horizontal);
    brightness_->setObjectName("slBright");
    brightness_->setRange(-100, 100);
    brightness_->setPageStep(10);
    brightnessLabel_ = new QLabel;
    brightnessLabel_->setObjectName("lBright");
    brightnessLabel_->setMinimumWidth(fontMetrics().horizontalAdvance("-100"));
    brightnessLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(brightness_, &QSlider::valueChanged, this, [this](int value) {
        brightnessLabel_->setText(QString::number(value));
        updatePreview();
    });
    auto* brightLayout = new QHBoxLayout(brightBox);
    brightLayout->addWidget(brightness_, 1);
    brightLayout->addWidget(brightnessLabel_);

    auto* grid = new QGridLayout;
    grid->addWidget(holdBox, 0, 0, 2, 1);
    grid->addWidget(optionBoxes[0], 0, 1);
    grid->addWidget(optionBoxes[1], 0, 2);
    grid->addWidget(sharedBox, 1, 1);
    grid->addWidget(coloursBox, 1, 2);
    grid->addWidget(brightBox, 2, 0, 1, 3);
    grid->addWidget(shaderBox, 3, 0, 1, 3);

    auto* layout = new QHBoxLayout(page);
    layout->addLayout(left);
    layout->addLayout(grid, 1);
    return page;
}

CrtLook SetupDialog::chosenLook() const
{
    CrtLook look;
    int* const figures[6] = {&look.curvature, &look.scanLines, &look.mask, &look.glow, &look.blur, &look.fringe};
    for (int i = 0; i < 6; ++i)
        *figures[i] = crtSliders_[i]->value();
    return look;
}

void SetupDialog::setLook(const CrtLook& look)
{
    const int figures[6] = {look.curvature, look.scanLines, look.mask, look.glow, look.blur, look.fringe};
    for (int i = 0; i < 6; ++i)
        crtSliders_[i]->setValue(figures[i]);
}

int SetupDialog::chosenMonitor() const
{
    return monitor_[1]->isChecked() ? 1 : monitor_[2]->isChecked() ? 2 : 0;
}

void SetupDialog::setPreview(const QImage& frame)
{
    previewFrame_ = frame.convertToFormat(QImage::Format_RGB32);
    updatePreview();
}

// A piece of the picture, twice as large, as the monitor chosen on the page
// would show it: each colour of the picture is taken back to the hardware
// colour it stands for, then to that colour on the new monitor.
void SetupDialog::updatePreview()
{
    if (!preview_ || previewFrame_.isNull() || !brightness_ || !linearPalette_)
        return;
    using tuxape::GateArray;
    using tuxape::MonitorKind;
    QRgb from[32], to[32];
    for (int colour = 0; colour < 32; ++colour) {
        from[colour] = GateArray::monitorColour(colour, static_cast<MonitorKind>(opened_.monitorType),
                                                opened_.linearPalette, opened_.brightness);
        to[colour] = GateArray::monitorColour(colour, static_cast<MonitorKind>(chosenMonitor()),
                                              linearPalette_->isChecked(), brightness_->value());
    }
    // Where the firmware's text begins.
    const QRect piece = QRect(64, 36, 150, 108).intersected(previewFrame_.rect());
    QImage shown = previewFrame_.copy(piece);
    QRgb last = 0, lastTo = 0xFF000000;
    for (int y = 0; y < shown.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(shown.scanLine(y));
        for (int x = 0; x < shown.width(); ++x) {
            const QRgb pixel = line[x] | 0xFF000000;
            if (pixel != last) {
                last = pixel;
                lastTo = pixel;  // a colour that is none of the monitor's (the black of a sync) stays
                for (int colour = 0; colour < 32; ++colour)
                    if (from[colour] == pixel) {
                        lastTo = to[colour];
                        break;
                    }
            }
            line[x] = lastTo;
        }
    }
    preview_->setPixmap(QPixmap::fromImage(shown.scaled(preview_->size())));
}

QWidget* SetupDialog::createSoundPage()
{
    auto* page = new QWidget;

    // A row of radio buttons in a box of their own.
    auto radioBox = [this](const QString& title, std::initializer_list<std::pair<const char*, QString>> buttons,
                           QRadioButton** into, bool sideBySide) {
        auto* box = new QGroupBox(title);
        QBoxLayout* layout = sideBySide ? static_cast<QBoxLayout*>(new QHBoxLayout(box))
                                        : static_cast<QBoxLayout*>(new QVBoxLayout(box));
        layout->setSpacing(3);
        for (const auto& [name, text] : buttons) {
            auto* button = new QRadioButton(text);
            button->setObjectName(name);
            layout->addWidget(button);
            *into++ = button;
        }
        return box;
    };

    auto* outputBox = radioBox(tr("Sound Output"),
                               {{"rbNone", tr("None")}, {"rbSpeaker", tr("PC Speaker")}, {"rbDirectSound", tr("DirectSound")}},
                               soundOutput_, true);
    notYet(soundOutput_[1]);  // no PC speaker to beep with
    soundOutput_[2]->setToolTip(tr("The sound device of this computer"));
    for (QRadioButton* button : soundOutput_)
        connect(button, &QRadioButton::toggled, this, &SetupDialog::updateSoundOptions);
    auto* rateBox = radioBox(tr("Sample Rate"), {{"rb22", tr("22 kHz")}, {"rb44", tr("44 kHz")}}, soundRate_, false);
    auto* bitsBox = radioBox(tr("Bits Per Sample"), {{"rb8bit", tr("8 bit")}, {"rb16bit", tr("16 bit")}}, soundBits_, false);
    auto* channelsBox =
        radioBox(tr("Sample Channels"), {{"rbMono", tr("Mono")}, {"rbStereo", tr("Stereo")}}, soundChannels_, false);
    auto* optionsRow = new QHBoxLayout;
    optionsRow->addWidget(rateBox);
    optionsRow->addWidget(bitsBox);
    optionsRow->addWidget(channelsBox);

    // ---- Sound Buffer Synchronisation ----
    auto* syncBox = new QGroupBox(tr("Sound Buffer Synchronisation"));
    auto* advice = new QLabel(tr("Change this setting if you experience sound timing problems, if the sound seems "
                                 "to lag behind the display, repeats unneccesarily, seems broken or jittery."));
    advice->setWordWrap(true);
    soundBufferSync_ = new QSlider(Qt::Horizontal);
    soundBufferSync_->setObjectName("slBufferSync");
    soundBufferSync_->setRange(0, 20);
    soundBufferSyncLabel_ = new QLabel;
    soundBufferSyncLabel_->setObjectName("lBufferSync");
    soundBufferSyncLabel_->setAlignment(Qt::AlignHCenter);
    connect(soundBufferSync_, &QSlider::valueChanged, this, [this](int value) {
        soundBufferSyncLabel_->setText(QStringLiteral("%1.%2").arg(value / 10).arg(value % 10));
    });
    auto* syncLayout = new QVBoxLayout(syncBox);
    syncLayout->addWidget(advice);
    syncLayout->addWidget(soundBufferSync_);
    syncLayout->addWidget(soundBufferSyncLabel_);

    // ---- Other Sounds: to come ----
    auto* otherBox = new QGroupBox(tr("Other Sounds"));
    auto* discSounds = notYet(new QCheckBox(tr("Disc Drive Sounds")));
    discSounds->setObjectName("ckDiscSound");
    tapeSounds_ = new QCheckBox(tr("Tape Loading Sounds"));
    tapeSounds_->setObjectName("ckTapeSound");
    amDrum_ = new QCheckBox(tr("AmDrum"));
    amDrum_->setObjectName("ckAmDrum");
    amDrum_->setToolTip(tr("A sound converter on ports #FF00 to #FFFF"));
    auto* amDrum = amDrum_;
    auto* otherLayout = new QHBoxLayout(otherBox);
    otherLayout->addWidget(discSounds);
    otherLayout->addWidget(tapeSounds_);
    otherLayout->addWidget(amDrum);

    // ---- Volume ----
    auto* volumeBox = new QGroupBox(tr("Volume"));
    soundVolume_ = new QSlider(Qt::Vertical);
    soundVolume_->setObjectName("slVolume");
    soundVolume_->setRange(0, 15);
    soundVolume_->setPageStep(1);
    soundVolumeLabel_ = new QLabel;
    soundVolumeLabel_->setObjectName("lVolume");
    soundVolumeLabel_->setMinimumWidth(fontMetrics().horizontalAdvance("15"));
    connect(soundVolume_, &QSlider::valueChanged, this,
            [this](int value) { soundVolumeLabel_->setText(QString::number(value)); });
    auto* volumeLayout = new QHBoxLayout(volumeBox);
    volumeLayout->addWidget(soundVolume_);
    volumeLayout->addWidget(soundVolumeLabel_);

    auto* left = new QVBoxLayout;
    left->addWidget(outputBox);
    left->addLayout(optionsRow);
    left->addWidget(syncBox);
    left->addWidget(otherBox);
    left->addStretch(1);
    auto* layout = new QHBoxLayout(page);
    layout->addLayout(left, 1);
    layout->addWidget(volumeBox);
    return page;
}

// The sound card's options mean nothing without the sound card.
void SetupDialog::updateSoundOptions()
{
    const bool on = soundOutput_[2]->isChecked();
    for (QRadioButton* button : soundRate_)
        button->setEnabled(on);
    for (QRadioButton* button : soundBits_)
        button->setEnabled(on);
    for (QRadioButton* button : soundChannels_)
        button->setEnabled(on);
    soundVolume_->setEnabled(on);
    soundBufferSync_->setEnabled(on);
}

QWidget* SetupDialog::createInputPage()
{
    using tuxape::CpcKey;
    auto* page = new QWidget;

    // ---- The CPC's keyboard and joystick, to click on ----
    constexpr int kUnit = 28;    // width of an ordinary key
    constexpr int kRow = 26;     // height of a row of keys
    auto* keyboard = new QWidget;
    keyboard->setObjectName("pKeyboard");
    QFont keyFont = font();
    keyFont.setPointSizeF(keyFont.pointSizeF() * 0.72);
    auto addKey = [&](const QString& text, CpcKey key, double x, int row, double width, int rows = 1,
                      const char* name = nullptr) {
        auto* button = new QToolButton(keyboard);
        button->setText(text);
        button->setFont(keyFont);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setGeometry(qRound(x * kUnit), row * kRow, qRound(width * kUnit) - 1, rows * kRow - 1);
        button->setObjectName(name ? QString::fromLatin1(name) : QStringLiteral("k%1").arg(static_cast<int>(key)));
        button->setProperty("cpcKey", static_cast<int>(key));
        connect(button, &QToolButton::clicked, this, [this, key] { selectCpcKey(static_cast<int>(key)); });
        keyButtons_.append(button);
    };
    struct Key {
        const char* text;
        CpcKey key;
        double width;
    };
    static const Key rows[5][16] = {
        {{"ESC", CpcKey::Escape, 1},    {"!\n1", CpcKey::Num1, 1},  {"\"\n2", CpcKey::Num2, 1}, {"#\n3", CpcKey::Num3, 1},
         {"$\n4", CpcKey::Num4, 1},    {"%\n5", CpcKey::Num5, 1},  {"&&\n6", CpcKey::Num6, 1},  {"'\n7", CpcKey::Num7, 1},
         {"(\n8", CpcKey::Num8, 1},    {")\n9", CpcKey::Num9, 1},  {"_\n0", CpcKey::Num0, 1},  {"=\n-", CpcKey::Minus, 1},
         {"\u00a3\n^", CpcKey::Caret, 1}, {"CLR", CpcKey::Clr, 1},   {"DEL", CpcKey::Del, 1}},
        {{"TAB", CpcKey::Tab, 1.5},     {"Q", CpcKey::Q, 1},        {"W", CpcKey::W, 1},        {"E", CpcKey::E, 1},
         {"R", CpcKey::R, 1},           {"T", CpcKey::T, 1},        {"Y", CpcKey::Y, 1},        {"U", CpcKey::U, 1},
         {"I", CpcKey::I, 1},           {"O", CpcKey::O, 1},        {"P", CpcKey::P, 1},        {"|\n@", CpcKey::At, 1},
         {"{\n[", CpcKey::LeftBracket, 1}},
        {{"CAPS\nLOCK", CpcKey::CapsLock, 1.75}, {"A", CpcKey::A, 1}, {"S", CpcKey::S, 1},      {"D", CpcKey::D, 1},
         {"F", CpcKey::F, 1},           {"G", CpcKey::G, 1},        {"H", CpcKey::H, 1},        {"J", CpcKey::J, 1},
         {"K", CpcKey::K, 1},           {"L", CpcKey::L, 1},        {"*\n:", CpcKey::Colon, 1}, {"+\n;", CpcKey::Semicolon, 1},
         {"}\n]", CpcKey::RightBracket, 1}},
        {{"SHIFT", CpcKey::Shift, 2.25}, {"Z", CpcKey::Z, 1},       {"X", CpcKey::X, 1},        {"C", CpcKey::C, 1},
         {"V", CpcKey::V, 1},           {"B", CpcKey::B, 1},        {"N", CpcKey::N, 1},        {"M", CpcKey::M, 1},
         {"<\n,", CpcKey::Comma, 1},    {">\n.", CpcKey::Period, 1}, {"?\n/", CpcKey::Slash, 1}, {"`\n\\", CpcKey::Backslash, 1}},
        {{"CONTROL", CpcKey::Control, 2.25}, {"COPY", CpcKey::Copy, 1.75}, {"", CpcKey::Space, 8}, {"ENTER", CpcKey::Enter, 3}},
    };
    for (int row = 0; row < 5; ++row) {
        double x = 0;
        for (const Key& key : rows[row]) {
            if (!key.text)
                break;
            addKey(QString::fromUtf8(key.text), key.key, x, row, key.width);
            x += key.width;
        }
    }
    // RETURN takes two rows; the second SHIFT is the same key as the first.
    addKey(tr("RETURN"), CpcKey::Return, 13.75, 1, 1.25, 2);
    // A narrow key for a long word.
    QFont narrow = keyFont;
    narrow.setPointSizeF(keyFont.pointSizeF() * 0.8);
    narrow.setStretch(QFont::Condensed);
    keyButtons_.last()->setFont(narrow);
    addKey(tr("SHIFT"), CpcKey::Shift, 13.25, 3, 1.75, 1, "j21");
    // The function keys and the cursor keys.
    static const Key pad[5][3] = {
        {{"f7", CpcKey::F7, 1}, {"f8", CpcKey::F8, 1}, {"f9", CpcKey::F9, 1}},
        {{"f4", CpcKey::F4, 1}, {"f5", CpcKey::F5, 1}, {"f6", CpcKey::F6, 1}},
        {{"f1", CpcKey::F1, 1}, {"f2", CpcKey::F2, 1}, {"f3", CpcKey::F3, 1}},
        {{"f0", CpcKey::F0, 1}, {"\u2191", CpcKey::CursorUp, 1}, {".", CpcKey::FDot, 1}},
        {{"\u2190", CpcKey::CursorLeft, 1}, {"\u2193", CpcKey::CursorDown, 1}, {"\u2192", CpcKey::CursorRight, 1}},
    };
    for (int row = 0; row < 5; ++row)
        for (int column = 0; column < 3; ++column)
            addKey(QString::fromUtf8(pad[row][column].text), pad[row][column].key, 15.3 + column, row, 1);
    // The joystick.
    auto* joystickLabel = new QLabel(tr("Joystick"), keyboard);
    joystickLabel->setAlignment(Qt::AlignHCenter);
    joystickLabel->setGeometry(qRound(18.8 * kUnit), 0, 3 * kUnit, kRow - 4);
    addKey(QString::fromUtf8("\u2191"), CpcKey::JoyUp, 19.8, 1, 1);
    addKey(QString::fromUtf8("\u2190"), CpcKey::JoyLeft, 18.8, 2, 1);
    addKey(QString::fromUtf8("\u2192"), CpcKey::JoyRight, 20.8, 2, 1);
    addKey(QString::fromUtf8("\u2193"), CpcKey::JoyDown, 19.8, 3, 1);
    // The CPC's main fire button is the one its firmware calls "fire 2".
    addKey(tr("FIRE 1"), CpcKey::JoyFire2, 18.6, 4, 1.6);
    addKey(tr("FIRE 2"), CpcKey::JoyFire1, 20.2, 4, 1.6);
    addKey(tr("FIRE 3"), CpcKey::JoyFire3, 19.4, 5, 1.6);
    keyboard->setFixedSize(qRound(21.9 * kUnit), 6 * kRow);

    // ---- The PC keys of the CPC key clicked ----
    QGroupBox* boxes[2] = {new QGroupBox(tr("With Num Lock Off")), new QGroupBox(tr("With Num Lock On"))};
    const char* const comboNames[2][3] = {{"cbKey1", "cbKey2", "cbKey3"}, {"cbKeyNL1", "cbKeyNL2", "cbKeyNL3"}};
    for (int state = 0; state < 2; ++state) {
        auto* grid = new QGridLayout(boxes[state]);
        grid->setVerticalSpacing(3);
        for (int alternative = 0; alternative < 3; ++alternative) {
            auto* combo = new QComboBox;
            combo->setObjectName(comboNames[state][alternative]);
            combo->addItem(QString(), 0);
            for (const uint8_t pcKey : tuxape::namedPcKeys())
                combo->addItem(QString::fromLatin1(tuxape::pcKeyName(pcKey)), pcKey);
            combo->setEnabled(false);
            connect(combo, &QComboBox::activated, this, [this, state, alternative, combo](int index) {
                if (selectedKey_ >= 0)
                    keyMap_.setPcKey(state != 0, static_cast<tuxape::CpcKey>(selectedKey_), alternative,
                                     static_cast<uint8_t>(combo->itemData(index).toInt()));
            });
            keyCombos_[state][alternative] = combo;
            grid->addWidget(new QLabel(tr("Key %1:").arg(alternative + 1)), alternative, 0);
            grid->addWidget(combo, alternative, 1);
        }
        grid->setColumnStretch(1, 1);
    }
    auto* bindings = new QHBoxLayout;
    bindings->addWidget(boxes[1]);  // WinAPE has "Num Lock On" on the left
    bindings->addWidget(boxes[0]);

    joystick_ = new QCheckBox(tr("Enable Joystick"));
    joystick_->setObjectName("ckJoystick");
    joystick_->setToolTip(tr("A joystick or game pad plugged into this computer is the CPC's joystick"));
    amxMouse_ = new QCheckBox(tr("Enable AMX Mouse"));
    amxMouse_->setObjectName("ckAMXMouse");
    amxMouse_->setToolTip(tr("This computer's mouse, over the picture, is an AMX mouse on the CPC's joystick port"));
    auto* mouse = amxMouse_;
    auto* checks = new QHBoxLayout;
    checks->addWidget(joystick_);
    checks->addSpacing(20);
    checks->addWidget(mouse);
    checks->addStretch(1);

    auto* layout = new QVBoxLayout(page);
    layout->addWidget(keyboard, 0, Qt::AlignHCenter);
    layout->addLayout(bindings);
    layout->addLayout(checks);
    layout->addStretch(1);
    return page;
}

void SetupDialog::setKeyMap(const tuxape::KeyMap& map)
{
    keyMap_ = map;
    showKeyBindings();
}

void SetupDialog::selectCpcKey(int key)
{
    selectedKey_ = key >= 0 && key < tuxape::kCpcKeyCount ? key : -1;
    // Both SHIFT keys are one key.
    for (QAbstractButton* button : keyButtons_)
        button->setChecked(button->property("cpcKey").toInt() == selectedKey_);
    showKeyBindings();
}

// The six lists show the PC keys of the CPC key clicked, or nothing.
void SetupDialog::showKeyBindings()
{
    for (int state = 0; state < 2; ++state)
        for (int alternative = 0; alternative < 3; ++alternative) {
            QComboBox* combo = keyCombos_[state][alternative];
            if (!combo)
                continue;
            combo->setEnabled(selectedKey_ >= 0);
            const int pcKey =
                selectedKey_ < 0 ? 0 : keyMap_.pcKey(state != 0, static_cast<tuxape::CpcKey>(selectedKey_), alternative);
            int index = combo->findData(pcKey);
            if (index < 0) {
                // A key the list has no name for: shown by its number.
                combo->addItem(tr("Key #%1").arg(pcKey, 2, 16, QLatin1Char('0')), pcKey);
                index = combo->count() - 1;
            }
            combo->setCurrentIndex(index);
        }
}

bool SetupDialog::loadKeyboard(const QString& path)
{
    const auto data = tuxape::readFile(path.toStdString());
    tuxape::KeyMap map;
    if (!data || !map.load(*data))
        return false;
    keyboardFile_ = path;
    setKeyMap(map);
    return true;
}

bool SetupDialog::saveKeyboard(const QString& path)
{
    if (!tuxape::writeFile(path.toStdString(), keyMap_.save()))
        return false;
    keyboardFile_ = path;
    return true;
}

QWidget* SetupDialog::createOtherPage()
{
    auto* page = new QWidget;

    // ---- Symbiface II: to come ----
    auto* symbiface = new QGroupBox(tr("Symbiface II"));
    auto* symbifaceLayout = new QVBoxLayout(symbiface);
    auto* ide = notYet(new QCheckBox(tr("Enable IDE Devices")));
    ide->setObjectName("ckIDE");
    symbifaceLayout->addWidget(ide);
    for (const QString& title : {tr("Master"), tr("Slave")}) {
        auto* box = new QGroupBox(title);
        auto* kinds = new QHBoxLayout;
        for (const QString& text : {tr("None"), tr("Logical Drive"), tr("IDE File")})
            kinds->addWidget(notYet(new QRadioButton(text)));
        kinds->addStretch(1);
        auto* file = new QHBoxLayout;
        file->addWidget(notYet(new QLineEdit), 1);
        auto* browse = notYet(new QToolButton);
        browse->setText("...");
        file->addWidget(browse);
        auto* inside = new QVBoxLayout(box);
        inside->addLayout(kinds);
        inside->addLayout(file);
        symbifaceLayout->addWidget(box);
    }
    const struct {
        QString text;
        const char* name;
    } kDevices[] = {{tr("Enable Real-Time Clock"), "ckRTC"},
                    {tr("Enable PS/2 Mouse"), "ckPS2Mouse"},
                    {tr("Adjust Mouse for Screen Mode"), "ckAdjustMouse"}};
    for (const auto& device : kDevices) {
        auto* box = notYet(new QCheckBox(device.text));
        box->setObjectName(device.name);
        symbifaceLayout->addWidget(box);
    }
    symbifaceLayout->addStretch(1);

    // ---- Printer: what is on the printer's port ----
    auto* printerBox = new QGroupBox(tr("Printer"));
    const struct {
        QString text;
        const char* name;
        int row, column;
    } kModes[5] = {{tr("Disabled"), "rbPrnDisabled", 0, 0},
                   {tr("Digiblaster"), "rbPrnDigiblaster", 0, 1},
                   {tr("Printer"), "rbPrnPrinter", 1, 0},
                   {tr("File"), "rbPrnFile", 1, 1},
                   {tr("Assembler"), "rbPrnAssembler", 1, 2}};
    auto* modes = new QGridLayout;
    for (int mode = 0; mode < 5; ++mode) {
        printer_[mode] = new QRadioButton(kModes[mode].text);
        printer_[mode]->setObjectName(kModes[mode].name);
        modes->addWidget(printer_[mode], kModes[mode].row, kModes[mode].column);
    }
    notYet(printer_[Settings::PrinterHost]);  // no printing on the host's printer
    printer_[Settings::PrinterDigiblaster]->setToolTip(tr("A sound converter in the printer's place"));
    printer_[Settings::PrinterFile]->setToolTip(tr("What is printed is added to a file"));
    printer_[Settings::PrinterAssembler]->setToolTip(tr("What is printed goes to a tab of the assembler"));
    printer_[Settings::PrinterDisabled]->setChecked(true);
    printerFile_ = new QLineEdit;
    printerFile_->setObjectName("edPrinterFile");
    printerFile_->setPlaceholderText(tr("Printer Output"));
    auto* browsePrinter = new QToolButton;
    browsePrinter->setObjectName("bPrinterFile");
    browsePrinter->setText("...");
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(printerFile_, 1);
    fileRow->addWidget(browsePrinter);
    auto* printerLayout = new QVBoxLayout(printerBox);
    printerLayout->addLayout(modes);
    printerLayout->addLayout(fileRow);
    // The file only matters when printing goes to one.
    const auto fileWanted = [this, browsePrinter] {
        const bool wanted = printer_[Settings::PrinterFile]->isChecked();
        printerFile_->setEnabled(wanted);
        browsePrinter->setEnabled(wanted);
    };
    for (QRadioButton* button : printer_)
        connect(button, &QRadioButton::toggled, this, fileWanted);
    fileWanted();
    connect(browsePrinter, &QToolButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Printer Output"), printerFile_->text(),
                                                          tr("Text files (*.txt);;All files (*)"), nullptr,
                                                          QFileDialog::DontConfirmOverwrite);
        if (!path.isEmpty())
            printerFile_->setText(QDir::toNativeSeparators(path));
    });

    // ---- Other devices: to come ----
    auto* devicesBox = new QGroupBox(tr("Other Devices"));
    auto* devicesLayout = new QHBoxLayout(devicesBox);
    devicesLayout->addWidget(notYet(new QLabel(tr("Dobbertin SmartWatch ROM Select:"))));
    auto* smartWatch = notYet(new QComboBox);
    smartWatch->setObjectName("cbSmartWatch");
    devicesLayout->addWidget(smartWatch, 1);

    auto* right = new QVBoxLayout;
    right->addWidget(printerBox);
    right->addWidget(devicesBox);
    right->addStretch(1);
    auto* layout = new QHBoxLayout(page);
    layout->addWidget(symbiface, 1);
    layout->addLayout(right, 1);
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
    enableCartridge_ = new QCheckBox(tr("Enable Cartridge"));
    enableCartridge_->setObjectName("ckEnableCart");
    cartridgeFile_ = new QLabel;
    cartridgeFile_->setObjectName("lCartridge");
    auto* browseCartridge = new QToolButton;
    browseCartridge->setObjectName("sbCartridge");
    browseCartridge->setText("...");
    connect(browseCartridge, &QToolButton::clicked, this, &SetupDialog::chooseCartridge);
    auto* cartridgeRow = new QHBoxLayout;
    cartridgeRow->addWidget(new QLabel(tr("File:")));
    cartridgeRow->addWidget(cartridgeFile_, 1);
    cartridgeRow->addWidget(browseCartridge);
    auto* cartridgeLayout = new QVBoxLayout(cartridgeBox);
    cartridgeLayout->setSpacing(3);
    cartridgeLayout->addWidget(enableCartridge_);
    cartridgeLayout->addLayout(cartridgeRow);

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

void SetupDialog::setCartridge(const QString& cartridge)
{
    cartridge_ = cartridge;
    cartridgeFile_->setText(QFileInfo(QDir::fromNativeSeparators(cartridge)).fileName());
    cartridgeFile_->setToolTip(QDir::toNativeSeparators(cartridge));
}

// A cartridge from the ROM folder goes by its name, as ROM images do.
void SetupDialog::chooseCartridge()
{
    const QString romDir = QDir::cleanPath(QString::fromStdString(tuxape::defaultRomDir().string()));
    const QString path = QFileDialog::getOpenFileName(this, tr("Cartridge"), romDir, tr("Cartridges (*.cpr);;All files (*)"));
    if (path.isEmpty())
        return;
    const QFileInfo file(path);
    setCartridge(QDir::cleanPath(file.absolutePath()) == romDir ? file.fileName() : QDir::toNativeSeparators(path));
    enableCartridge_->setChecked(true);
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
    monitor_[settings.monitorType]->setChecked(true);
    verticalHold_->setValue(settings.verticalHold);
    verticalHoldLabel_->setText(QString::number(settings.verticalHold));
    brightness_->setValue(settings.brightness);
    brightnessLabel_->setText(QString::number(settings.brightness));
    linearPalette_->setChecked(settings.linearPalette);
    pal_->setChecked(settings.palEmulation);
    crtShader_->setChecked(settings.crtShader);
    setLook(settings.crtLook);
    displaySync_->setChecked(settings.displaySync);
    welcome_->setChecked(settings.welcomePicture);
    driveLed_->setChecked(settings.driveLed);
    showTrack_->setChecked(settings.showDriveCylinders);
    const Settings::WindowOptions* options[2] = {&settings.windowed, &settings.fullScreen};
    for (int mode = 0; mode < 2; ++mode) {
        const bool values[6] = {options[mode]->halfSize,  options[mode]->renderBothLines, options[mode]->hideMouse,
                                options[mode]->hidePanel, options[mode]->hideMenus,       options[mode]->noRightClick};
        for (int i = 0; i < 6; ++i)
            windowOptions_[mode][i]->setChecked(values[i]);
    }
    updatePreview();

    soundOutput_[settings.soundOn ? 2 : 0]->setChecked(true);
    soundRate_[settings.soundRate < 33000 ? 0 : 1]->setChecked(true);
    soundBits_[settings.sound16Bit ? 1 : 0]->setChecked(true);
    soundChannels_[settings.soundStereo ? 1 : 0]->setChecked(true);
    soundVolume_->setValue(settings.soundVolume);
    tapeSounds_->setChecked(settings.tapeSounds);
    amDrum_->setChecked(settings.amDrum);
    printer_[std::clamp(settings.printerMode, 0, 4)]->setChecked(true);
    printerFile_->setText(QDir::toNativeSeparators(settings.printerFile));
    soundVolumeLabel_->setText(QString::number(settings.soundVolume));
    soundBufferSync_->setValue(settings.soundBufferSync);
    soundBufferSyncLabel_->setText(
        QStringLiteral("%1.%2").arg(settings.soundBufferSync / 10).arg(settings.soundBufferSync % 10));
    updateSoundOptions();

    joystick_->setChecked(settings.joystick);
    amxMouse_->setChecked(settings.amxMouse);
    keyboardFile_ = settings.keyboardFile;

    crtcType_->setCurrentIndex(settings.crtcType);
    enablePlus_->setChecked(settings.machine.plus);
    enableCartridge_->setChecked(settings.machine.cartridgeEnabled);
    setCartridge(QString::fromStdString(settings.machine.cartridge));
    fastDisc_->setChecked(settings.fastDisc);
    fourDrives_->setChecked(settings.fourDrives);
    speed_->setValue(settings.speedPercent);
    turbo_->setChecked(settings.turbo);
    plusPpi_->setChecked(settings.plusPpi);
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
    auto* display = group(tr("Display"));
    leaf(display, tr("Full Screen Settings"), Settings::FullScreenPart, false);
    leaf(display, tr("Monitor"), Settings::MonitorPart, false);
    leaf(display, tr("Windowed Settings"), Settings::WindowedPart, false);
    auto* general = group(tr("General"));
    leaf(general, tr("CRTC Type"), Settings::CrtcPart, true);
    leaf(general, tr("Emulation Speed"), Settings::SpeedPart, false);
    leaf(general, tr("Fast Disc Emulation"), Settings::FastDiscPart, false);
    auto* input = group(tr("Input"));
    leaf(input, tr("Joystick"), Settings::InputPart, false);
    auto* memory = group(tr("Memory"));
    leaf(memory, tr("RAM"), Settings::RamPart, true);
    leaf(memory, tr("ROMs"), Settings::RomsPart, true);
    auto* sound = group(tr("Sound"));
    leaf(sound, tr("Sound Output"), Settings::SoundPart, false);
    tree_->expandAll();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    wireHelp(buttons->button(QDialogButtonBox::Help));
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
    // What none of the pages shows comes back as it was given.
    Settings settings = opened_;
    settings.crtcType = crtcType_->currentIndex();
    settings.fastDisc = fastDisc_->isChecked();
    settings.fourDrives = fourDrives_->isChecked();
    settings.speedPercent = speed_->value();
    settings.turbo = turbo_->isChecked();
    settings.plusPpi = plusPpi_->isChecked();
    settings.displayEvery = displayEvery_->isChecked();
    settings.displayEveryFrames = displayEveryFrames_->value();

    tuxape::MachineConfig& machine = settings.machine;
    machine.ram = chosenRam();
    machine.siliconDisc = siliconDisc_->isChecked();
    machine.plus = enablePlus_->isChecked();
    machine.cartridgeEnabled = enableCartridge_->isChecked();
    machine.cartridge = cartridge_.toStdString();
    machine.lowerRom = romNames_[0].toStdString();
    for (int slot = 0; slot < tuxape::Memory::kRomSlots; ++slot)
        machine.upperRoms[static_cast<size_t>(slot)] = romNames_[1 + slot].toStdString();
    machine.rom32 = rom32_->isChecked();
    machine.disableAllRoms = disableRoms_->isChecked();
    machine.onlyLower0And7 = onlyLower0And7_->isChecked();

    settings.monitorType = chosenMonitor();
    settings.verticalHold = verticalHold_->value();
    settings.brightness = brightness_->value();
    settings.linearPalette = linearPalette_->isChecked();
    settings.palEmulation = pal_->isChecked();
    settings.crtShader = crtShader_->isChecked();
    settings.crtLook = chosenLook();
    settings.displaySync = displaySync_->isChecked();
    settings.welcomePicture = welcome_->isChecked();
    settings.driveLed = driveLed_->isChecked();
    settings.showDriveCylinders = showTrack_->isChecked();
    Settings::WindowOptions* options[2] = {&settings.windowed, &settings.fullScreen};
    for (int mode = 0; mode < 2; ++mode) {
        options[mode]->halfSize = windowOptions_[mode][0]->isChecked();
        options[mode]->renderBothLines = windowOptions_[mode][1]->isChecked();
        options[mode]->hideMouse = windowOptions_[mode][2]->isChecked();
        options[mode]->hidePanel = windowOptions_[mode][3]->isChecked();
        options[mode]->hideMenus = windowOptions_[mode][4]->isChecked();
        options[mode]->noRightClick = windowOptions_[mode][5]->isChecked();
    }

    settings.soundOn = soundOutput_[2]->isChecked();
    settings.soundRate = soundRate_[0]->isChecked() ? 22050 : 44100;
    settings.sound16Bit = soundBits_[1]->isChecked();
    settings.soundStereo = soundChannels_[1]->isChecked();
    settings.soundVolume = soundVolume_->value();
    settings.tapeSounds = tapeSounds_->isChecked();
    settings.amDrum = amDrum_->isChecked();
    for (int mode = 0; mode < 5; ++mode)
        if (printer_[mode]->isChecked())
            settings.printerMode = mode;
    settings.printerFile = QDir::fromNativeSeparators(printerFile_->text().trimmed());
    settings.soundBufferSync = soundBufferSync_->value();

    settings.joystick = joystick_->isChecked();
    settings.amxMouse = amxMouse_->isChecked();
    settings.keyboardFile = keyboardFile_;
    return settings;
}
