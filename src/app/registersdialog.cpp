#include "registersdialog.h"

#include "contrast.h"

#include <QGuiApplication>
#include <QCheckBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "emulator.h"

namespace {

const char kPlain[] = "";
const char kLit[] = "background: #40e0ff; color: #000000;";  // the register last selected, a counter at work

}  // namespace

RegistersDialog::RegistersDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("TuxAPE - CRTC Registers"));
    auto* layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    for (int column = 0; column <= 16; ++column) {
        auto* head = new QLabel(QString::number(column, 16).toUpper());
        head->setAlignment(Qt::AlignCenter);
        grid->addWidget(head, 0, column + 1);
    }
    const auto row = [&](int at, const QString& title, const QString& name, int count) {
        grid->addWidget(new QLabel(title), at, 0, Qt::AlignRight);
        for (int n = 0; n < count; ++n)
            grid->addWidget(field(name + QString::number(n), 2, true), at, n + 1);
    };
    row(1, tr("Palette:"), "Palette", 17);
    row(2, tr("PSG:"), "PSG", 16);
    row(3, tr("CRTC:"), "CRTC", 16);
    layout->addLayout(grid);

    auto* counters = new QGridLayout;
    counters->setSpacing(4);
    const char* const kNames[2][6] = {{"VCC", "R52", "HDC", "HCC", "VMA", "Mode"}, {"VLC", "VSC", "VTAC", "HSC", "VDUR", "ICSR"}};
    for (int line = 0; line < 2; ++line) {
        for (int n = 0; n < 6; ++n) {
            const QString name = kNames[line][n];
            const int digits = name == "VMA" || name == "VDUR" ? 4 : name == "Mode" ? 1 : 2;
            counters->addWidget(new QLabel(name + ':'), line, n * 2, Qt::AlignRight);
            counters->addWidget(field(name, digits), line, n * 2 + 1, Qt::AlignLeft);
        }
    }
    asicButton_ = new QToolButton;
    asicButton_->setObjectName("bAsic");
    asicButton_->setText(tr("ASIC..."));
    asicButton_->setToolTip(tr("ASIC Registers: a Plus's, in a window of their own"));
    counters->addWidget(asicButton_, 1, 12, Qt::AlignRight);
    rowHighlight_ = new QCheckBox(tr("Row Highlight"));
    rowHighlight_->setObjectName("ckRowHighlight");
    rowHighlight_->setToolTip(tr("Shows on the picture where the Z80 took its interrupts (dashed lines, a square where "
                                 "the beam was), and where the beam is while the machine stands still"));
    counters->addWidget(rowHighlight_, 0, 12, Qt::AlignRight);
    connect(rowHighlight_, &QCheckBox::toggled, this, [this](bool on) {
        emit interruptMarksShown(on && isVisible());
        refresh();
    });
    counters->setColumnStretch(12, 1);
    layout->addLayout(counters);

    // The ASIC's registers have a window of their own: the button opens it.
    connect(asicButton_, &QToolButton::clicked, this, &RegistersDialog::asicRequested);
    // The machine goes on while the window is up.
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible() && !emulator_->isPaused())
            refresh();
    });
    timer->start(200);
    refresh();
}

QLineEdit* RegistersDialog::field(const QString& name, int digits, bool editable)
{
    auto* edit = new QLineEdit;
    edit->setObjectName("ed" + name);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setMaxLength(digits);
    edit->setAlignment(Qt::AlignCenter);
    edit->setFixedWidth(edit->fontMetrics().horizontalAdvance(QString(digits, QLatin1Char('0'))) + 12);
    edit->setReadOnly(!editable);
    if (editable) {
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, name] {
            if (edit->isModified())
                setValue(name, edit->text());
            edit->setModified(false);
            refresh();
        });
    }
    fields_.insert(name, edit);
    return edit;
}

void RegistersDialog::put(const QString& name, unsigned value, bool highlight)
{
    QLineEdit* edit = fields_.value(name);
    if (!edit)
        return;
    // Not under the fingers of someone typing a value in.
    if (!(edit->hasFocus() && edit->isModified())) {
        const int digits = edit->maxLength();
        const unsigned mask = digits >= 8 ? ~0u : (1u << (digits * 4)) - 1;
        const QString text = QString("%1").arg(value & mask, digits, 16, QLatin1Char('0')).toUpper();
        if (edit->text() != text)
            edit->setText(text);
    }
    const char* style = highlight ? kLit : kPlain;
    if (edit->styleSheet() != style)
        edit->setStyleSheet(style);
}

QString RegistersDialog::value(const QString& name) const
{
    const QLineEdit* edit = fields_.value(name);
    return edit ? edit->text() : QString();
}

bool RegistersDialog::highlighted(const QString& name) const
{
    const QLineEdit* edit = fields_.value(name);
    return edit && !edit->styleSheet().isEmpty();
}

