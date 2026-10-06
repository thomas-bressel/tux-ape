#include "registersdialog.h"

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
const char kLit[] = "background: #40e0ff;";  // the register last selected, a counter at work

QRgb rgbOf(uint16_t grb)
{
    return qRgb((grb >> 4 & 0xF) * 17, (grb >> 8 & 0xF) * 17, (grb & 0xF) * 17);
}

// A tick box that only shows.
QCheckBox* indicator(const QString& text, const char* objectName)
{
    auto* box = new QCheckBox(text);
    box->setObjectName(objectName);
    box->setAttribute(Qt::WA_TransparentForMouseEvents);
    box->setFocusPolicy(Qt::NoFocus);
    return box;
}

// One instruction of a sound list, as WinAPE writes it.
QString dmaText(unsigned word)
{
    const auto hex = [](unsigned value, int digits) { return QString("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper(); };
    switch (word >> 12) {
    case 0: return QString("LD R%1,%2").arg(hex(word >> 8 & 0xF, 1), hex(word & 0xFF, 2));
    case 1: return QString("PAUSE %1").arg(hex(word & 0xFFF, 3));
    case 2: return QString("REPEAT %1").arg(hex(word & 0xFFF, 3));
    case 4:
        if (word == 0x4000) return "NOP";
        if (word == 0x4001) return "LOOP";
        if (word == 0x4010) return "INT";
        if (word == 0x4020) return "STOP";
        break;
    }
    return hex(word, 4);
}

}  // namespace

RegistersDialog::RegistersDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("TuxAPE - Registers"));
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
    asicButton_->setArrowType(Qt::DownArrow);
    asicButton_->setCheckable(true);
    asicButton_->setToolTip(tr("ASIC Registers"));
    counters->addWidget(asicButton_, 1, 12, Qt::AlignRight);
    counters->setColumnStretch(12, 1);
    layout->addLayout(counters);

    // ---- The ASIC ----
    asicPane_ = new QWidget;
    auto* asic = new QVBoxLayout(asicPane_);
    asic->setContentsMargins(0, 0, 0, 0);
    auto* palette = new QGridLayout;
    palette->setSpacing(2);
    palette->addWidget(new QLabel(tr("Palette:")), 0, 0, Qt::AlignRight);
    palette->addWidget(new QLabel(tr("Sprite Palette:")), 1, 0, Qt::AlignRight);
    for (int index = 0; index < 32; ++index) {
        auto* square = new QLabel;
        square->setObjectName(QString("colour%1").arg(index));
        square->setFixedSize(22, 18);
        square->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        square->setAutoFillBackground(true);
        colours_[index] = square;
        // The sprites' colours 1 to 15 stand under pens 1 to 15.
        palette->addWidget(square, index < 17 ? 0 : 1, index < 17 ? index + 1 : index - 15);
    }
    asic->addLayout(palette);

    auto* flags = new QHBoxLayout;
    unlocked_ = indicator(tr("ASIC Unlocked"), "ckUnlocked");
    ramEnabled_ = indicator(tr("ASIC RAM Enabled"), "ckRamEnabled");
    flags->addWidget(unlocked_);
    flags->addWidget(ramEnabled_);
    flags->addStretch();
    const auto labelled = [this](QBoxLayout* into, const QString& title, const QString& name, int digits) {
        into->addWidget(new QLabel(title));
        into->addWidget(field(name, digits));
    };
    labelled(flags, tr("LRB:"), "LRB", 4);
    labelled(flags, tr("Cart Bank:"), "CartBank", 1);
    labelled(flags, tr("DCSR:"), "DCSR", 2);
    asic->addLayout(flags);

    auto* lower = new QHBoxLayout;
    spriteBox_ = new QGroupBox;
    spriteBox_->setObjectName("gbSprite");
    auto* sprite = new QGridLayout(spriteBox_);
    spriteView_ = new QLabel;
    spriteView_->setObjectName("SpriteView");
    spriteView_->setFixedSize(64, 64);
    spriteBar_ = new QScrollBar(Qt::Horizontal);
    spriteBar_->setObjectName("sbSprite");
    spriteBar_->setRange(0, 15);
    sprite->addWidget(spriteView_, 0, 0, 4, 1);
    sprite->addWidget(spriteBar_, 4, 0);
    const auto cell = [&](int at, int column, const QString& title, const QString& name, int digits) {
        sprite->addWidget(new QLabel(title), at, column, Qt::AlignRight);
        sprite->addWidget(field(name, digits), at, column + 1, Qt::AlignLeft);
    };
    cell(0, 1, tr("X:"), "X", 4);
    cell(1, 1, tr("Y:"), "Y", 4);
    cell(2, 1, tr("Mag X:"), "MagX", 1);
    cell(3, 1, tr("Mag Y:"), "MagY", 1);
    cell(0, 3, tr("PRI:"), "PRI", 2);
    cell(1, 3, tr("SSS:"), "SSS", 2);
    cell(2, 3, tr("SSC:"), "SSC", 2);
    cell(3, 3, tr("IVR:"), "IVR", 2);
    cell(4, 3, tr("SSSA:"), "SSSA", 4);
    lower->addWidget(spriteBox_);
    for (int channel = 0; channel < 3; ++channel) {
        auto* box = new QGroupBox(tr("DMA Chan %1").arg(channel));
        auto* inside = new QGridLayout(box);
        dmaEnabled_[channel] = indicator(tr("Enabled"), qPrintable(QString("ckDma%1").arg(channel)));
        inside->addWidget(dmaEnabled_[channel], 0, 0, 1, 2);
        inside->addWidget(new QLabel(tr("Addr:")), 1, 0, Qt::AlignRight);
        inside->addWidget(field(QString("Addr%1").arg(channel), 4), 1, 1);
        inside->addWidget(new QLabel(tr("Pause:")), 2, 0, Qt::AlignRight);
        inside->addWidget(field(QString("Pause%1").arg(channel), 2), 2, 1);
        dmaNext_[channel] = new QLabel;
        dmaNext_[channel]->setObjectName(QString("lDma%1").arg(channel));
        dmaNext_[channel]->setAlignment(Qt::AlignCenter);
        dmaNext_[channel]->setStyleSheet("color: purple;");
        inside->addWidget(dmaNext_[channel], 3, 0, 1, 2);
        dmaBox_[channel] = box;
        lower->addWidget(box);
    }
    asic->addLayout(lower);
    layout->addWidget(asicPane_);
    asicPane_->hide();

    connect(asicButton_, &QToolButton::toggled, this, [this](bool on) {
        asicButton_->setArrowType(on ? Qt::UpArrow : Qt::DownArrow);
        asicPane_->setVisible(on);
        refresh();
    });
    connect(spriteBar_, &QScrollBar::valueChanged, this, &RegistersDialog::refresh);
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

