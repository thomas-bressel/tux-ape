#include "asicdialog.h"

#include <algorithm>
#include <functional>

#include <QCheckBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "contrast.h"
#include "core/cpc.h"
#include "emulator.h"

namespace {

QRgb rgbOf(uint16_t grb)
{
    return qRgb((grb >> 4 & 0xF) * 17, (grb >> 8 & 0xF) * 17, (grb & 0xF) * 17);
}

// The bytes that unlock the ASIC, written to the CRTC's register select
// port; with another byte in the place of &CD, they lock it.
constexpr uint8_t kAsicKey[17] = {0xFF, 0x00, 0xFF, 0x77, 0xB3, 0x51, 0xA8, 0xD4, 0x62,
                                  0x39, 0x9C, 0x46, 0x2B, 0x15, 0x8A, 0xCD, 0xEE};
// The room of the sprite as shown, at a screen pixel to a mode 2 pixel,
// as TuxAPE's screen at its own size: one magnified 4 x 4 fills it.
constexpr int kShownWidth = 64, kShownHeight = 128;

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

// The grey chequer that stands for colour 0, which lets the screen through.
void paintThrough(QPainter& painter, const QRectF& area, qreal square)
{
    painter.save();
    painter.setClipRect(area);
    painter.fillRect(area, QColor(72, 72, 72));
    for (int row = 0; row * square < area.height(); ++row)
        for (int column = row & 1; column * square < area.width(); column += 2)
            painter.fillRect(QRectF(area.left() + column * square, area.top() + row * square, square, square), QColor(96, 96, 96));
    painter.restore();
}

// What a sprite shows: its own colours, colour 0 clear.
QImage spriteImage(const uint8_t* pixels, const uint16_t* colours)
{
    QImage image(16, 16, QImage::Format_ARGB32);
    for (int i = 0; i < 256; ++i) {
        const uint8_t pen = pixels[i] & 15;
        image.setPixel(i & 15, i >> 4, pen ? rgbOf(colours[16 + pen]) : qRgba(0, 0, 0, 0));
    }
    return image;
}

}  // namespace

// What the sixteen sprites have, for the board and the map to draw.
struct SpriteState {
    std::array<QImage, 16> images;
    std::array<tuxape::Asic::Sprite, 16> sprites{};
    int selected = 0;
    int width = 640, lines = 200;  // the picture the CRTC shows, in mode 2 pixels and lines
};

// The sixteen sprites side by side, two rows of eight: each as it is
// drawn, twice its size, its number and its magnification under it, or
// "off" when it is not shown. A click chooses one.
class SpriteBoard : public QWidget {
public:
    static constexpr int kCellWidth = 44, kCellHeight = 54, kPicture = 32;

    explicit SpriteBoard(const SpriteState& state) : state_(state)
    {
        setObjectName("SpriteBoard");
        setFixedSize(8 * kCellWidth, 2 * kCellHeight);
        setToolTip(tr("The sixteen hardware sprites, as the machine has them: click one to see its registers"));
    }
    std::function<void(int)> chosen;

    QRect cell(int n) const { return QRect((n & 7) * kCellWidth, (n >> 3) * kCellHeight, kCellWidth, kCellHeight); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const QColor window = palette().color(QPalette::Window);
        const QColor text = readable(palette().color(QPalette::WindowText), window);
        const QColor mark = readable(palette().color(QPalette::Highlight), window, kGraphicContrast);
        QFont small = font();
        small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1));
        for (int n = 0; n < 16; ++n) {
            const QRect box = cell(n);
            const tuxape::Asic::Sprite& sprite = state_.sprites[static_cast<size_t>(n)];
            const bool shown = sprite.magX && sprite.magY;
            const QRect picture(box.left() + (kCellWidth - kPicture) / 2, box.top() + 3, kPicture, kPicture);
            paintThrough(painter, picture, kPicture / 8.0);
            painter.setOpacity(shown ? 1.0 : 0.35);
            painter.drawImage(picture, state_.images[static_cast<size_t>(n)]);
            painter.setOpacity(1.0);
            QFont face = small;
            face.setBold(n == state_.selected);
            painter.setFont(face);
            painter.setPen(text);
            const QString what = shown ? QString("%1  %2×%3").arg(n).arg(sprite.magX).arg(sprite.magY) : tr("%1  off").arg(n);
            painter.drawText(QRect(box.left(), picture.bottom() + 2, box.width(), box.bottom() - picture.bottom() - 2),
                             Qt::AlignHCenter | Qt::AlignTop, what);
            // The one whose registers are shown: a frame round it, and its
            // number in bold.
            if (n == state_.selected) {
                painter.setPen(QPen(mark, 2));
                painter.drawRect(box.adjusted(1, 1, -1, -1));
            }
        }
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        for (int n = 0; n < 16; ++n)
            if (cell(n).contains(event->position().toPoint()) && chosen)
                chosen(n);
    }

