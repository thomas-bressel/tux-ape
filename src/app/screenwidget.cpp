#include "screenwidget.h"

#include "contrast.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPen>
#include <QFontMetrics>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "core/keymap.h"
#include "crtview.h"
#include "emulator.h"
#include "hostkeys.h"

namespace {

// A CPC pixel line is shown as two screen lines.

// A few words in small, light on a dark box, legible on any picture.
void paintTag(QPainter& painter, const QRect& box, const QString& text)
{
    painter.fillRect(box, QColor(0, 0, 0, 200));
    painter.setPen(inkOn(Qt::black));
    painter.drawText(box, Qt::AlignCenter, text);
}

QFont tagFont(const QPainter& painter, const QRect& picture)
{
    QFont font = painter.font();
    font.setBold(false);
    font.setPixelSize(std::max(9, picture.height() / 54));
    return font;
}

}  // namespace

ScreenWidget::ScreenWidget(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    image_ = emulator_->frame();
    connect(emulator_, &Emulator::frameReady, this, &ScreenWidget::fetchFrame, Qt::QueuedConnection);
    connect(emulator_, &Emulator::driveLightChanged, this, [this] { showDriveLight(emulator_->driveLight()); },
            Qt::QueuedConnection);
    // The window may go to another screen, or the screen to another rate.
    auto* watch = new QTimer(this);
    connect(watch, &QTimer::timeout, this, &ScreenWidget::updateDisplayClock);
    watch->start(2000);
}

QSize ScreenWidget::sizeHint() const
{
    return halfSize_ ? QSize(displayWidth() / 2, displayHeight() / 2) : QSize(displayWidth(), displayHeight());
}

void ScreenWidget::setHalfSize(bool half)
{
    if (half == halfSize_)
        return;
    halfSize_ = half;
    updateGeometry();
    update();
}

void ScreenWidget::setRenderBothLines(bool both)
{
    if (both == renderBoth_)
        return;
    renderBoth_ = both;
    striped_ = QImage();
    update();
}

QSize ScreenWidget::minimumSizeHint() const
{
    return {displayWidth() / 2, displayHeight() / 2};
}

QImage ScreenWidget::screenshot() const
{
    return image_.scaled(displayWidth(), displayHeight());
}

QImage ScreenWidget::shownPicture(int widest) const
{
    QImage picture;
    if (crt_ && crt_->isValid() && !crt_->failed())
        picture = crt_->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    if (picture.isNull())
        picture = screenshot();
    if (picture.width() > widest)
        picture = picture.scaledToWidth(widest, Qt::SmoothTransformation);
    return picture;
}

void ScreenWidget::setAmxMouse(bool on)
{
    amxMouse_ = on;
    amxSeen_ = false;
    setMouseTracking(on);
}

// The pointer's way since it was last looked at, as steps of the mouse: a
// step for each point of the picture, which is two of the CPC's smallest.
void ScreenWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!amxMouse_)
        return QWidget::mouseMoveEvent(event);
    const QPoint now = event->position().toPoint();
    if (amxSeen_)
        emulator_->amxMouseMoved(now.x() - amxLast_.x(), now.y() - amxLast_.y());
    amxLast_ = now;
    amxSeen_ = true;
}

void ScreenWidget::amxButtons(QMouseEvent* event)
{
    const Qt::MouseButtons down = event->buttons();
    emulator_->amxMouseButtons((down & Qt::LeftButton ? 1u : 0u) | (down & Qt::RightButton ? 2u : 0u)
                               | (down & Qt::MiddleButton ? 4u : 0u));
}

void ScreenWidget::mousePressEvent(QMouseEvent* event)
{
    if (!amxMouse_)
        return QWidget::mousePressEvent(event);
    amxButtons(event);
}

void ScreenWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!amxMouse_)
        return QWidget::mouseReleaseEvent(event);
    amxButtons(event);
}

void ScreenWidget::setPalEmulation(bool pal)
{
    if (pal == pal_)
        return;
    pal_ = pal;
    striped_ = QImage();
    update();
}

void ScreenWidget::fetchFrame()
{
    const QSize before = image_.size();
    image_ = emulator_->frame();
    // The whole raster, or the part WinAPE shows: another size.
    if (image_.size() != before) {
        striped_ = QImage();
        updateGeometry();
        if (crt_)
            crt_->setGeometry(pictureRect());
        emit pictureSizeChanged();
    }
    if (interruptsShown_)
        interrupts_ = emulator_->interruptMarks();
    if (interruptsShown_)
        rows_ = emulator_->rowMarks();
    if (syncsShown_)
        syncs_ = emulator_->syncMarks();
    if (crt_)
        crt_->setFrame(image_);
    else
        update();
}

