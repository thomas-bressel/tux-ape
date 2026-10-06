#include "graphicsdialog.h"

#include <algorithm>

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "emulator.h"

namespace {

// The bits of a screen byte that make each pixel, lowest bit of the pen
// first, as the Gate Array has them.
const int kMode0Bits[2][4] = {{7, 3, 5, 1}, {6, 2, 4, 0}};
const int kMode1Bits[4][2] = {{7, 3}, {6, 2}, {5, 1}, {4, 0}};

int pixelsPerByte(int mode)
{
    return mode == 0 ? 2 : mode == 1 ? 4 : mode == 2 ? 8 : 1;
}

int bitsPerPixel(int mode)
{
    return mode == 0 ? 4 : mode == 1 ? 2 : mode == 2 ? 1 : 4;
}

// The pen of pixel `x` of a byte.
int penOf(uint8_t byte, int x, int mode, GraphicsDialog::Encoding encoding)
{
    if (mode == GraphicsDialog::kSpriteMode)
        return byte & 15;
    const int bits = bitsPerPixel(mode);
    if (encoding == GraphicsDialog::Linear)
        return byte >> (8 - bits * (x + 1)) & ((1 << bits) - 1);
    if (encoding == GraphicsDialog::Reverse)
        return byte >> (bits * x) & ((1 << bits) - 1);
    if (mode == 2)
        return byte >> (7 - x) & 1;
    int pen = 0;
    for (int bit = 0; bit < bits; ++bit)
        pen |= (byte >> (mode == 0 ? kMode0Bits[x][bit] : kMode1Bits[x][bit]) & 1) << bit;
    return pen;
}

// The byte with its pixel `x` given a pen.
uint8_t withPen(uint8_t byte, int x, int pen, int mode, GraphicsDialog::Encoding encoding)
{
    if (mode == GraphicsDialog::kSpriteMode)
        return static_cast<uint8_t>(pen & 15);
    const int bits = bitsPerPixel(mode);
    const int mask = (1 << bits) - 1;
    if (encoding == GraphicsDialog::Linear || encoding == GraphicsDialog::Reverse) {
        const int shift = encoding == GraphicsDialog::Linear ? 8 - bits * (x + 1) : bits * x;
        return static_cast<uint8_t>((byte & ~(mask << shift)) | (pen & mask) << shift);
    }
    if (mode == 2)
        return static_cast<uint8_t>((byte & ~(0x80 >> x)) | (pen & 1) << (7 - x));
    for (int bit = 0; bit < bits; ++bit) {
        const int place = mode == 0 ? kMode0Bits[x][bit] : kMode1Bits[x][bit];
        byte = static_cast<uint8_t>((byte & ~(1 << place)) | (pen >> bit & 1) << place);
    }
    return byte;
}

// The address of the screen line under the one at `address`.
unsigned nextScreenLine(unsigned address)
{
    const unsigned next = address + 0x800;
    return (next & 0x3800) ? next & 0xFFFF : (address & 0xC000) | ((next + 0x50) & 0x3FFF);
}

}  // namespace