private:
    const SpriteState& state_;
};

// The picture the CRTC shows, with a margin of border round it, and each
// sprite drawn where it stands, as large as its magnification makes it:
// sprite 0 above the others, as the ASIC shows them. The one whose
// registers are shown has a dashed frame. A click chooses the one on top
// there.
class SpriteMap : public QWidget {
public:
    static constexpr int kMarginX = 32, kMarginY = 16;  // the border shown, in mode 2 pixels and lines

    explicit SpriteMap(const SpriteState& state) : state_(state)
    {
        setObjectName("SpriteMap");
        setFixedSize(400, 240);
        setToolTip(tr("Where the sprites stand on the picture, the border round it: click one to see its registers"));
    }
    std::function<void(int)> chosen;

    // A sprite's place in the widget; empty when it is not shown.
    QRectF place(int n) const
    {
        const tuxape::Asic::Sprite& sprite = state_.sprites[static_cast<size_t>(n)];
        if (!sprite.magX || !sprite.magY)
            return {};
        const Frame f = frame();
        return QRectF(f.left + (sprite.x + kMarginX) * f.scale, f.top + (sprite.y + kMarginY) * 2 * f.scale,
                      16 * sprite.magX * f.scale, 16 * sprite.magY * 2 * f.scale);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const Frame f = frame();
        const QColor border(24, 24, 24), picture(56, 56, 56);
        const QRectF all(f.left, f.top, (state_.width + 2 * kMarginX) * f.scale, (state_.lines + 2 * kMarginY) * 2 * f.scale);
        const QRectF shown(f.left + kMarginX * f.scale, f.top + kMarginY * 2 * f.scale, state_.width * f.scale,
                           state_.lines * 2 * f.scale);
        painter.fillRect(rect(), palette().color(QPalette::Window));
        painter.fillRect(all, border);
        painter.fillRect(shown, picture);
        painter.setPen(readable(QColor(150, 150, 150), border, kGraphicContrast));
        painter.drawRect(shown);
        painter.setClipRect(all);
        for (int n = 15; n >= 0; --n) {
            const QRectF at = place(n);
            if (at.isEmpty())
                continue;
            painter.drawImage(at, state_.images[static_cast<size_t>(n)]);
        }
        // The numbers, white on black, and the chosen one's frame.
        QFont small = font();
        small.setPointSizeF(std::max(6.0, small.pointSizeF() - 2));
        painter.setFont(small);
        for (int n = 15; n >= 0; --n) {
            const QRectF at = place(n);
            if (at.isEmpty())
                continue;
            const QString number = QString::number(n);
            const QRectF label(at.topLeft(), QSizeF(painter.fontMetrics().horizontalAdvance(number) + 4, painter.fontMetrics().height()));
            painter.fillRect(label, Qt::black);
            painter.setPen(Qt::white);
            painter.drawText(label, Qt::AlignCenter, number);
            if (n == state_.selected) {
                painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
                painter.drawRect(at.adjusted(-1, -1, 1, 1));
            }
        }
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        for (int n = 0; n < 16; ++n)
            if (place(n).contains(event->position()) && chosen) {
                chosen(n);
                return;
            }
    }

private:
    struct Frame {
        qreal scale, left, top;
    };
    // Mode 2 pixels across, lines down, twice as tall: the picture and its
    // border as large as the widget lets them be, in its middle.
    Frame frame() const
    {
        const qreal across = state_.width + 2 * kMarginX, down = 2.0 * (state_.lines + 2 * kMarginY);
        const qreal scale = std::min(width() / across, height() / down);
        return {scale, (width() - across * scale) / 2, (height() - down * scale) / 2};
    }

    const SpriteState& state_;
};

