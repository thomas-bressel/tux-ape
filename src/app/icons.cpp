#include "icons.h"

#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

// Icons are designed on a 20 x 20 grid and rendered at several scales.
constexpr int kGrid = 20;

const QColor kInk(0x30, 0x30, 0x38);
const QColor kGreen(0x1E, 0x9E, 0x3A);
const QColor kBlue(0x2F, 0x6F, 0xD0);
const QColor kRed(0xC8, 0x30, 0x30);
const QColor kYellow(0xE8, 0xB0, 0x20);
const QColor kPaper(0xF4, 0xF4, 0xF0);

QPen outline(const QColor& colour = kInk, qreal width = 1.4)
{
    QPen pen(colour, width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    return pen;
}

void arrow(QPainter& p, QPointF from, QPointF to, const QColor& colour)
{
    p.setPen(outline(colour, 1.8));
    p.drawLine(from, to);
    const qreal angle = std::atan2(to.y() - from.y(), to.x() - from.x());
    for (qreal side : {-1.0, 1.0}) {
        const qreal a = angle + M_PI + side * 0.6;
        p.drawLine(to, to + QPointF(std::cos(a), std::sin(a)) * 3.5);
    }
}

void camera(QPainter& p)
{
    p.setPen(outline());
    p.setBrush(kPaper);
    p.drawRoundedRect(QRectF(2, 8, 12, 9), 1.5, 1.5);
    p.drawRect(QRectF(5, 6, 4, 2));
    p.setBrush(kBlue);
    p.drawEllipse(QPointF(8, 12.5), 2.6, 2.6);
}

void draw(QPainter& p, IconId id)
{
    switch (id) {
    case IconId::Run: {
        p.setPen(outline(kGreen.darker(130)));
        p.setBrush(kGreen);
        p.drawPolygon(QPolygonF({{5, 3}, {16, 10}, {5, 17}}));
        break;
    }
    case IconId::Pause:
        p.setPen(outline(kBlue.darker(130)));
        p.setBrush(kBlue);
        p.drawRect(QRectF(4.5, 3.5, 4, 13));
        p.drawRect(QRectF(11.5, 3.5, 4, 13));
        break;
    case IconId::SingleStep:
        // An arrow going down into a line of code.
        p.setPen(outline());
        for (qreal y : {4.0, 8.0, 12.0, 16.0})
            p.drawLine(QPointF(10, y), QPointF(17, y));
        arrow(p, {5, 3}, {5, 15}, kGreen);
        break;
    case IconId::StepOver: {
        // An arrow jumping over a line of code.
        p.setPen(outline());
        for (qreal y : {8.0, 12.0, 16.0})
            p.drawLine(QPointF(8, y), QPointF(17, y));
        QPainterPath arc;
        arc.moveTo(4, 14);
        arc.cubicTo(1, 2, 12, 1, 14, 5);
        p.setPen(outline(kGreen, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(arc);
        p.drawLine(QPointF(14, 5), QPointF(10.5, 4.2));
        p.drawLine(QPointF(14, 5), QPointF(13.6, 1.6));
        break;
    }
    case IconId::Registers:
        // An integrated circuit.
        p.setPen(outline());
        for (qreal y : {5.0, 8.3, 11.7, 15.0}) {
            p.drawLine(QPointF(2, y), QPointF(5, y));
            p.drawLine(QPointF(15, y), QPointF(18, y));
        }
        p.setBrush(kInk.lighter(160));
        p.drawRect(QRectF(5, 2.5, 10, 15));
        p.setBrush(kPaper);
        p.drawEllipse(QPointF(10, 4.8), 1.2, 1.2);
        break;
    case IconId::Assembler:
        // A page of source code.
        p.setPen(outline());
        p.setBrush(kPaper);
        p.drawRect(QRectF(3.5, 2, 13, 16));
        p.setPen(outline(kBlue, 1.4));
        p.drawLine(QPointF(6, 6), QPointF(11, 6));
        p.drawLine(QPointF(8, 9), QPointF(14, 9));
        p.drawLine(QPointF(8, 12), QPointF(12, 12));
        p.setPen(outline(kRed, 1.4));
        p.drawLine(QPointF(6, 15), QPointF(10, 15));
        break;
    case IconId::Disc:
        // A 3-inch disc: tall case, shutter at the top, label below.
        p.setPen(outline());
        p.setBrush(kBlue);
        p.drawRoundedRect(QRectF(4, 2, 12, 16), 1, 1);
        p.setBrush(kInk.lighter(220));
        p.drawRect(QRectF(7, 2, 6, 5));
        p.setBrush(kPaper);
        p.drawRect(QRectF(6, 10, 8, 6));
        break;
    case IconId::Cartridge:
        p.setPen(outline());
        p.setBrush(kInk.lighter(170));
        p.drawPolygon(QPolygonF({{3, 4}, {17, 4}, {17, 14}, {14, 14}, {14, 17}, {6, 17}, {6, 14}, {3, 14}}));
        p.setBrush(kYellow);
        p.drawRect(QRectF(6, 6.5, 8, 4.5));
        break;
    case IconId::Library:
        // Books on a shelf.
        p.setPen(outline());
        p.setBrush(kRed);
        p.drawRect(QRectF(2.5, 4, 4, 12));
        p.setBrush(kBlue);
        p.drawRect(QRectF(6.5, 2.5, 4, 13.5));
        p.setBrush(kYellow);
        p.drawRect(QRectF(10.5, 5, 3.5, 11));
        p.setBrush(kGreen);
        p.drawPolygon(QPolygonF({{14.5, 6.5}, {17.5, 5.5}, {19, 15.5}, {16, 16}}));
        p.drawLine(QPointF(1.5, 17.5), QPointF(19, 17.5));
        break;
    case IconId::Tape:
        p.setPen(outline());
        p.setBrush(kInk.lighter(170));
        p.drawRoundedRect(QRectF(2, 4.5, 16, 11), 1.5, 1.5);
        p.setBrush(kPaper);
        p.drawRect(QRectF(4.5, 6.5, 11, 5));
        p.setBrush(kInk);
        p.drawEllipse(QPointF(7, 9), 1.4, 1.4);
        p.drawEllipse(QPointF(13, 9), 1.4, 1.4);
        break;
    case IconId::Photo:
        // A camera: the hump of its viewfinder, its body, its lens.
        p.setPen(outline());
        p.setBrush(kInk.lighter(170));
        p.drawRoundedRect(QRectF(7, 3.5, 6, 4), 1, 1);
        p.drawRoundedRect(QRectF(2, 5.5, 16, 10.5), 1.5, 1.5);
        p.setBrush(kPaper);
        p.drawEllipse(QPointF(10, 10.8), 3.4, 3.4);
        p.setBrush(kInk);
        p.drawEllipse(QPointF(10, 10.8), 1.5, 1.5);
        break;
    case IconId::LoadSnapshot:
        camera(p);
        arrow(p, {17, 3}, {17, 12}, kGreen);
        break;
    case IconId::SaveSnapshot:
        camera(p);
        arrow(p, {17, 12}, {17, 3}, kRed);
        break;
    case IconId::Settings: {
        // A cog wheel.
        p.setPen(Qt::NoPen);
        p.setBrush(kInk.lighter(140));
        p.translate(10, 10);
        for (int tooth = 0; tooth < 8; ++tooth) {
            p.drawRect(QRectF(-1.6, -8.5, 3.2, 4));
            p.rotate(45);
        }
        p.drawEllipse(QPointF(0, 0), 6, 6);
        p.setBrush(kPaper);
        p.drawEllipse(QPointF(0, 0), 2.4, 2.4);
        p.resetTransform();
        break;
    }
    case IconId::FullScreen:
        p.setPen(outline());
        p.setBrush(kBlue);
        p.drawRect(QRectF(2.5, 4, 15, 11));
        p.setPen(outline(kPaper, 1.5));
        p.drawLine(QPointF(5, 9), QPointF(5, 6.5));
        p.drawLine(QPointF(5, 6.5), QPointF(8, 6.5));
        p.drawLine(QPointF(15, 10), QPointF(15, 12.5));
        p.drawLine(QPointF(15, 12.5), QPointF(12, 12.5));
        p.setPen(outline());
        p.drawLine(QPointF(7, 17.5), QPointF(13, 17.5));
        break;
    case IconId::Help: {
        p.setPen(outline(kBlue.darker(130)));
        p.setBrush(kBlue);
        p.drawEllipse(QPointF(10, 10), 8, 8);
        QFont font = p.font();
        font.setPixelSize(13);
        font.setBold(true);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(QRectF(2, 2, 16, 16), Qt::AlignCenter, QStringLiteral("?"));
        break;
    }
    }
}

}  // namespace

QIcon makeIcon(IconId id)
{
    QIcon icon;
    for (int scale : {1, 2, 3}) {
        QPixmap pixmap(kGrid * scale, kGrid * scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);
        draw(painter, id);
        painter.end();
        pixmap.setDevicePixelRatio(scale);
        icon.addPixmap(pixmap);
    }
    return icon;
}