bool RegistersDialog::setValue(const QString& name, const QString& hexText)
{
    bool ok = false;
    const unsigned value = hexText.toUInt(&ok, 16);
    const auto numbered = [&](const char* prefix, int count) {
        if (!name.startsWith(QLatin1String(prefix)))
            return -1;
        bool isNumber = false;
        const int n = name.mid(static_cast<int>(qstrlen(prefix))).toInt(&isNumber);
        return isNumber && n >= 0 && n < count ? n : -1;
    };
    const int pen = numbered("Palette", 17), sound = numbered("PSG", 16), video = numbered("CRTC", 16);
    if (!ok || value > 0xFF || (pen < 0 && sound < 0 && video < 0))
        return false;
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        if (pen >= 0) {
            // Through the Gate Array's port, as a program would.
            const uint8_t selected = cpc.gateArray().selectedPen();
            cpc.out(0x7F00, static_cast<uint8_t>(pen < 16 ? pen : 0x10));
            cpc.out(0x7F00, static_cast<uint8_t>(0x40 | (value & 0x1F)));
            cpc.out(0x7F00, static_cast<uint8_t>(selected < 16 ? selected : 0x10));
        } else if (sound >= 0) {
            cpc.psg().setRegister(sound, static_cast<uint8_t>(value));
        } else {
            const uint8_t selected = cpc.crtc().selected();
            cpc.crtc().select(static_cast<uint8_t>(video));
            cpc.crtc().write(static_cast<uint8_t>(value));
            cpc.crtc().select(selected);
        }
    });
    refresh();
    return true;
}

void RegistersDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    emit interruptMarksShown(rowHighlight_->isChecked());
    refresh();
}

bool RegistersDialog::rowHighlight() const
{
    return rowHighlight_->isChecked();
}

void RegistersDialog::setRowHighlight(bool on)
{
    rowHighlight_->setChecked(on);
}

void RegistersDialog::hideEvent(QHideEvent* event)
{
    QDialog::hideEvent(event);
    emit beamChanged(-1, -1);
    emit interruptMarksShown(false);
}

void RegistersDialog::refresh()
{
    struct State {
        uint8_t ink[17], pen, psg[16], psgSelected, crtc[16], crtcSelected;
        uint8_t vcc, vlc, r52, vsc, hcc, hsc, vtac, mode, icsr;
        int hdc, vdur, beamColumn;
        uint16_t vma;
        bool hsync, vsync, adjust, plus;
    };
    const State s = emulator_->withMachine([&](tuxape::Cpc& cpc) {
        State out{};
        const tuxape::GateArray& ga = cpc.gateArray();
        const tuxape::Crtc& crtc = cpc.crtc();
        for (int pen = 0; pen < 17; ++pen)
            out.ink[pen] = ga.ink(pen);
        out.pen = ga.selectedPen();
        for (int n = 0; n < 16; ++n) {
            out.psg[n] = cpc.psg().reg(n);
            out.crtc[n] = crtc.reg(n);
        }
        out.psgSelected = cpc.psg().selected();
        out.crtcSelected = crtc.selected();
        out.vcc = crtc.vcc();
        out.vlc = crtc.vlc();
        out.hcc = crtc.hcc();
        out.hsc = crtc.hsc();
        out.vsc = crtc.vsc();
        out.vtac = crtc.vtac();
        out.hsync = crtc.hsync();
        out.vsync = crtc.vsync();
        out.adjust = crtc.inVerticalAdjust();
        out.r52 = ga.interruptCounter();
        out.mode = ga.mode();
        out.hdc = cpc.monitor().beamX();
        out.vdur = cpc.monitor().rasterLine();
        out.beamColumn = cpc.monitor().beamColumn();
        // Where in memory the byte being shown comes from.
        const unsigned ma = crtc.ma();
        out.vma = static_cast<uint16_t>((ma & 0x3000) << 2 | (crtc.ra() & 7) << 11 | (ma & 0x3FF) << 1);
        out.plus = cpc.plus();
        out.icsr = ga.interruptRequested() ? 0x80 : 0;
        if (out.plus)
            out.icsr = static_cast<uint8_t>(out.icsr | (cpc.asic().dcsr() & 0x70));
        return out;
    });
    // "Row Highlight": the screen shows where the beam is, while the
    // machine stands still.
    if (rowHighlight_->isChecked() && isVisible() && emulator_->isPaused())
        emit beamChanged(s.vdur, s.beamColumn);
    else
        emit beamChanged(-1, -1);

    for (int pen = 0; pen < 17; ++pen)
        put(QString("Palette%1").arg(pen), s.ink[pen], pen == s.pen);
    for (int n = 0; n < 16; ++n) {
        put(QString("PSG%1").arg(n), s.psg[n], n == s.psgSelected);
        put(QString("CRTC%1").arg(n), s.crtc[n], n == s.crtcSelected);
    }
    put("VCC", s.vcc);
    put("VLC", s.vlc);
    put("R52", s.r52);
    put("VSC", s.vsc, s.vsync);
    put("HDC", static_cast<unsigned>(s.hdc));
    put("HCC", s.hcc);
    put("HSC", s.hsc, s.hsync);
    put("VTAC", s.vtac, s.adjust);
    put("VMA", s.vma);
    put("VDUR", static_cast<unsigned>(s.vdur));
    put("Mode", s.mode);
    put("ICSR", s.icsr);

    // The ASIC's window is a Plus's alone.
    asicButton_->setEnabled(s.plus);
}