AsicDialog::AsicDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
    , state_(std::make_unique<SpriteState>())
{
    setWindowTitle(tr("TuxAPE - ASIC Registers"));

    auto* layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    notPlus_ = new QLabel(tr("This machine is not a Plus: it has no ASIC."));
    notPlus_->setObjectName("lNotPlus");
    layout->addWidget(notPlus_);
    body_ = new QWidget;
    auto* asic = new QVBoxLayout(body_);
    asic->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(body_);

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
    unlocked_ = new QCheckBox(tr("ASIC Unlocked"));
    unlocked_->setObjectName("ckUnlocked");
    unlocked_->setToolTip(tr("Sends the ASIC the sequence that unlocks it, or locks it"));
    ramEnabled_ = new QCheckBox(tr("ASIC RAM Enabled"));
    ramEnabled_->setObjectName("ckRamEnabled");
    connect(unlocked_, &QCheckBox::clicked, this, [this](bool on) { setUnlocked(on); });
    connect(ramEnabled_, &QCheckBox::clicked, this, [this](bool on) { setRamEnabled(on); });
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

    auto* middle = new QHBoxLayout;
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
    // The sprite as the screen shows it: the magnification gives it its
    // size and its shape.
    spriteShown_ = new QLabel;
    spriteShown_->setObjectName("SpriteShown");
    spriteShown_->setFixedSize(kShownWidth + 6, kShownHeight + 6);
    spriteShown_->setAlignment(Qt::AlignCenter);
    spriteShown_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    sprite->addWidget(spriteShown_, 0, 3, 5, 1);
    const auto cell = [&](QGridLayout* into, int at, int column, const QString& title, const QString& name, int digits,
                          bool editable) {
        into->addWidget(new QLabel(title), at, column, Qt::AlignRight);
        into->addWidget(field(name, digits, editable), at, column + 1, Qt::AlignLeft);
    };
    cell(sprite, 0, 1, tr("X:"), "X", 4, true);
    cell(sprite, 1, 1, tr("Y:"), "Y", 4, true);
    cell(sprite, 2, 1, tr("Mag X:"), "MagX", 1, true);
    cell(sprite, 3, 1, tr("Mag Y:"), "MagY", 1, true);
    fields_.value("MagX")->setToolTip(tr("0 hides the sprite; 1, 2 or 4 is the width of its pixels"));
    fields_.value("MagY")->setToolTip(tr("0 hides the sprite; 1, 2 or 4 is the height of its pixels"));
    middle->addWidget(spriteBox_);
    // The raster interrupt and the split screen, as WinAPE has them beside
    // the sprite's registers.
    auto* rasterBox = new QGroupBox(tr("Raster"));
    rasterBox->setObjectName("gbRaster");
    auto* raster = new QGridLayout(rasterBox);
    cell(raster, 0, 0, tr("PRI:"), "PRI", 2, false);
    cell(raster, 1, 0, tr("SSS:"), "SSS", 2, false);
    cell(raster, 2, 0, tr("SSC:"), "SSC", 2, false);
    cell(raster, 3, 0, tr("IVR:"), "IVR", 2, false);
    cell(raster, 4, 0, tr("SSSA:"), "SSSA", 4, false);
    middle->addWidget(rasterBox);
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
        dmaNext_[channel]->setStyleSheet(
            QString("color: %1;").arg(readable(QColor(128, 0, 128), QGuiApplication::palette().color(QPalette::Window)).name()));
        inside->addWidget(dmaNext_[channel], 3, 0, 1, 2);
        dmaBox_[channel] = box;
        middle->addWidget(box);
    }
    asic->addLayout(middle);

    // TuxAPE's own: the sixteen sprites, and where they stand.
    auto* spritesBox = new QGroupBox(tr("Hardware Sprites"));
    spritesBox->setObjectName("gbSprites");
    auto* sprites = new QHBoxLayout(spritesBox);
    board_ = new SpriteBoard(*state_);
    map_ = new SpriteMap(*state_);
    board_->chosen = map_->chosen = [this](int n) { setSprite(n); };
    sprites->addWidget(board_, 0, Qt::AlignTop);
    sprites->addWidget(map_, 0, Qt::AlignTop);
    asic->addWidget(spritesBox);

    connect(spriteBar_, &QScrollBar::valueChanged, this, &AsicDialog::refresh);
    // The machine goes on while the window is up: the sprites move as
    // they do on the screen.
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (isVisible() && !emulator_->isPaused())
            refresh();
    });
    timer_->start(40);
    refresh();
}

AsicDialog::~AsicDialog() = default;

