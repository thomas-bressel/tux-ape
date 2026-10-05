#include "screenwidget.h"

#include <cstring>

#include <QKeyEvent>
#include <QPainter>

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

void ScreenWidget::fetchFrame()
{
    image_ = emulator_->frame();
    update();
}

void ScreenWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    // Largest picture of the right proportions that fits, centred.
    QSize size(kDisplayWidth, kDisplayHeight);
    size.scale(this->size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    if (target != rect())
        painter.fillRect(rect(), Qt::black);
    // At half size a CPC line is one screen line: there is no second line
    // to leave out.
    if (renderBoth_ || halfSize_ || image_.isNull()) {
        painter.drawImage(target, image_);
        return;
    }
    if (striped_.isNull()) {
        striped_ = QImage(image_.width(), image_.height() * 2, QImage::Format_RGB32);
        striped_.fill(Qt::black);
    }
    const qsizetype rowBytes = static_cast<qsizetype>(image_.width()) * 4;
    for (int y = 0; y < image_.height(); ++y)
        std::memcpy(striped_.scanLine(y * 2), image_.constScanLine(y), static_cast<size_t>(rowBytes));
    painter.drawImage(target, striped_);
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