GraphicsDialog::GraphicsDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("Find Graphics"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    auto* layout = new QVBoxLayout(this);
    auto* top = new QGridLayout;
    const auto spin = [](const char* objectName, int least, int most, int value) {
        auto* box = new QSpinBox;
        box->setObjectName(objectName);
        box->setRange(least, most);
        box->setValue(value);
        return box;
    };
    modeBox_ = new QComboBox;
    modeBox_->setObjectName("cbMode");
    modeBox_->addItems({tr("Current"), "0", "1", "2", tr("Sprite")});
    zoomBox_ = spin("seZoom", 1, 16, 3);
    addressBox_ = spin("seAddress", 0, 0xFFFF, 0xC000);
    addressBox_->setDisplayIntegerBase(16);
    addressBox_->setWrapping(true);
    QFont upper = addressBox_->font();
    upper.setCapitalization(QFont::AllUppercase);
    addressBox_->setFont(upper);
    widthBox_ = spin("seWidth", 1, 80, 2);
    heightBox_ = spin("seHeight", 1, 200, 8);
    encodingBox_ = new QComboBox;
    encodingBox_->setObjectName("cbEncoding");
    encodingBox_->addItems({tr("CPC"), tr("Screen"), tr("Linear"), tr("Reverse")});
    const auto place = [&](int row, int column, const QString& title, QWidget* widget) {
        top->addWidget(new QLabel(title), row, column * 2, Qt::AlignRight);
        top->addWidget(widget, row, column * 2 + 1);
    };
    place(0, 0, tr("Mode:"), modeBox_);
    place(0, 1, tr("Zoom:"), zoomBox_);
    place(0, 2, tr("Address:"), addressBox_);
    place(1, 0, tr("Width:"), widthBox_);
    place(1, 1, tr("Height:"), heightBox_);
    place(1, 2, tr("Encoding:"), encodingBox_);
    layout->addLayout(top);

    view_ = new QLabel;
    view_->setObjectName("GraphicsView");
    view_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    view_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    view_->setMinimumSize(340, 130);
    view_->setMouseTracking(true);
    view_->installEventFilter(this);
    layout->addWidget(view_, 1);
    bar_ = new QScrollBar(Qt::Horizontal);
    bar_->setObjectName("sbAddress");
    bar_->setRange(0, 0xFFFF);
    layout->addWidget(bar_);

    auto* bottom = new QHBoxLayout;
    addressLabel_ = new QLabel(tr("Address: %1").arg("0000"));
    addressLabel_->setObjectName("lAddress");
    addressLabel_->setMinimumWidth(100);
    bottom->addWidget(addressLabel_);
    const auto tool = [&](const char* objectName, const QString& text, const QString& tip) {
        auto* button = new QToolButton;
        button->setObjectName(objectName);
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        bottom->addWidget(button);
        return button;
    };
    selectTool_ = tool("bSelect", QString(QChar(0x2196)), tr("Select"));
    brushTool_ = tool("bBrush", QString(QChar(0x270E)), tr("Paintbrush"));
    fillTool_ = tool("bFill", QString(QChar(0x25A7)), tr("Fill"));
    selectTool_->setChecked(true);
    penSwatch_ = new QLabel;
    penSwatch_->setObjectName("PenSwatch");
    penSwatch_->setFixedSize(16, 28);
    penSwatch_->setAutoFillBackground(true);
    bottom->addWidget(penSwatch_);
    swatches_ = new QWidget;
    swatches_->setObjectName("Swatches");
    auto* swatchRow = new QHBoxLayout(swatches_);
    swatchRow->setContentsMargins(0, 0, 0, 0);
    swatchRow->setSpacing(0);
    for (int pen = 0; pen < 16; ++pen) {
        auto* swatch = new QLabel;
        swatch->setObjectName(QString("pen%1").arg(pen));
        swatch->setFixedSize(12, 14);
        swatch->setAutoFillBackground(true);
        swatch->installEventFilter(this);
        swatchRow->addWidget(swatch);
    }
    bottom->addWidget(swatches_);
    bottom->addStretch();
    auto* close = new QPushButton(tr("&Close"));
    close->setObjectName("bClose");
    bottom->addWidget(close);
    layout->addLayout(bottom);
    resize(380, 290);

    const auto changed = [this] {
        {
            const QSignalBlocker blocker(bar_);
            bar_->setValue(addressBox_->value());
        }
        refresh();
    };
    connect(modeBox_, &QComboBox::currentIndexChanged, this, changed);
    connect(encodingBox_, &QComboBox::currentIndexChanged, this, changed);
    for (QSpinBox* box : {zoomBox_, addressBox_, widthBox_, heightBox_})
        connect(box, &QSpinBox::valueChanged, this, changed);
    connect(bar_, &QScrollBar::valueChanged, this, [this](int value) { addressBox_->setValue(value); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    // The program goes on drawing while the window is up.
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible() && !emulator_->isPaused())
            refresh();
    });
    timer->start(250);
    changed();
}

void GraphicsDialog::setView(int mode, int zoom, unsigned address, int width, int height, Encoding encoding)
{
    modeBox_->setCurrentIndex(mode + 1);
    zoomBox_->setValue(zoom);
    addressBox_->setValue(static_cast<int>(address & 0xFFFF));
    widthBox_->setValue(width);
    heightBox_->setValue(height);
    encodingBox_->setCurrentIndex(encoding);
    refresh();
}

int GraphicsDialog::mode() const
{
    return modeBox_->currentIndex() == 0 ? machineMode_ : modeBox_->currentIndex() - 1;
}

int GraphicsDialog::zoom() const
{
    return zoomBox_->value();
}

unsigned GraphicsDialog::address() const
{
    return static_cast<unsigned>(addressBox_->value());
}

int GraphicsDialog::tileWidth() const
{
    return widthBox_->value();
}