bool RegistersDialog::asicShown() const
{
    return asicButton_->isChecked();
}

void RegistersDialog::setAsicShown(bool shown)
{
    asicButton_->setChecked(shown);
}

int RegistersDialog::sprite() const
{
    return spriteBar_->value();
}

void RegistersDialog::setSprite(int number)
{
    spriteBar_->setValue(number);
}

QString RegistersDialog::dmaInstruction(int channel) const
{
    return dmaNext_[channel % 3]->text();
}

void RegistersDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    refresh();
}

void RegistersDialog::refresh()
{
    struct State {
        uint8_t ink[17], pen, psg[16], psgSelected, crtc[16], crtcSelected;
        uint8_t vcc, vlc, r52, vsc, hcc, hsc, vtac, mode, icsr;
        int hdc, vdur;
        uint16_t vma;
        bool hsync, vsync, adjust, plus;
        uint16_t colours[32];
        bool unlocked;
        uint8_t rmr2, dcsr, pri, sss, ssc, ivr;
        uint16_t sssa;
        tuxape::Asic::Sprite sprite;
        uint8_t pixels[256];
        tuxape::Asic::Channel dma[3];
        uint16_t next[3];
    };
    const int spriteNumber = spriteBar_->value();
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
        // Where in memory the byte being shown comes from.
        const unsigned ma = crtc.ma();
        out.vma = static_cast<uint16_t>((ma & 0x3000) << 2 | (crtc.ra() & 7) << 11 | (ma & 0x3FF) << 1);
        out.plus = cpc.plus();
        out.icsr = ga.interruptRequested() ? 0x80 : 0;
        if (out.plus) {
            const tuxape::Asic& asic = cpc.asic();
            out.icsr = static_cast<uint8_t>(out.icsr | (asic.dcsr() & 0x70));
            for (int index = 0; index < 32; ++index)
                out.colours[index] = asic.colour(index);
            out.unlocked = asic.unlocked();
            out.rmr2 = asic.rmr2();
            out.dcsr = asic.dcsr();
            out.pri = asic.rasterInterruptLine();
            out.sss = asic.splitLine();
            out.ssc = asic.scroll();
            out.ivr = asic.interruptVector();
            out.sssa = asic.splitAddress();
            out.sprite = asic.sprite(spriteNumber);
            for (int i = 0; i < 256; ++i)
                out.pixels[i] = asic.spritePixel(spriteNumber, i & 15, i >> 4);
            for (int channel = 0; channel < 3; ++channel) {
                out.dma[channel] = asic.channel(channel);
                const uint16_t at = out.dma[channel].address;
                out.next[channel] = static_cast<uint16_t>(cpc.memory().readRam(at) | cpc.memory().readRam(static_cast<uint16_t>(at + 1)) << 8);
            }
        }
        return out;
    });

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

    // The ASIC's part is a Plus's alone.
    asicButton_->setEnabled(s.plus);
    if (!s.plus && asicButton_->isChecked())
        asicButton_->setChecked(false);
    if (!s.plus || !asicPane_->isVisible())
        return;
    for (int index = 0; index < 32; ++index) {
        QLabel* square = colours_[index];
        const QRgb rgb = rgbOf(s.colours[index]);
        QPalette look = square->palette();
        if (look.color(QPalette::Window).rgb() != rgb) {
            look.setColor(QPalette::Window, QColor(rgb));
            square->setPalette(look);
        }
        square->setToolTip(tr("Red %1, Green %2, Blue %3").arg(s.colours[index] >> 4 & 0xF).arg(s.colours[index] >> 8 & 0xF).arg(s.colours[index] & 0xF));
    }
    unlocked_->setChecked(s.unlocked);
    const int position = s.rmr2 >> 3 & 3;
    ramEnabled_->setChecked(position == 3);
    put("LRB", position == 1 ? 0x4000u : position == 2 ? 0x8000u : 0u);
    put("CartBank", s.rmr2 & 7u);
    put("DCSR", s.dcsr);
    spriteBox_->setTitle(tr("Sprite %1").arg(spriteNumber));
    QImage image(16, 16, QImage::Format_RGB32);
    for (int i = 0; i < 256; ++i) {
        // Colour 0 lets the screen through: shown as a grey chequer.
        const uint8_t pixel = s.pixels[i] & 15;
        const QRgb through = ((i ^ i >> 4) & 1) ? qRgb(96, 96, 96) : qRgb(72, 72, 72);
        image.setPixel(i & 15, i >> 4, pixel ? rgbOf(s.colours[16 + pixel]) : through);
    }
    spriteView_->setPixmap(QPixmap::fromImage(image.scaled(64, 64)));
    put("X", static_cast<uint16_t>(s.sprite.x));
    put("Y", static_cast<uint16_t>(s.sprite.y));
    put("MagX", s.sprite.magX);
    put("MagY", s.sprite.magY);
    put("PRI", s.pri);
    put("SSS", s.sss);
    put("SSC", s.ssc);
    put("IVR", s.ivr);
    put("SSSA", s.sssa);
    for (int channel = 0; channel < 3; ++channel) {
        const tuxape::Asic::Channel& dma = s.dma[channel];
        dmaEnabled_[channel]->setChecked((s.dcsr >> channel & 1) != 0);
        put(QString("Addr%1").arg(channel), dma.address);
        put(QString("Pause%1").arg(channel), dma.prescaler);
        dmaNext_[channel]->setText(dmaText(s.next[channel]));
        dmaBox_[channel]->setToolTip(tr("Pause counter %1, loop start %2, loop count %3")
                                         .arg(dma.pause)
                                         .arg(QString("%1").arg(dma.loopStart, 4, 16, QLatin1Char('0')).toUpper())
                                         .arg(dma.repeats));
    }
}