void ScreenWidget::setCrtShader(bool on, bool colourTube, const CrtLook& look)
{
    if (on && !crt_ && CrtView::supported() && CrtRenderer::available()) {
        crt_ = new CrtView(this);
        // The keyboard and the mouse stay this widget's.
        crt_->setAttribute(Qt::WA_TransparentForMouseEvents);
        crt_->setFocusPolicy(Qt::NoFocus);
        crt_->setGeometry(pictureRect());
        crt_->overlay = [this](QPainter& painter) {
            paintDriveLight(painter, driveLightRect().translated(-crt_->pos()));
            paintInterruptMarks(painter, crt_->rect());
            paintSyncMarks(painter, crt_->rect());
            paintNote(painter, crt_->rect());
            paintKeys(painter, crt_->rect());
            paintBeamMarker(painter, crt_->rect());
        };
        // A graphics card that will not have the shaders: plain drawing.
        connect(crt_, &CrtView::unusable, this, [this] { setCrtShader(false); }, Qt::QueuedConnection);
        // Each picture the screen has shown: the machine's cue for its
        // next frame, when in step with the screen.
        connect(crt_, &QOpenGLWidget::frameSwapped, this, [this] {
            if (swapsPerFrame_ <= 0 || !crt_)
                return;
            // Every picture the screen shows counts, whether the machine's
            // has changed or not: the view is drawn again each time, and
            // the machine told once in so many.
            if (++swaps_ >= swapsPerFrame_) {
                swaps_ = 0;
                emulator_->displayTick();
            }
            crt_->update();
        });
        crt_->setFrame(image_);
        crt_->show();
    } else if (!on && crt_) {
        crt_->deleteLater();
        crt_ = nullptr;
    }
    updateDisplayClock();
    if (crt_) {
        crt_->setMask(colourTube);
        crt_->setLook(look);
    }
    update();
}

void ScreenWidget::setDisplaySync(bool wanted)
{
    syncWanted_ = wanted;
    updateDisplayClock();
}

// A screen that shows fifty pictures a second, or a whole number of times
// that, within a hundredth: one frame of the CPC to that many pictures.
int ScreenWidget::syncEveryFor(double hertz)
{
    const int times = static_cast<int>(std::lround(hertz / 50.0));
    return times >= 1 && times <= 4 && std::abs(hertz / times - 50.0) < 0.6 ? times : 0;
}

void ScreenWidget::updateDisplayClock()
{
    int every = 0;
    const QWindow* handle = window() ? window()->windowHandle() : nullptr;
    if (syncWanted_ && crt_ && !crt_->failed() && isVisible() && handle && handle->screen())
        every = syncEveryFor(handle->screen()->refreshRate());
    if (every == swapsPerFrame_ && emulator_->displayClock() == (every > 0))
        return;
    swapsPerFrame_ = every;
    swaps_ = 0;
    emulator_->setDisplayClock(every > 0);
    if (every > 0)
        crt_->update();  // the first of the pictures that keep one another coming
}

// "Row Highlight": where the beam is, as a line across the picture and one
// down it.
void ScreenWidget::setBeamMarker(int line, int column)
{
    if (line == beamLine_ && column == beamColumn_)
        return;
    beamLine_ = line;
    beamColumn_ = column;
    update();
    if (crt_)
        crt_->update();
}

void ScreenWidget::showNote(const QString& text)
{
    note_ = text;
    const int number = ++noteNumber_;
    QTimer::singleShot(2500, this, [this, number] {
        if (number != noteNumber_)
            return;
        note_.clear();
        update();
        if (crt_)
            crt_->update();
    });
    update();
    if (crt_)
        crt_->update();
}

// The note: light text in a dark box at the picture's top left, legible on
// any picture.
void ScreenWidget::paintNote(QPainter& painter, const QRect& picture) const
{
    if (note_.isEmpty())
        return;
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(std::max(12, picture.height() / 28));
    painter.setFont(font);
    const QRect box = noteRect(picture);
    const QColor back(0, 0, 0, 200);
    painter.fillRect(box, back);
    painter.setPen(inkOn(Qt::black));
    painter.drawText(box, Qt::AlignCenter, note_);
}

