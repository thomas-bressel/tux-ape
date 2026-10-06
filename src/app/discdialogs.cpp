#include "discdialogs.h"

#include "help.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTabBar>
#include <QVBoxLayout>

#include "discmanager.h"

FormatDialog::FormatDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Format Disc Image"));

    format_ = new QComboBox(this);
    for (const tuxape::DiscFormat& format : tuxape::discFormats())
        format_->addItem(QString::fromLatin1(format.name));
    format_->setCurrentText(QString::fromLatin1(tuxape::defaultDiscFormat().name));
    clear_ = new QCheckBox(tr("Clear unused tracks"), this);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, this);
    wireHelp(buttons->button(QDialogButtonBox::Help));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* form = new QFormLayout;
    form->addRow(tr("Format:"), format_);
    form->addRow(QString(), clear_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

const tuxape::DiscFormat& FormatDialog::format() const
{
    return tuxape::discFormats()[static_cast<size_t>(format_->currentIndex())];
}

bool FormatDialog::clearUnused() const
{
    return clear_->isChecked();
}

DriveSetupDialog::DriveSetupDialog(DiscManager* discs, QWidget* parent)
    : QDialog(parent)
    , discs_(discs)
{
    setWindowTitle(tr("TuxAPE - Drive Setup"));

    // ---- the drive page, shared by the A: and B: tabs ----
    tabs_ = new QTabBar(this);
    tabs_->addTab(tr("&A:"));
    tabs_->addTab(tr("&B:"));
    if (discs_->fourDrives()) {
        tabs_->addTab(tr("&C:"));
        tabs_->addTab(tr("&D:"));
    }

    auto* page = new QFrame(this);
    page->setFrameShape(QFrame::StyledPanel);

    none_ = new QRadioButton(tr("None"), page);
    auto* floppyA = new QRadioButton(tr("Floppy A:"), page);
    auto* floppyB = new QRadioButton(tr("Floppy B:"), page);
    diskFile_ = new QRadioButton(tr("Disk File"), page);
    // Real floppy drives are not supported.
    floppyA->setEnabled(false);
    floppyB->setEnabled(false);
    auto* source = new QHBoxLayout;
    source->addWidget(none_);
    source->addWidget(floppyA);
    source->addWidget(floppyB);
    source->addWidget(diskFile_);
    source->addStretch(1);

    // The format only matters for real floppies, whose layout cannot be
    // detected.
    auto* format = new QComboBox(page);
    for (const tuxape::DiscFormat& f : tuxape::discFormats())
        format->addItem(QString::fromLatin1(f.name));
    format->setCurrentText(QString::fromLatin1(tuxape::defaultDiscFormat().name));
    format->setEnabled(false);
    auto* open = new QPushButton(tr("&Open"), page);
    auto* formatRow = new QHBoxLayout;
    formatRow->addWidget(new QLabel(tr("Format:"), page));
    formatRow->addWidget(format, 1);
    formatRow->addWidget(open);

    description_ = new QLabel(page);
    description_->setWordWrap(true);
    description_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    description_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    description_->setMinimumHeight(fontMetrics().lineSpacing() * 4);

    singleSided_ = new QCheckBox(tr("Allow &Single-sided read"), page);
    temporaryWrites_ = new QCheckBox(tr("Allow Temporary &Writes"), page);
    promptToSave_ = new QCheckBox(tr("&Prompt to Save Changes"), page);
    edit_ = new QPushButton(tr("&Edit"), page);
    edit_->setObjectName("bEdit");
    flip_ = new QPushButton(tr("&Flip"), page);

    auto* options = new QVBoxLayout;
    options->addWidget(singleSided_);
    options->addWidget(temporaryWrites_);
    options->addWidget(promptToSave_);
    auto* pageButtons = new QVBoxLayout;
    pageButtons->addWidget(edit_);
    pageButtons->addWidget(flip_);
    pageButtons->addStretch(1);
    auto* bottom = new QHBoxLayout;
    bottom->addLayout(options, 1);
    bottom->addLayout(pageButtons);

    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->addLayout(source);
    pageLayout->addLayout(formatRow);
    pageLayout->addWidget(new QLabel(tr("Information:"), page));
    pageLayout->addWidget(description_, 1);
    pageLayout->addLayout(bottom);

    auto* left = new QVBoxLayout;
    left->setSpacing(0);
    left->addWidget(tabs_);
    left->addWidget(page, 1);

    // ---- the buttons down the right-hand side ----
    auto* ok = new QPushButton(tr("&OK"), this);
    ok->setDefault(true);
    auto* cancel = new QPushButton(tr("&Cancel"), this);
    auto* help = new QPushButton(tr("&Help"), this);
    help->setObjectName("bHelp");
    wireHelp(help);
    auto* swap = new QPushButton(tr("&Swap"), this);
    auto* right = new QVBoxLayout;
    right->addWidget(ok);
    right->addWidget(cancel);
    right->addWidget(help);
    right->addStretch(1);
    right->addWidget(swap);

    auto* layout = new QHBoxLayout(this);
    layout->addLayout(left, 1);
    layout->addLayout(right);
    resize(470, sizeHint().height());

    // ---- behaviour ----
    for (int d = 0; d < DiscManager::kDrives; ++d)
        singleSidedChoice_[d] = discs_->singleSidedRead(d);
    temporaryWrites_->setChecked(discs_->allowTemporaryWrites());
    promptToSave_->setChecked(discs_->promptToSave());

    connect(tabs_, &QTabBar::currentChanged, this, &DriveSetupDialog::refresh);
    connect(discs_, &DiscManager::changed, this, &DriveSetupDialog::refresh);
    connect(singleSided_, &QCheckBox::toggled, this, [this](bool on) { singleSidedChoice_[drive()] = on; });
    connect(open, &QPushButton::clicked, this, [this] { emit openRequested(drive()); });
    connect(none_, &QRadioButton::clicked, this, [this] {
        emit removeRequested(drive());
        refresh();  // the owner may have declined
    });
    connect(flip_, &QPushButton::clicked, this, [this] { emit flipRequested(drive()); });
    connect(edit_, &QPushButton::clicked, this, [this] { emit editRequested(drive()); });
    connect(swap, &QPushButton::clicked, this, &DriveSetupDialog::swapRequested);
    connect(ok, &QPushButton::clicked, this, &DriveSetupDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &DriveSetupDialog::reject);

    refresh();
}

int DriveSetupDialog::drive() const
{
    return tabs_->currentIndex();
}

void DriveSetupDialog::refresh()
{
    const DiscManager::Info info = discs_->info(drive());
    none_->setChecked(!info.present);
    diskFile_->setChecked(info.present);
    diskFile_->setEnabled(info.present);
    singleSided_->setChecked(singleSidedChoice_[drive()]);
    edit_->setEnabled(info.present);

    if (!info.present) {
        description_->setText(tr("No disk selected for drive"));
        return;
    }
    QString text = info.path.isEmpty() ? tr("(not saved to a file)") : QDir::toNativeSeparators(info.path);
    text += QLatin1Char('\n') + info.description;
    if (info.readOnly)
        text += QLatin1Char('\n') + tr("Read only");
    description_->setText(text);
}

void DriveSetupDialog::accept()
{
    for (int d = 0; d < DiscManager::kDrives; ++d)
        discs_->setSingleSidedRead(d, singleSidedChoice_[d]);
    discs_->setAllowTemporaryWrites(temporaryWrites_->isChecked());
    discs_->setPromptToSave(promptToSave_->isChecked());
    QDialog::accept();
}