QLineEdit* AsicDialog::field(const QString& name, int digits, bool editable)
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

void AsicDialog::put(const QString& name, unsigned value)
{
    QLineEdit* edit = fields_.value(name);
    // Not under the fingers of someone typing a value in.
    if (!edit || (edit->hasFocus() && edit->isModified()))
        return;
    const int digits = edit->maxLength();
    const unsigned mask = digits >= 8 ? ~0u : (1u << (digits * 4)) - 1;
    const QString text = QString("%1").arg(value & mask, digits, 16, QLatin1Char('0')).toUpper();
    if (edit->text() != text)
        edit->setText(text);
}

QString AsicDialog::value(const QString& name) const
{
    const QLineEdit* edit = fields_.value(name);
    return edit ? edit->text() : QString();
}

bool AsicDialog::setValue(const QString& name, const QString& hexText)
{
    bool ok = false;
    const unsigned value = hexText.toUInt(&ok, 16);
    // The sprite's position and magnification, written where a program
    // writes them: eight bytes to a sprite from &6000 in the ASIC's page.
    const bool position = name == "X" || name == "Y", magnification = name == "MagX" || name == "MagY";
    if (!ok || !(position || magnification) || (position && value > 0xFFFF)
        || (magnification && value != 0 && value != 1 && value != 2 && value != 4))
        return false;
    const int number = spriteBar_->value();
    bool plus = false;
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        if (!(plus = cpc.plus()))
            return;
        tuxape::Asic& asic = cpc.asic();
        const auto at = [&](int offset) { return static_cast<uint16_t>(0x6000 + number * 8 + offset); };
        if (position) {
            const int offset = name == "X" ? 0 : 2;
            asic.write(at(offset), static_cast<uint8_t>(value));
            asic.write(at(offset + 1), static_cast<uint8_t>(value >> 8));
        } else {
            const auto code = [](unsigned factor) { return factor == 4 ? 3u : factor; };
            const tuxape::Asic::Sprite& now = asic.sprite(number);
            const unsigned x = name == "MagX" ? value : now.magX, y = name == "MagY" ? value : now.magY;
            asic.write(at(4), static_cast<uint8_t>(code(x) << 2 | code(y)));
        }
    });
    refresh();
    return plus;
}

void AsicDialog::setUnlocked(bool on)
{
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        if (!cpc.plus())
            return;
        // The CRTC takes those bytes as register numbers: the one that was
        // chosen is chosen again after them.
        const uint8_t selected = cpc.crtc().selected();
        for (int n = 0; n < 17; ++n)
            cpc.out(0xBC00, n == 15 && !on ? 0xA5 : kAsicKey[n]);
        cpc.out(0xBC00, selected);
    });
    refresh();
}

bool AsicDialog::setRamEnabled(bool on)
{
    bool done = false;
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        if (!cpc.plus() || !cpc.asic().unlocked())
            return;
        const uint8_t rmr2 = cpc.asic().rmr2();
        const int position = rmr2 >> 3 & 3;
        if (on && position != 3)
            lowerRomPosition_ = position;
        const int wanted = on ? 3 : (position == 3 ? lowerRomPosition_ : position);
        // Through the Gate Array's port, as a program would: RMR2.
        cpc.out(0x7F00, static_cast<uint8_t>(0xA0 | wanted << 3 | (rmr2 & 7)));
        done = true;
    });
    refresh();
    return done;
}

int AsicDialog::sprite() const
{
    return spriteBar_->value();
}

void AsicDialog::setSprite(int number)
{
    spriteBar_->setValue(number);
}

QImage AsicDialog::spriteAsShown() const
{
    return shownImage_;
}

QString AsicDialog::dmaInstruction(int channel) const
{
    return channel >= 0 && channel < 3 ? dmaNext_[channel]->text() : QString();
}

QRect AsicDialog::boardCell(int sprite) const
{
    return sprite >= 0 && sprite < 16 ? board_->cell(sprite) : QRect();
}

QRect AsicDialog::mapPlace(int sprite) const
{
    return sprite >= 0 && sprite < 16 ? map_->place(sprite).toAlignedRect() : QRect();
}

void AsicDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    refresh();
}