// Where the note is drawn; empty when there is none.
QRect ScreenWidget::noteRect(const QRect& picture) const
{
    if (note_.isEmpty())
        return {};
    QFont font = this->font();
    font.setBold(true);
    font.setPixelSize(std::max(12, picture.height() / 28));
    const QFontMetrics metrics(font);
    const int margin = metrics.height() / 2;
    return QRect(picture.left() + margin, picture.top() + margin, metrics.horizontalAdvance(note_) + 2 * margin,
                 metrics.height() + margin);
}

void ScreenWidget::setKeysShown(bool shown, int corner, bool large)
{
    keysCorner_ = std::clamp(corner, 0, 3);
    keysLarge_ = large;
    if (shown && !keysTimer_) {
        emulator_->takeHostKeys();  // what was pressed before is not shown
        keysTimer_ = new QTimer(this);
        connect(keysTimer_, &QTimer::timeout, this, &ScreenWidget::watchKeys);
        keysTimer_->start(50);
    } else if (!shown && keysTimer_) {
        delete keysTimer_;
        keysTimer_ = nullptr;
        keys_.clear();
    }
    repaintOverlay();
}

QStringList ScreenWidget::keysOnScreen() const
{
    QStringList keys;
    for (const ShownKey& key : keys_)
        keys << (!key.word.isEmpty() ? key.word : key.bottom.isEmpty() ? key.top : key.top + '\n' + key.bottom);
    return keys;
}

// Twenty times a second while the keys are shown: those pressed since are
// added, and all go a while after the last one held is let go.
void ScreenWidget::watchKeys()
{
    const Emulator::HostKeys keys = emulator_->takeHostKeys();
    bool changed = false;
    // What is printed on them is read once for each burst of keys: the
    // machine may have been changed in between.
    if (keys.pressed.any() && keys_.isEmpty())
        keyTables_ = emulator_->keyTables();
    const auto add = [&](int key) {
        if (!keys.pressed[static_cast<size_t>(key)]
            || std::any_of(keys_.begin(), keys_.end(), [key](const ShownKey& shown) { return shown.key == key; }))
            return;
        if (keys_.size() == kMostKeys)
            keys_.removeFirst();
        const tuxape::KeyLegend legend =
            tuxape::keyLegend(keyTables_, static_cast<uint8_t>(key), tuxape::keyboardModel(emulator_->model()));
        ShownKey shown{key, keys.down[static_cast<size_t>(key)], QString::fromUtf8(legend.word), {}, {}};
        if (legend.top)
            shown.top = QString::fromUcs4(&legend.top, 1);
        if (legend.topAlso)
            shown.top += QString::fromUcs4(&legend.topAlso, 1);
        if (legend.bottom)
            shown.bottom = QString::fromUcs4(&legend.bottom, 1);
        keys_.append(shown);
        changed = true;
    };
    // SHIFT and CONTROL first of the keys pressed together, as they go in
    // a combination.
    add(static_cast<int>(tuxape::CpcKey::Shift));
    add(static_cast<int>(tuxape::CpcKey::Control));
    for (int key = 0; key < tuxape::kCpcKeyCount; ++key)
        add(key);
    for (ShownKey& shown : keys_) {
        const bool down = keys.down[static_cast<size_t>(shown.key)];
        changed = changed || down != shown.down;
        shown.down = down;
    }
    // A key that came and went since the last look is the keyboard at
    // work as much as one held.
    if (keys.down.any()) {
        keysLetGo_.invalidate();
    } else if (keys.pressed.any() || (!keys_.isEmpty() && !keysLetGo_.isValid())) {
        keysLetGo_.start();
    } else if (!keys_.isEmpty() && keysLetGo_.elapsed() >= kKeysStay) {
        keys_.clear();
        keysLetGo_.invalidate();
        changed = true;
    }
    if (changed)
        repaintOverlay();
}

void ScreenWidget::repaintOverlay()
{
    update();
    if (crt_)
        crt_->update();
}

