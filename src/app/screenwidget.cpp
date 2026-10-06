#include "screenwidget.h"

#include <cstring>

#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>

#include "crtview.h"
#include "emulator.h"
#include "hostkeys.h"

namespace {

// A CPC pixel line is shown as two screen lines.
constexpr int kDisplayWidth = tuxape::Monitor::kWidth;
constexpr int kDisplayHeight = tuxape::Monitor::kHeight * 2;

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
}

QSize ScreenWidget::sizeHint() const
{
    return halfSize_ ? QSize(kDisplayWidth / 2, kDisplayHeight / 2) : QSize(kDisplayWidth, kDisplayHeight);
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
    return {kDisplayWidth / 2, kDisplayHeight / 2};
}

QImage ScreenWidget::screenshot() const
{
    return image_.scaled(kDisplayWidth, kDisplayHeight);
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
    image_ = emulator_->frame();
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
        crt_->overlay = [this](QPainter& painter) { paintDriveLight(painter, driveLightRect().translated(-crt_->pos())); };
        // A graphics card that will not have the shaders: plain drawing.
        connect(crt_, &CrtView::unusable, this, [this] { setCrtShader(false); }, Qt::QueuedConnection);
        crt_->setFrame(image_);
        crt_->show();
    } else if (!on && crt_) {
        crt_->deleteLater();
        crt_ = nullptr;
    }
    if (crt_) {
        crt_->setMask(colourTube);
        crt_->setLook(look);
    }
    update();
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
    QSize size(kDisplayWidth, kDisplayHeight);
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
}

// The drive's light: red, with the drive's letter and its cylinder.
void ScreenWidget::paintDriveLight(QPainter& painter, const QRect& light) const
{
    if (light.isEmpty())
        return;
    painter.setPen(QColor(64, 0, 0));
    painter.setBrush(QColor(255, 32, 32));
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

bool ScreenWidget::event(QEvent* event)
{
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
    event->accept();
}

void ScreenWidget::focusOutEvent(QFocusEvent* event)
{
    // Key releases will not reach us any more; do not leave keys stuck.
    emulator_->releaseAllKeys();
    QWidget::focusOutEvent(event);
}
