#include "graphicsdialog.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
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
    view_->setFocusPolicy(Qt::StrongFocus);
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
    // The pen, and under it the paper.
    auto* chosen = new QVBoxLayout;
    chosen->setSpacing(0);
    const auto swatch = [&](const char* objectName, const QString& tip) {
        auto* label = new QLabel;
        label->setObjectName(objectName);
        label->setFixedSize(16, 14);
        label->setAutoFillBackground(true);
        label->setFrameStyle(QFrame::Box | QFrame::Plain);
        label->setToolTip(tip);
        chosen->addWidget(label);
        return label;
    };
    penSwatch_ = swatch("PenSwatch", tr("Pen: the left button draws with it"));
    paperSwatch_ = swatch("PaperSwatch", tr("Paper: the right button draws with it"));
    bottom->addLayout(chosen);
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
        swatch->setToolTip(tr("Left button: pen. Right button: paper."));
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

    // The right button's menu, for the tiles selected. Its keys work
    // wherever the pointer is in the window.
    const auto entry = [this](const char* objectName, const QString& text, const QKeySequence& keys, auto&& slot) {
        auto* action = new QAction(text, this);
        action->setObjectName(objectName);
        action->setShortcut(keys);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, std::forward<decltype(slot)>(slot));
        addAction(action);
        actions_ << action;
    };
    entry("Copy1", tr("&Copy"), QKeySequence::Copy, [this] { copySelection(false); });
    entry("CopyMerged1", tr("Copy &Merged"), Qt::CTRL | Qt::SHIFT | Qt::Key_C, [this] { copySelection(true); });
    entry("MarkAsData", tr("Mark as &data"), {}, [this] { markSelectionAsData(); });
    entry("FlipHorizontal1", tr("Flip &Horizontal"), {}, [this] { turnSelection(FlipHorizontal); });
    entry("FlipVertical1", tr("Flip &Vertical"), {}, [this] { turnSelection(FlipVertical); });
    entry("Rotate1", tr("&Rotate Clockwise"), {}, [this] { turnSelection(RotateClockwise); });
    entry("RotateAntiClockwise1", tr("Rotate &Anti-Clockwise"), {}, [this] { turnSelection(RotateAntiClockwise); });

    const auto changed = [this] {
        {
            const QSignalBlocker blocker(bar_);
            bar_->setValue(addressBox_->value());
        }
        // Other tiles are in view: those selected are no longer.
        if (!keepSelection_)
            selected_ = -1;
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
    paper_ = std::min(paper_, pensInMode() - 1);
    drawView();
    updateSwatches();
}

// The room a tile takes in the view, the line after it included.
QSize GraphicsDialog::cell() const
{
    const int unit = zoom();
    const int wide = mode() == 0 ? 2 * unit : mode() == 2 ? std::max(1, unit / 2) : unit;
    return QSize(pixelsWide() * wide + 1, tileHeight() * unit + 1);
}

// How many tiles the view has room for.
void GraphicsDialog::layoutTiles()
{
    const QSize tile = cell();
    columns_ = std::max(1, (view_->width() - 2) / tile.width());
    rows_ = std::max(1, (view_->height() - 2) / tile.height());
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
    // The view may have room for fewer tiles than were selected.
    if (selected_ >= tileCount())
        selected_ = -1;
    else if (selected_ >= 0)
        selectedCount_ = std::min(selectedCount_, tileCount() - selected_);
    QImage image = picture();
    if (selected_ >= 0) {
        // A dashed line round each tile selected.
        QPainter painter(&image);
        const QSize tile = cell();
        QPen dashes(Qt::white);
        dashes.setDashPattern({2, 2});
        for (int n = selected_; n < selected_ + selectedCount_; ++n) {
            const QRect box((n % columns_) * tile.width(), (n / columns_) * tile.height(), tile.width() - 2,
                            tile.height() - 2);
            painter.setPen(Qt::black);
            painter.drawRect(box);
            painter.setPen(dashes);
            painter.drawRect(box);
        }
    }
    view_->setPixmap(QPixmap::fromImage(image));
    updateActions();
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
    paint(paperSwatch_, colour(paper_));
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

void GraphicsDialog::setCurrentPaper(int pen)
{
    paper_ = std::clamp(pen, 0, pensInMode() - 1);
    updateSwatches();
}

// A pixel changes here; flush() gives the machine the bytes changed.
void GraphicsDialog::setPixel(int tile, int x, int y, int pen)
{
    const int perByte = pixelsPerByte(mode());
    const unsigned at = byteAddress(tile, x / perByte, y);
    memory_[at] = withPen(memory_[at], x % perByte, pen, mode(), encoding());
    written_.push_back(at);
}

void GraphicsDialog::flush()
{
    emulator_->withMachine([&](tuxape::Cpc& cpc) {
        for (const unsigned at : written_)
            cpc.memory().write(static_cast<uint16_t>(at), memory_[at]);
    });
    written_.clear();
}

void GraphicsDialog::paintPixel(int tile, int x, int y, bool paper)
{
    if (tile < 0 || tile >= tileCount() || x < 0 || x >= pixelsWide() || y < 0 || y >= tileHeight())
        return;
    setPixel(tile, x, y, paper ? paper_ : pen_);
    flush();
    drawView();
}

// The pixels of the tile that touch one another in the colour of the one
// clicked take the pen chosen.
void GraphicsDialog::fillFrom(int tile, int x, int y, bool paper)
{
    if (tile < 0 || tile >= tileCount() || x < 0 || x >= pixelsWide() || y < 0 || y >= tileHeight())
        return;
    const int old = pen(tile, x, y), with = paper ? paper_ : pen_;
    if (old == with)
        return;
    std::vector<QPoint> todo = {QPoint(x, y)};
    while (!todo.empty()) {
        const QPoint at = todo.back();
        todo.pop_back();
        if (at.x() < 0 || at.x() >= pixelsWide() || at.y() < 0 || at.y() >= tileHeight() || pen(tile, at.x(), at.y()) != old)
            continue;
        setPixel(tile, at.x(), at.y(), with);
        for (const QPoint step : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)})
            todo.push_back(at + step);
    }
    flush();
    drawView();
}

