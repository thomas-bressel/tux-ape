#include "setupdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

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

}  // namespace

SetupDialog::SetupDialog(const Settings& settings, QWidget* parent)
    : QDialog(parent)
    , initial_(settings)
{
    setWindowTitle(tr("TuxAPE - Setup"));

    auto* profileRow = new QHBoxLayout;
    auto* profileLabel = new QLabel(tr("&Profile:"));
    auto* profile = notYet(new QComboBox);
    profile->setObjectName("cbProfile");
    profile->addItem(tr("(Current Settings)"));
    profileLabel->setBuddy(profile);
    auto* saveProfile = notYet(new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save")));
    saveProfile->setObjectName("bSaveProfile");
    profileRow->addWidget(profileLabel);
    profileRow->addWidget(profile, 1);
    profileRow->addSpacing(40);
    profileRow->addWidget(saveProfile);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("PageControl");
    tabs_->addTab(createGeneralPage(), tr("General"));
    // The other pages are to come.
    for (const char* name : {QT_TR_NOOP("Display"), QT_TR_NOOP("Sound"), QT_TR_NOOP("Memory"), QT_TR_NOOP("Input"),
                             QT_TR_NOOP("Other")}) {
        const int index = tabs_->addTab(new QWidget, tr(name));
        tabs_->setTabEnabled(index, false);
        tabs_->setTabToolTip(index, tr("Not available yet"));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    notYet(buttons->button(QDialogButtonBox::Help));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(profileRow);
    layout->addWidget(tabs_, 1);
    layout->addWidget(buttons);
    resize(640, 400);
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
    crtcType_->setCurrentIndex(initial_.crtcType);
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
    fastDisc_->setChecked(initial_.fastDisc);
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
    speed_->setValue(initial_.speedPercent);
    speedLabel_ = new QLabel;
    speedLabel_->setObjectName("lbSpeed");
    speedLabel_->setMinimumWidth(fontMetrics().horizontalAdvance("1000%"));
    speedLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(speed_, 1);
    speedRow->addWidget(speedLabel_);

    displayEvery_ = new QCheckBox(tr("&Display Every"));
    displayEvery_->setObjectName("ckUseRate");
    displayEvery_->setChecked(initial_.displayEvery);
    displayEveryFrames_ = new QSpinBox;
    displayEveryFrames_->setObjectName("edRate");
    displayEveryFrames_->setRange(1, 50);
    displayEveryFrames_->setValue(initial_.displayEveryFrames);
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

void SetupDialog::showPage(Page page)
{
    if (tabs_->isTabEnabled(page))
        tabs_->setCurrentIndex(page);
}

Settings SetupDialog::settings() const
{
    Settings settings = initial_;
    settings.crtcType = crtcType_->currentIndex();
    settings.fastDisc = fastDisc_->isChecked();
    settings.speedPercent = speed_->value();
    settings.displayEvery = displayEvery_->isChecked();
    settings.displayEveryFrames = displayEveryFrames_->value();
    return settings;
}