void AsicDialog::refresh()
{
    struct State {
        bool plus, unlocked;
        uint16_t colours[32];
        uint8_t rmr2, dcsr, pri, sss, ssc, ivr;
        uint16_t sssa;
        tuxape::Asic::Sprite sprites[16];
        uint8_t pixels[16][256];
        tuxape::Asic::Channel dma[3];
        uint16_t next[3];
        int width, lines;
    };
    const int spriteNumber = spriteBar_->value();
    const State s = emulator_->withMachine([&](tuxape::Cpc& cpc) {
        State out{};
        out.plus = cpc.plus();
        const tuxape::Crtc& crtc = cpc.crtc();
        // The picture the CRTC shows; the usual one while it shows next to
        // none, as before the firmware has set it.
        const int columns = crtc.reg(1), lines = (crtc.reg(6) & 0x7F) * ((crtc.reg(9) & 0x1F) + 1);
        out.width = columns >= 8 ? std::min(columns, 64) * 16 : 640;
        out.lines = lines >= 16 ? std::min(lines, 312) : 200;
        if (!out.plus)
            return out;
        const tuxape::Asic& asic = cpc.asic();
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
        for (int n = 0; n < 16; ++n) {
            out.sprites[n] = asic.sprite(n);
            for (int i = 0; i < 256; ++i)
                out.pixels[n][i] = asic.spritePixel(n, i & 15, i >> 4);
        }
        for (int channel = 0; channel < 3; ++channel) {
            out.dma[channel] = asic.channel(channel);
            const uint16_t at = out.dma[channel].address;
            out.next[channel] = static_cast<uint16_t>(cpc.memory().readRam(at) | cpc.memory().readRam(static_cast<uint16_t>(at + 1)) << 8);
        }
        return out;
    });
    plus_ = s.plus;
    notPlus_->setVisible(!s.plus);
    body_->setEnabled(s.plus);
    if (!s.plus || !isVisible())
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
    // RMR2 is written only while the ASIC is unlocked.
    ramEnabled_->setEnabled(s.unlocked);
    ramEnabled_->setToolTip(s.unlocked ? tr("Shows the ASIC's page of registers at &4000-&7FFF, or the lower ROM where it was")
                                       : tr("Unlock the ASIC first"));
    put("LRB", position == 1 ? 0x4000u : position == 2 ? 0x8000u : 0u);
    put("CartBank", s.rmr2 & 7u);
    put("DCSR", s.dcsr);

    // The sprites, for the board and the map.
    SpriteState& state = *state_;
    for (int n = 0; n < 16; ++n) {
        state.images[static_cast<size_t>(n)] = spriteImage(s.pixels[n], s.colours);
        state.sprites[static_cast<size_t>(n)] = s.sprites[n];
    }
    state.selected = spriteNumber;
    state.width = s.width;
    state.lines = s.lines;
    board_->update();
    map_->update();

    const tuxape::Asic::Sprite& chosen = s.sprites[spriteNumber];
    spriteBox_->setTitle(tr("Sprite %1").arg(spriteNumber));
    QImage image(16, 16, QImage::Format_RGB32);
    for (int i = 0; i < 256; ++i) {
        // Colour 0 lets the screen through: shown as a grey chequer.
        const uint8_t pixel = s.pixels[spriteNumber][i] & 15;
        const QRgb through = ((i ^ i >> 4) & 1) ? qRgb(96, 96, 96) : qRgb(72, 72, 72);
        image.setPixel(i & 15, i >> 4, pixel ? rgbOf(s.colours[16 + pixel]) : through);
    }
    spriteView_->setPixmap(QPixmap::fromImage(image.scaled(64, 64)));
    // As shown: a sprite pixel is its magnification of mode 2 pixels wide
    // and of lines high, and a line is two mode 2 pixels high. Hidden, it
    // is drawn as if it were not magnified.
    const int wide = chosen.magX ? chosen.magX : 1, high = chosen.magY ? chosen.magY : 1;
    shownImage_ = image.scaled(kShownWidth * wide / 4, kShownHeight * high / 4);
    spriteShown_->setPixmap(QPixmap::fromImage(shownImage_));
    spriteShown_->setToolTip(chosen.magX && chosen.magY
                                 ? tr("As the screen shows it: %1 x %2 mode 2 pixels").arg(16 * chosen.magX).arg(16 * chosen.magY * 2)
                                 : tr("Not shown: a magnification is 0"));
    put("X", static_cast<uint16_t>(chosen.x));
    put("Y", static_cast<uint16_t>(chosen.y));
    put("MagX", chosen.magX);
    put("MagY", chosen.magY);
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