// ---- the tiles selected --------------------------------------------------------

void GraphicsDialog::select(int tile, int count)
{
    if (tile < 0 || tile >= tileCount() || count < 1) {
        clearSelection();
        return;
    }
    selected_ = tile;
    selectedCount_ = std::min(count, tileCount() - tile);
    drawView();
}

void GraphicsDialog::clearSelection()
{
    selected_ = -1;
    drawView();
}

// The arrow keys: the tile selected is the next one that way, and the view
// goes a row up or down when that is out of it. False at an end of memory.
bool GraphicsDialog::moveSelection(int columns, int rows)
{
    if (selected_ < 0) {
        select(0);
        return true;
    }
    int tile = selected_ + columns + rows * columns_;
    if (tile < 0 || tile >= tileCount()) {
        const int row = static_cast<int>((byteAddress(columns_, 0, 0) - address()) & 0xFFFF);
        const int moved = static_cast<int>(address()) + (tile < 0 ? -row : row);
        if (moved < 0 || moved > 0xFFFF)
            return false;
        tile += tile < 0 ? columns_ : -columns_;
        keepSelection_ = true;
        addressBox_->setValue(moved);
        keepSelection_ = false;
    }
    anchor_ = tile;
    select(tile);
    return true;
}

QImage GraphicsDialog::selectionPicture(bool merged) const
{
    if (selected_ < 0)
        return {};
    const int across = pixelsWide(), down = tileHeight(), gap = merged ? 0 : 1;
    // The rows and the columns of the view the tiles stand in.
    const int first = selected_, last = selected_ + selectedCount_ - 1;
    const int top = first / columns_, bottom = last / columns_;
    const int left = top == bottom ? first % columns_ : 0, right = top == bottom ? last % columns_ : columns_ - 1;
    QImage image((right - left + 1) * (across + gap) - gap, (bottom - top + 1) * (down + gap) - gap, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int tile = first; tile <= last; ++tile) {
        const int x0 = (tile % columns_ - left) * (across + gap), y0 = (tile / columns_ - top) * (down + gap);
        for (int y = 0; y < down; ++y)
            for (int x = 0; x < across; ++x)
                image.setPixel(x0 + x, y0 + y, colour(pen(tile, x, y)));
    }
    return image;
}