// The keys as keycaps in a row, from the corner chosen: white on a dark
// cap that lets a little of the picture through, the keys still held with
// a thicker, white edge.
void ScreenWidget::paintKeys(QPainter& painter, const QRect& picture) const
{
    if (keys_.isEmpty())
        return;
    const int high = std::max(16, static_cast<int>(std::lround(picture.height() * (keysLarge_ ? 0.075 : 0.05))));
    const int gap = std::max(2, high / 6), margin = std::max(4, high / 3);
    // A letter, two characters one above the other, a word.
    QFont letterFont = painter.font();
    letterFont.setBold(true);
    letterFont.setPixelSize(std::max(8, high / 2));
    QFont pairFont = letterFont;
    pairFont.setPixelSize(std::max(7, high * 2 / 5));
    QFont wordFont = letterFont;
    wordFont.setPixelSize(std::max(7, high * 9 / 25));
    const QFontMetrics pair(pairFont), words(wordFont);
    QList<int> widths;
    for (const ShownKey& key : keys_) {
        const int text = !key.word.isEmpty() ? words.horizontalAdvance(key.word)
                                             : std::max(pair.horizontalAdvance(key.top), pair.horizontalAdvance(key.bottom));
        widths << std::max(high, text + high / 2);
    }
    // In rows no wider than the picture, the oldest keys in the first,
    // which is the one at the edge of the picture.
    const int room = picture.width() - 2 * margin;
    QList<QList<int>> rows{{}};
    int used = 0;
    for (int i = 0; i < widths.size(); ++i) {
        if (!rows.last().isEmpty() && used + gap + widths[i] > room) {
            rows.append(QList<int>());
            used = 0;
        }
        used += (rows.last().isEmpty() ? 0 : gap) + widths[i];
        rows.last() << i;
    }
    const bool right = keysCorner_ == 1 || keysCorner_ == 3, bottom = keysCorner_ >= 2;
    // Clear of the note and of the drive's light, which have the top corners.
    int top = picture.top() + margin;
    const QRect taken = keysCorner_ == 0 ? noteRect(picture)
                        : keysCorner_ == 1 && crt_ ? driveLightRect().translated(-crt_->pos())
                        : keysCorner_ == 1         ? driveLightRect()
                                                   : QRect();
    if (!taken.isEmpty())
        top = std::max(top, taken.bottom() + 1 + gap);
    const QColor cap(0, 0, 0, 204);  // the darkest any picture makes it is a grey of 51: white reads on it
    const QColor ink = inkOn(Qt::black);
    const QColor edgeUp = readable(QColor(128, 128, 128), QColor(51, 51, 51), kGraphicContrast);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    for (int r = 0; r < rows.size(); ++r) {
        // The first row at the edge: at the bottom, the others above it.
        const int y = bottom ? picture.bottom() + 1 - margin - high - r * (high + gap) : top + r * (high + gap);
        int rowWidth = -gap;
        for (const int i : rows[r])
            rowWidth += gap + widths[i];
        int x = right ? picture.right() + 1 - margin - rowWidth : picture.left() + margin;
        for (const int i : rows[r]) {
            const ShownKey& key = keys_[i];
            const QRectF box(x, y, widths[i], high);
            const qreal edge = key.down ? std::max(2, high / 12) : 1;
            painter.setPen(QPen(key.down ? ink : edgeUp, edge));
            painter.setBrush(cap);
            painter.drawRoundedRect(box.adjusted(edge / 2, edge / 2, -edge / 2, -edge / 2), high / 6.0, high / 6.0);
            painter.setPen(ink);
            if (!key.word.isEmpty()) {
                painter.setFont(wordFont);
                painter.drawText(box, Qt::AlignCenter, key.word);
            } else if (!key.bottom.isEmpty()) {
                painter.setFont(pairFont);
                const QRectF upper(box.left(), box.top() + high * 0.08, box.width(), high * 0.42);
                painter.drawText(upper, Qt::AlignCenter, key.top);
                painter.drawText(upper.translated(0, high * 0.42), Qt::AlignCenter, key.bottom);
            } else {
                painter.setFont(letterFont);
                painter.drawText(box, Qt::AlignCenter, key.top);
            }
            x += widths[i] + gap;
        }
    }
    painter.restore();
}

void ScreenWidget::setInterruptMarksShown(bool on)
{
    if (on == interruptsShown_)
        return;
    interruptsShown_ = on;
    interrupts_ = on ? emulator_->interruptMarks() : QList<tuxape::Cpc::InterruptMark>();
    emulator_->setRowMarksWanted(on);
    rows_ = {};
    update();
    if (crt_)
        crt_->update();
}

void ScreenWidget::setSyncMarksShown(bool on)
{
    if (on == syncsShown_)
        return;
    syncsShown_ = on;
    emulator_->setSyncMarksWanted(on);
    syncs_ = {};
    update();
    if (crt_)
        crt_->update();
}