int GraphicsDialog::tileHeight() const
{
    return heightBox_->value();
}

GraphicsDialog::Encoding GraphicsDialog::encoding() const
{
    return static_cast<Encoding>(encodingBox_->currentIndex());
}

int GraphicsDialog::pixelsWide() const
{
    return tileWidth() * pixelsPerByte(mode());
}

int GraphicsDialog::pensInMode() const
{
    return 1 << bitsPerPixel(mode());
}

unsigned GraphicsDialog::tileAddress(int tile) const
{
    return byteAddress(tile, 0, 0);
}

// Where the byte of a tile is. Lines follow one another in memory, tile
// after tile; or, as on the screen, tiles stand side by side and lines are
// &800 apart.
unsigned GraphicsDialog::byteAddress(int tile, int column, int line) const
{
    if (encoding() != Screen)
        return (address() + static_cast<unsigned>((tile * tileHeight() + line) * tileWidth() + column)) & 0xFFFF;
    unsigned at = address() + static_cast<unsigned>((tile % columns_) * tileWidth());
    for (int n = (tile / columns_) * tileHeight() + line; n > 0; --n)
        at = nextScreenLine(at & 0xFFFF);
    return (at + static_cast<unsigned>(column)) & 0xFFFF;
}

int GraphicsDialog::pen(int tile, int x, int y) const
{
    const int perByte = pixelsPerByte(mode());
    return penOf(memory_[byteAddress(tile, x / perByte, y)], x % perByte, mode(), encoding());
}

QRgb GraphicsDialog::colour(int pen) const
{
    return colours_[static_cast<size_t>(pen & 15)];
}

void GraphicsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    refresh();
}

void GraphicsDialog::refresh()
{
    const bool sprites = modeBox_->currentIndex() - 1 == kSpriteMode;
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        for (int a = 0; a < 0x10000; ++a)
            memory_[static_cast<size_t>(a)] = cpc.memory().readRam(static_cast<uint16_t>(a));
        machineMode_ = cpc.gateArray().mode() & 3;
        if (machineMode_ == 3)
            machineMode_ = 0;
        for (int p = 0; p < 16; ++p) {
            if (!sprites) {
                colours_[static_cast<size_t>(p)] = cpc.gateArray().colour(cpc.gateArray().ink(p)) | 0xFF000000;
            } else if (cpc.plus()) {
                // A Plus's sprites have a palette of their own; pen 0 lets
                // the screen through.
                const uint16_t grb = cpc.asic().colour(16 + p);
                colours_[static_cast<size_t>(p)] = p ? qRgb((grb >> 4 & 15) * 17, (grb >> 8 & 15) * 17, (grb & 15) * 17) : qRgb(64, 64, 64);
            } else {
                colours_[static_cast<size_t>(p)] = qRgb(p * 17, p * 17, p * 17);
            }
        }
    });
    pen_ = std::min(pen_, pensInMode() - 1);
    drawView();
    updateSwatches();
}

// How many tiles the view has room for.
void GraphicsDialog::layoutTiles()
{
    const int unit = zoom();
    const int wide = mode() == 0 ? 2 * unit : mode() == 2 ? std::max(1, unit / 2) : unit;
    const int tileW = pixelsWide() * wide + 1, tileH = tileHeight() * unit + 1;
    columns_ = std::max(1, (view_->width() - 2) / tileW);
    rows_ = std::max(1, (view_->height() - 2) / tileH);
}

QImage GraphicsDialog::picture() const
{
    const int unit = zoom();
    const int wide = mode() == 0 ? 2 * unit : mode() == 2 ? std::max(1, unit / 2) : unit;
    const int across = pixelsWide(), down = tileHeight();
    const int tileW = across * wide + 1, tileH = down * unit + 1;
    QImage image(columns_ * tileW, rows_ * tileH, QImage::Format_RGB32);
    image.fill(Qt::white);  // the lines between the tiles
    QPainter painter(&image);
    for (int tile = 0; tile < tileCount(); ++tile) {
        const int left = (tile % columns_) * tileW, topEdge = (tile / columns_) * tileH;
        for (int y = 0; y < down; ++y)
            for (int x = 0; x < across; ++x)
                painter.fillRect(left + x * wide, topEdge + y * unit, wide, unit, QColor(colour(pen(tile, x, y))));
    }
    return image;
}

void GraphicsDialog::drawView()
{
    layoutTiles();
    view_->setPixmap(QPixmap::fromImage(picture()));
    bar_->setSingleStep(tileWidth() * tileHeight());
    bar_->setPageStep(std::max(1, tileCount() * tileWidth() * tileHeight()));
}