QString GraphicsDialog::selectionText() const
{
    const auto hex = [](unsigned value, int digits) { return QString("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper(); };
    QString text;
    for (int tile = selected_; selected_ >= 0 && tile < selected_ + selectedCount_; ++tile) {
        text += "; #" + hex(tileAddress(tile), 4) + '\n';
        for (int line = 0; line < tileHeight(); ++line) {
            QStringList bytes;
            for (int column = 0; column < tileWidth(); ++column)
                bytes << '#' + hex(memory_[byteAddress(tile, column, line)], 2);
            text += "db " + bytes.join(',') + '\n';
        }
    }
    return text;
}

void GraphicsDialog::copySelection(bool merged)
{
    if (selected_ < 0)
        return;
    auto* data = new QMimeData;
    data->setImageData(selectionPicture(merged));
    data->setText(selectionText());
    QGuiApplication::clipboard()->setMimeData(data);
}

void GraphicsDialog::markSelectionAsData()
{
    if (selected_ < 0)
        return;
    if (encoding() != Screen) {
        const unsigned start = tileAddress(selected_);
        const int size = selectedCount_ * tileWidth() * tileHeight();
        emit dataMarked(start, std::min(size, static_cast<int>(0x10000 - start)));
        return;
    }
    // As on the screen a tile's lines are apart from one another.
    for (int tile = selected_; tile < selected_ + selectedCount_; ++tile)
        for (int line = 0; line < tileHeight(); ++line)
            emit dataMarked(byteAddress(tile, 0, line), tileWidth());
}

bool GraphicsDialog::canRotate() const
{
    return pixelsWide() == tileHeight();
}

// Each tile selected is flipped or turned by itself, in the machine's
// memory.
void GraphicsDialog::turnSelection(Turn turn)
{
    const bool rotates = turn == RotateClockwise || turn == RotateAntiClockwise;
    if (selected_ < 0 || (rotates && !canRotate()))
        return;
    const int across = pixelsWide(), down = tileHeight();
    std::vector<int> old(static_cast<size_t>(across * down));
    for (int tile = selected_; tile < selected_ + selectedCount_; ++tile) {
        for (int y = 0; y < down; ++y)
            for (int x = 0; x < across; ++x)
                old[static_cast<size_t>(y * across + x)] = pen(tile, x, y);
        for (int y = 0; y < down; ++y) {
            for (int x = 0; x < across; ++x) {
                // Where the pixel that comes here was.
                const int from = turn == FlipHorizontal    ? y * across + (across - 1 - x)
                                 : turn == FlipVertical    ? (down - 1 - y) * across + x
                                 : turn == RotateClockwise ? (down - 1 - x) * across + y
                                                           : x * across + (across - 1 - y);
                if (old[static_cast<size_t>(from)] != old[static_cast<size_t>(y * across + x)])
                    setPixel(tile, x, y, old[static_cast<size_t>(from)]);
            }
        }
    }
    flush();
    drawView();
}

void GraphicsDialog::updateActions()
{
    for (QAction* action : actions_) {
        const bool rotates = action->objectName().startsWith("Rotate");
        action->setEnabled(selected_ >= 0 && (!rotates || canRotate()));
    }
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
        const bool press = event->type() == QEvent::MouseButtonPress;
        if (press)
            view_->setFocus();
        int tile = 0, x = 0, y = 0;
        if (locate(mouse->position().toPoint(), &tile, &x, &y)) {
            // The address of the byte under the pointer.
            const unsigned at = byteAddress(tile, x / pixelsPerByte(mode()), y);
            addressLabel_->setText(tr("Address: %1").arg(QString("%1").arg(at, 4, 16, QLatin1Char('0')).toUpper()));
            const bool left = press ? mouse->button() == Qt::LeftButton : (mouse->buttons() & Qt::LeftButton) != 0;
            const bool right = press ? mouse->button() == Qt::RightButton : (mouse->buttons() & Qt::RightButton) != 0;
            if (brushTool_->isChecked()) {
                // The left button draws with the pen, the right one with
                // the paper.
                if (left || right)
                    paintPixel(tile, x, y, !left);
            } else if (fillTool_->isChecked()) {
                if (press && (left || right))
                    fillFrom(tile, x, y, !left);
            } else if (left) {
                // A click selects a tile; Shift, or the button held, as far
                // as another.
                if (press && !(mouse->modifiers() & Qt::ShiftModifier))
                    anchor_ = tile;
                anchor_ = std::clamp(anchor_, 0, tileCount() - 1);
                select(std::min(anchor_, tile), std::abs(tile - anchor_) + 1);
            } else if (press && right && (tile < selected_ || tile >= selected_ + selectionCount())) {
                // The menu is for the tile under the pointer, unless it is
                // one of those selected.
                anchor_ = tile;
                select(tile);
            }
        }
    } else if (watched == view_ && event->type() == QEvent::ContextMenu) {
        if (selectTool_->isChecked() && selected_ >= 0) {
            QMenu menu(this);
            for (QAction* action : actions_) {
                if (action->objectName() == "FlipHorizontal1")
                    menu.addSeparator();
                menu.addAction(action);
            }
            menu.exec(static_cast<QContextMenuEvent*>(event)->globalPos());
        }
        return true;
    } else if (watched == view_ && event->type() == QEvent::KeyPress) {
        switch (static_cast<QKeyEvent*>(event)->key()) {
        case Qt::Key_Left: moveSelection(-1, 0); return true;
        case Qt::Key_Right: moveSelection(1, 0); return true;
        case Qt::Key_Up: moveSelection(0, -1); return true;
        case Qt::Key_Down: moveSelection(0, 1); return true;
        default: break;
        }
    } else if (event->type() == QEvent::MouseButtonPress && watched->parent() == swatches_) {
        const int pen = watched->objectName().mid(3).toInt();
        if (static_cast<QMouseEvent*>(event)->button() == Qt::RightButton)
            setCurrentPaper(pen);
        else
            setCurrentPen(pen);
    }
    return QDialog::eventFilter(watched, event);
}