// What "Row Highlight" shows: at the left a tick for each line where a colour
// was written, then the interrupts asked for (dotted) and those the Z80 took
// (dashed, a square where the beam was), and over it all their numbers, and
// how many there are of each.
void ScreenWidget::paintInterruptMarks(QPainter& painter, const QRect& picture) const
{
    if (!interruptsShown_)
        return;
    using tuxape::Cpc;
    const QColor edge(0, 0, 0, 200);
    const QColor taken = readable(QColor(255, 110, 255), Qt::black, kGraphicContrast);
    const QColor asked = readable(QColor(255, 190, 60), Qt::black, kGraphicContrast);
    const QColor colour = inkOn(Qt::black);
    const int lines = displayHeight() / 2;
    const auto inSight = [&](int line) { return line >= 0 && line < lines; };
    const auto yOf = [&](int line) { return picture.top() + (line * 2 + 1) * picture.height() / displayHeight(); };
    const QFont font = tagFont(painter, picture);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int high = metrics.height();

    // The colours written, each line's tick and, where there is room, its
    // number.
    const int tick = std::max(4, picture.width() / 160);
    const int rowHeight = std::max(1, 2 * picture.height() / displayHeight());
    int lastBottom = picture.top() - 1;
    for (const Cpc::ColourMark& mark : rows_.colours) {
        if (!inSight(mark.line))
            continue;
        const int top = picture.top() + mark.line * 2 * picture.height() / displayHeight();
        painter.fillRect(QRect(picture.left(), top, tick + 1, rowHeight), edge);
        painter.fillRect(QRect(picture.left(), top, tick, std::max(1, rowHeight - 1)), colour);
        const int labelTop = std::clamp(top - high / 2, picture.top(), picture.bottom() + 1 - high);
        if (labelTop > lastBottom) {
            const QString text = QString::number(mark.crtcLine);
            paintTag(painter, QRect(picture.left() + tick + 2, labelTop, metrics.horizontalAdvance(text) + 4, high), text);
            lastBottom = labelTop + high - 1;
        }
    }

    // The lines of the interrupts.
    const auto across = [&](int y, const QColor& ink, Qt::PenStyle style) {
        painter.setPen(QPen(edge, 3));
        painter.drawLine(picture.left(), y, picture.right(), y);
        painter.setPen(QPen(ink, 1, style));
        painter.drawLine(picture.left(), y, picture.right(), y);
    };
    for (const Cpc::RaisedMark& mark : rows_.raised)
        if (inSight(mark.line))
            across(yOf(mark.line), asked, Qt::DotLine);
    for (const Cpc::InterruptMark& mark : interrupts_) {
        if (!inSight(mark.line))
            continue;
        const int y = yOf(mark.line);
        across(y, taken, Qt::DashLine);
        const int column = picture.left() + std::clamp<int>(mark.column, 0, displayWidth() - 1) * picture.width() / displayWidth();
        const QRect square(column - 3, y - 3, 7, 7);
        painter.fillRect(square, taken);
        painter.setPen(QPen(edge, 1));
        painter.drawRect(square);
    }

    // Their numbers, from the top down, one beside the other where they
    // would fall on one another: what asked for it and whether it was
    // taken, and the number alone of one taken with nothing known of it.
    struct Tag {
        int line;
        QString text;
    };
    QList<Tag> tags;
    for (const Cpc::RaisedMark& mark : rows_.raised) {
        const QString name = mark.source == Cpc::InterruptSource::Counter  ? QStringLiteral("R52")
                             : mark.source == Cpc::InterruptSource::Raster ? QStringLiteral("PRI")
                                                                           : QStringLiteral("DMA%1").arg(static_cast<int>(mark.source) - static_cast<int>(Cpc::InterruptSource::Channel0));
        const QString text = QString::number(mark.crtcLine) + ' ' + name;
        tags.append({mark.line, mark.taken ? text : tr("%1, not taken").arg(text)});
    }
    for (const Cpc::InterruptMark& mark : interrupts_) {
        const bool known = std::any_of(rows_.raised.begin(), rows_.raised.end(),
                                       [&](const Cpc::RaisedMark& raised) { return std::abs(raised.line - mark.line) <= 1; });
        if (!known)
            tags.append({mark.line, QString::number(mark.crtcLine)});
    }
    std::stable_sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.line < b.line; });
    lastBottom = picture.top() - 1;
    int x = picture.left();
    for (const Tag& tag : tags) {
        const bool above = tag.line < 0, below = tag.line >= lines;
        // One out of sight, at the edge it is beyond, with an arrow saying so.
        const QString text = (above ? QStringLiteral("\u2191") : below ? QStringLiteral("\u2193") : QString()) + tag.text;
        const int wide = metrics.horizontalAdvance(text) + 4;
        const int top = above   ? picture.top()
                        : below ? picture.bottom() + 1 - high
                                : std::clamp(yOf(tag.line) - high / 2, picture.top(), picture.bottom() + 1 - high);
        x = top <= lastBottom ? x : picture.left();
        paintTag(painter, QRect(x, top, wide, high), text);
        x += wide + 2;
        lastBottom = std::max(lastBottom, top + high - 1);
    }

    // How many of each, which says at a glance what a picture is made of.
    const qsizetype fromAsic = std::count_if(rows_.raised.begin(), rows_.raised.end(), [](const Cpc::RaisedMark& mark) {
        return mark.source != Cpc::InterruptSource::Counter;
    });
    const tuxape::CpcModel model = emulator_->model();
    const bool plus = model == tuxape::CpcModel::Plus464 || model == tuxape::CpcModel::Plus6128;
    const QString counts = plus ? tr("Z80 interrupts: %1 - ASIC interrupts: %2 - colour lines: %3").arg(interrupts_.size()).arg(fromAsic).arg(rows_.colours.size())
                                : tr("Z80 interrupts: %1 - colour lines: %2").arg(interrupts_.size()).arg(rows_.colours.size());
    const int wide = metrics.horizontalAdvance(counts) + 4;
    paintTag(painter, QRect(picture.left() + std::max(0, (picture.width() - wide) / 2), picture.top(), wide, high), counts);
}