void GraphicsDialog::updateSwatches()
{
    const auto paint = [](QWidget* widget, QRgb rgb) {
        QPalette look = widget->palette();
        look.setColor(QPalette::Window, QColor(rgb));
        widget->setPalette(look);
    };
    paint(penSwatch_, colour(pen_));
    for (int p = 0; p < 16; ++p) {
        auto* swatch = swatches_->findChild<QLabel*>(QString("pen%1").arg(p));
        swatch->setVisible(p < pensInMode());
        paint(swatch, colour(p));
    }
}

void GraphicsDialog::setCurrentPen(int pen)
{
    pen_ = std::clamp(pen, 0, pensInMode() - 1);
    updateSwatches();
}

void GraphicsDialog::setPixel(int tile, int x, int y, int pen)
{
    const int perByte = pixelsPerByte(mode());
    const unsigned at = byteAddress(tile, x / perByte, y);
    const uint8_t value = withPen(memory_[at], x % perByte, pen, mode(), encoding());
    memory_[at] = value;
    emulator_->withMachine([&](tuxape::Cpc& cpc) { cpc.memory().write(static_cast<uint16_t>(at), value); });
}

void GraphicsDialog::paintPixel(int tile, int x, int y)
{
    if (tile < 0 || tile >= tileCount() || x < 0 || x >= pixelsWide() || y < 0 || y >= tileHeight())
        return;
    setPixel(tile, x, y, pen_);
    drawView();
}

// The pixels of the tile that touch one another in the colour of the one
// clicked take the pen chosen.
void GraphicsDialog::fillFrom(int tile, int x, int y)
{
    if (tile < 0 || tile >= tileCount() || x < 0 || x >= pixelsWide() || y < 0 || y >= tileHeight())
        return;
    const int old = pen(tile, x, y);
    if (old == pen_)
        return;
    std::vector<QPoint> todo = {QPoint(x, y)};
    while (!todo.empty()) {
        const QPoint at = todo.back();
        todo.pop_back();
        if (at.x() < 0 || at.x() >= pixelsWide() || at.y() < 0 || at.y() >= tileHeight() || pen(tile, at.x(), at.y()) != old)
            continue;
        setPixel(tile, at.x(), at.y(), pen_);
        for (const QPoint step : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)})
            todo.push_back(at + step);
    }
    drawView();
}

// The tile and the pixel at a place of the view.
bool GraphicsDialog::locate(const QPoint& at, int* tile, int* x, int* y) const
{
    const int unit = zoom();
    const int wide = mode() == 0 ? 2 * unit : mode() == 2 ? std::max(1, unit / 2) : unit;
    const int tileW = pixelsWide() * wide + 1, tileH = tileHeight() * unit + 1;
    const QPoint inside = at - QPoint(view_->frameWidth(), view_->frameWidth());
    if (inside.x() < 0 || inside.y() < 0 || inside.x() >= columns_ * tileW || inside.y() >= rows_ * tileH)
        return false;
    *tile = inside.y() / tileH * columns_ + inside.x() / tileW;
    *x = std::min(inside.x() % tileW / wide, pixelsWide() - 1);
    *y = std::min(inside.y() % tileH / unit, tileHeight() - 1);
    return true;
}

bool GraphicsDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == view_ && event->type() == QEvent::Resize) {
        QTimer::singleShot(0, this, [this] { drawView(); });
    } else if (watched == view_ && (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress)) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        int tile = 0, x = 0, y = 0;
        if (locate(mouse->position().toPoint(), &tile, &x, &y)) {
            // The address of the byte under the pointer.
            const unsigned at = byteAddress(tile, x / pixelsPerByte(mode()), y);
            addressLabel_->setText(tr("Address: %1").arg(QString("%1").arg(at, 4, 16, QLatin1Char('0')).toUpper()));
            const bool pressed = event->type() == QEvent::MouseButtonPress || (mouse->buttons() & Qt::LeftButton);
            if (pressed && brushTool_->isChecked())
                paintPixel(tile, x, y);
            else if (event->type() == QEvent::MouseButtonPress && fillTool_->isChecked())
                fillFrom(tile, x, y);
        }
    } else if (event->type() == QEvent::MouseButtonPress && watched->parent() == swatches_) {
        setCurrentPen(watched->objectName().mid(3).toInt());
    }
    return QDialog::eventFilter(watched, event);
}