// The bands of the sync pulses: the HSYNC of each line where it was, so that
// one moved by a program shows, and the VSYNC across the picture. What falls
// out of the picture's sight leaves a tick along the edge it is beyond.
void ScreenWidget::paintSyncMarks(QPainter& painter, const QRect& picture) const
{
    if (!syncsShown_)
        return;
    QColor hbl = readable(QColor(80, 200, 255), Qt::black, kGraphicContrast);
    QColor vbl = readable(QColor(120, 255, 120), Qt::black, kGraphicContrast);
    const QColor hblEdge = hbl, vblEdge = vbl;
    hbl.setAlpha(110);
    vbl.setAlpha(110);
    const int lines = displayHeight() / 2;
    const auto yOf = [&](int line) { return picture.top() + line * 2 * picture.height() / displayHeight(); };
    const auto xOf = [&](int column) { return picture.left() + column * picture.width() / displayWidth(); };
    const int tick = std::max(2, picture.width() / 256);
    int firstHcc = 256, lastHcc = -1, widest = 0, hblColumn = 0;
    for (const tuxape::Cpc::HsyncMark& pulse : syncs_.hsyncs) {
        firstHcc = std::min<int>(firstHcc, pulse.hcc);
        lastHcc = std::max<int>(lastHcc, pulse.hcc);
        widest = std::max<int>(widest, pulse.width);
        hblColumn = pulse.column;
        if (pulse.line < 0 || pulse.line >= lines)
            continue;
        const int top = yOf(pulse.line), bottom = yOf(pulse.line + 1);
        const int end = pulse.column + pulse.width * tuxape::Monitor::kCellWidth;
        if (pulse.column >= displayWidth())
            painter.fillRect(QRect(picture.right() + 1 - tick, top, tick, bottom - top), hblEdge);
        else if (end <= 0)
            painter.fillRect(QRect(picture.left(), top, tick, bottom - top), hblEdge);
        else
            painter.fillRect(QRect(QPoint(xOf(std::max<int>(0, pulse.column)), top), QPoint(xOf(std::min(displayWidth(), end)) - 1, bottom - 1)), hbl);
    }
    int vblLine = 0;
    for (const tuxape::Cpc::VsyncMark& pulse : syncs_.vsyncs) {
        vblLine = pulse.line;
        const int first = std::max<int>(pulse.line, 0), last = std::min<int>(pulse.line + std::max<int>(pulse.lines, 1), lines);
        if (first < last)
            painter.fillRect(QRect(QPoint(picture.left(), yOf(first)), QPoint(picture.right(), yOf(last) - 1)), vbl);
        else
            painter.fillRect(pulse.line < 0 ? QRect(picture.left(), picture.top(), picture.width(), tick)
                                            : QRect(picture.left(), picture.bottom() + 1 - tick, picture.width(), tick),
                             vblEdge);
    }
    // What they are in the CRTC's terms: its character, its line.
    const QFont font = tagFont(painter, picture);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int high = metrics.height();
    if (lastHcc >= 0) {
        const QString text = (firstHcc == lastHcc ? tr("HBL C%1").arg(firstHcc) : tr("HBL C%1-%2").arg(firstHcc).arg(lastHcc)) + QStringLiteral(" +%1").arg(widest);
        const int wide = metrics.horizontalAdvance(text) + 4;
        const int x = std::clamp(xOf(hblColumn), picture.left(), std::max(picture.left(), picture.right() + 1 - wide));
        paintTag(painter, QRect(x, picture.top(), wide, high), text);
    }
    if (!syncs_.vsyncs.empty()) {
        const tuxape::Cpc::VsyncMark& pulse = syncs_.vsyncs.back();
        const QString text = tr("VBL L%1 +%2").arg(pulse.crtcLine).arg(pulse.lines);
        const int wide = metrics.horizontalAdvance(text) + 4;
        const int y = std::clamp(yOf(vblLine), picture.top() + high, std::max(picture.top(), picture.bottom() + 1 - high));
        paintTag(painter, QRect(picture.right() + 1 - wide, y, wide, high), text);
    }
}

void ScreenWidget::paintBeamMarker(QPainter& painter, const QRect& picture) const
{
    if (beamLine_ < 0 && beamColumn_ < 0)
        return;
    painter.setPen(QColor(255, 255, 0, 200));
    if (beamLine_ >= 0 && beamLine_ < displayHeight() / 2) {
        const int y = picture.top() + (beamLine_ * 2 + 1) * picture.height() / displayHeight();
        painter.drawLine(picture.left(), y, picture.right(), y);
    }
    if (beamColumn_ >= 0 && beamColumn_ < displayWidth()) {
        const int x = picture.left() + beamColumn_ * picture.width() / displayWidth();
        painter.drawLine(x, picture.top(), x, picture.bottom());
    }
}

void ScreenWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (crt_)
        crt_->setGeometry(pictureRect());
}

// Largest picture of the right proportions that fits, centred.
QRect ScreenWidget::pictureRect() const
{
    QSize size(displayWidth(), displayHeight());
    size.scale(this->size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

void ScreenWidget::setDriveLight(bool shown, bool withCylinder)
{
    driveLightShown_ = shown;
    driveCylinderShown_ = withCylinder;
    update();
    if (crt_)
        crt_->update();
}

void ScreenWidget::showDriveLight(int light)
{
    if (light == driveLight_)
        return;
    driveLight_ = light;
    if (driveLightShown_)
        update();
    if (driveLightShown_ && crt_)
        crt_->update();
}

QRect ScreenWidget::driveLightRect() const
{
    if (!driveLightShown_ || driveLight_ < 0)
        return {};
    const QRect picture = pictureRect();
    const int unit = std::max(1, picture.width() / 96);  // 8 at full size
    const int wide = (driveCylinderShown_ ? 5 : 2) * unit + unit / 2, high = unit + unit / 2;
    return QRect(picture.right() - wide - unit, picture.top() + unit, wide, high);
}

void ScreenWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRect target = pictureRect();
    if (target != rect())
        painter.fillRect(rect(), Qt::black);
    // The graphics card draws the picture, and the light on it.
    if (crt_)
        return;
    // At half size a CPC line is one screen line: there is no second line
    // to leave out.
    if (pal_ && !halfSize_ && !image_.isNull()) {
        // As a television shows it: along a line each pixel takes a
        // quarter of each neighbour, and the line under is dimmer.
        if (striped_.isNull())
            striped_ = QImage(image_.width(), image_.height() * 2, QImage::Format_RGB32);
        const int width = image_.width();
        for (int y = 0; y < image_.height(); ++y) {
            const auto* in = reinterpret_cast<const quint32*>(image_.constScanLine(y));
            auto* full = reinterpret_cast<quint32*>(striped_.scanLine(y * 2));
            auto* dim = reinterpret_cast<quint32*>(striped_.scanLine(y * 2 + 1));
            for (int x = 0; x < width; ++x) {
                const quint32 left = in[x ? x - 1 : 0], right = in[x + 1 < width ? x + 1 : x];
                const quint32 mixed = ((in[x] & 0xFEFEFE) >> 1) + ((left & 0xFCFCFC) >> 2) + ((right & 0xFCFCFC) >> 2);
                full[x] = mixed | 0xFF000000;
                dim[x] = ((mixed & 0xFEFEFE) >> 1) | 0xFF000000;
            }
        }
        painter.drawImage(target, striped_);
    } else if (renderBoth_ || halfSize_ || image_.isNull()) {
        painter.drawImage(target, image_);
    } else {
        if (striped_.isNull()) {
            striped_ = QImage(image_.width(), image_.height() * 2, QImage::Format_RGB32);
            striped_.fill(Qt::black);
        }
        const qsizetype rowBytes = static_cast<qsizetype>(image_.width()) * 4;
        for (int y = 0; y < image_.height(); ++y)
            std::memcpy(striped_.scanLine(y * 2), image_.constScanLine(y), static_cast<size_t>(rowBytes));
        painter.drawImage(target, striped_);
    }
    paintDriveLight(painter, driveLightRect());
    paintInterruptMarks(painter, target);
    paintSyncMarks(painter, target);
    paintBeamMarker(painter, target);
    paintNote(painter, target);
    paintKeys(painter, target);
}

// The drive's light: red, with the drive's letter and its cylinder.
void ScreenWidget::paintDriveLight(QPainter& painter, const QRect& light) const
{
    if (light.isEmpty())
        return;
    // A red deep enough for the white letter to read on.
    painter.setPen(QColor(64, 0, 0));
    painter.setBrush(readable(QColor(255, 32, 32), Qt::white));
    painter.drawRect(light.adjusted(0, 0, -1, -1));
    QFont font = painter.font();
    font.setPixelSize(std::max(6, light.height() - 2));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QChar letter('A' + (driveLight_ >> 8));
    painter.drawText(light, Qt::AlignCenter,
                     driveCylinderShown_ ? QString("%1:%2").arg(letter).arg(driveLight_ & 0xFF, 2, 10, QLatin1Char('0')) : QString(letter));
}

// Whether a key pressed, with what is held down with it, is the CPC's and
// not the interface's. The function keys are the interface's whatever the
// layout: WinAPE's keys are all of that kind, which is why a program's own
// CONTROL+L never opened a window there. Any other key the layout gives to
// the CPC is the CPC's, unless it comes with a key that is not: Alt and a
// letter is a menu, as long as no layout has put COPY on Alt.
bool ScreenWidget::isCpcKey(const QKeyEvent* event) const
{
    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35)
        return false;
    const tuxape::KeyMap map = emulator_->keyMap();
    const auto given = [&](uint8_t pcKey) { return pcKey != 0 && !map.cpcKeys(pcKey, numLock_).empty(); };
    const Qt::KeyboardModifiers held = event->modifiers();
    if (held & Qt::MetaModifier)
        return false;
    if ((held & Qt::AltModifier) && !given(tuxape::PcLeftAlt))
        return false;
    if ((held & Qt::ControlModifier) && !given(tuxape::PcLeftControl) && !given(tuxape::PcRightControl))
        return false;
    return given(pcKeyFromEvent(event));
}

bool ScreenWidget::event(QEvent* event)
{
    // A shortcut made of the CPC's keys does not take them from it while
    // the screen has the keyboard: the key press comes here instead.
    if (event->type() == QEvent::ShortcutOverride && isCpcKey(static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    // Tab belongs to the CPC; Qt would otherwise use it to move the focus
    // and never deliver it as a key press.
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            keyPressEvent(keyEvent);
            return true;
        }
    }
    return QWidget::event(event);
}

void ScreenWidget::keyPressEvent(QKeyEvent* event)
{
    handleKey(event, true);
}

void ScreenWidget::keyReleaseEvent(QKeyEvent* event)
{
    handleKey(event, false);
}

void ScreenWidget::handleKey(QKeyEvent* event, bool pressed)
{
    if (event->isAutoRepeat()) {
        event->accept();  // the CPC does its own key repeat
        return;
    }
    if (const int state = numLockFromEvent(event); state >= 0)
        numLock_ = state;
    const uint8_t pcKey = pcKeyFromEvent(event);
    if (pcKey == 0) {
        event->ignore();
        return;
    }
    emulator_->pcKeyEvent(pcKey, numLock_, pressed);
    // A key the layout gives to no key of the CPC is the interface's: Alt,
    // for the menus, unless a layout has COPY on it, as WinAPE's does.
    event->setAccepted(!emulator_->keyMap().cpcKeys(pcKey, numLock_).empty());
}

void ScreenWidget::focusOutEvent(QFocusEvent* event)
{
    // Key releases will not reach us any more; do not leave keys stuck.
    emulator_->releaseAllKeys();
    QWidget::focusOutEvent(event);
}
