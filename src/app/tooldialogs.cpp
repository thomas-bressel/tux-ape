#include "tooldialogs.h"

#include <cstring>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QDialogButtonBox* okCancelHelp(QDialog* dialog)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    buttons->button(QDialogButtonBox::Help)->setEnabled(false);
    buttons->button(QDialogButtonBox::Help)->setToolTip(QDialog::tr("Not available yet"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

void put16(QByteArray& out, int value)
{
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>(value >> 8 & 0xFF));
}

// Truevision Targa, 24 bits, not compressed, first line at the top.
QByteArray targa(const QImage& image)
{
    QByteArray out;
    out.append(QByteArray(2, '\0'));
    out.append('\x02');                // true colour, no compression
    out.append(QByteArray(9, '\0'));   // no colour map, origin 0,0
    put16(out, image.width());
    put16(out, image.height());
    out.append('\x18');                // 24 bits per pixel
    out.append('\x20');                // top to bottom
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            out.append(static_cast<char>(qBlue(line[x])));
            out.append(static_cast<char>(qGreen(line[x])));
            out.append(static_cast<char>(qRed(line[x])));
        }
    }
    return out;
}

// ZSoft PC Paintbrush version 5, 24 bits as three planes, run-length coded.
QByteArray pcx(const QImage& image)
{
    const int width = image.width(), height = image.height();
    const int bytesPerLine = (width + 1) & ~1;
    QByteArray out;
    out.append('\x0A');  // ZSoft
    out.append('\x05');  // version 5
    out.append('\x01');  // run-length coded
    out.append('\x08');  // 8 bits per plane
    put16(out, 0);
    put16(out, 0);
    put16(out, width - 1);
    put16(out, height - 1);
    put16(out, 72);
    put16(out, 72);
    out.append(QByteArray(48, '\0'));  // no 16-colour palette
    out.append('\0');
    out.append('\x03');                // three planes
    put16(out, bytesPerLine);
    put16(out, 1);                     // colour
    out.append(QByteArray(58, '\0'));
    QByteArray plane(bytesPerLine, '\0');
    for (int y = 0; y < height; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int channel = 0; channel < 3; ++channel) {
            for (int x = 0; x < width; ++x)
                plane[x] = static_cast<char>(channel == 0 ? qRed(line[x]) : channel == 1 ? qGreen(line[x]) : qBlue(line[x]));
            for (int x = 0; x < bytesPerLine;) {
                const char value = plane[x];
                int run = 1;
                while (x + run < bytesPerLine && run < 63 && plane[x + run] == value)
                    ++run;
                if (run > 1 || (static_cast<unsigned char>(value) & 0xC0) == 0xC0)
                    out.append(static_cast<char>(0xC0 | run));
                out.append(value);
                x += run;
            }
        }
    }
    return out;
}

}  // namespace

// ---- Screenshots --------------------------------------------------------

ScreenshotDialog::ScreenshotDialog(const QImage& picture, QWidget* parent)
    : QDialog(parent)
    , picture_(picture.convertToFormat(QImage::Format_RGB32))
{
    setWindowTitle(tr("Save Screenshot"));

    auto* previewBox = new QGroupBox(tr("Preview"));
    preview_ = new QLabel;
    preview_->setObjectName("Image1");
    preview_->setFixedSize(384, 270);
    preview_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    preview_->setStyleSheet("background-color: black;");
    auto* previewLayout = new QVBoxLayout(previewBox);
    previewLayout->addWidget(preview_);

    halfSize_ = new QCheckBox(tr("Half Size"));
    halfSize_->setObjectName("ckHalfSize");
    halfHeight_ = new QCheckBox(tr("Half Height"));
    halfHeight_->setObjectName("ckHalfHeight");
    connect(halfSize_, &QCheckBox::toggled, this, &ScreenshotDialog::updatePreview);
    connect(halfHeight_, &QCheckBox::toggled, this, &ScreenshotDialog::updatePreview);
    auto* options = new QHBoxLayout;
    options->addWidget(halfSize_);
    options->addWidget(halfHeight_);
    options->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(previewBox);
    layout->addLayout(options);
    layout->addWidget(okCancelHelp(this));
    updatePreview();
}

void ScreenshotDialog::setHalfSize(bool half)
{
    halfSize_->setChecked(half);
}

void ScreenshotDialog::setHalfHeight(bool half)
{
    halfHeight_->setChecked(half);
}

bool ScreenshotDialog::halfSize() const
{
    return halfSize_->isChecked();
}

bool ScreenshotDialog::halfHeight() const
{
    return halfHeight_->isChecked();
}

QImage ScreenshotDialog::result() const
{
    if (picture_.isNull())
        return picture_;
    QImage out = picture_;
    // A CPC line is two screen lines: dropping every other loses nothing.
    if (halfHeight()) {
        QImage lines(out.width(), out.height() / 2, QImage::Format_RGB32);
        for (int y = 0; y < lines.height(); ++y)
            std::memcpy(lines.scanLine(y), out.constScanLine(y * 2), static_cast<size_t>(out.bytesPerLine()));
        out = lines;
    }
    // Halving the width blends pixels in pairs, which keeps mode 2 text
    // readable.
    if (halfSize())
        out = out.scaled(out.width() / 2, out.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return out;
}

// As in WinAPE the preview shows the picture at its real size, as much of
// it as fits.
void ScreenshotDialog::updatePreview()
{
    preview_->setPixmap(QPixmap::fromImage(result().copy(0, 0, preview_->width(), preview_->height())));
}

QString ScreenshotDialog::fileFilter()
{
    // WinAPE's list. Bitmap, PCX and Targa are written here; the others
    // only if this Qt can.
    const QList<QByteArray> known = QImageWriter::supportedImageFormats();
    QStringList filters = {tr("PNG files (*.png)"), tr("Bitmap files (*.bmp)"), tr("PCX files (*.pcx)")};
    if (known.contains("jpg") || known.contains("jpeg"))
        filters << tr("JPEG files (*.jpg)");
    if (known.contains("tif") || known.contains("tiff"))
        filters << tr("TIFF files (*.tif)");
    filters << tr("Targa files (*.tga)") << tr("All files (*)");
    return filters.join(";;");
}

bool ScreenshotDialog::save(const QImage& picture, const QString& path)
{
    const QString kind = QFileInfo(path).suffix().toLower();
    if (kind == "pcx" || kind == "tga") {
        const QImage image = picture.convertToFormat(QImage::Format_RGB32);
        QFile file(path);
        const QByteArray data = kind == "pcx" ? pcx(image) : targa(image);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
    return picture.save(path, kind.isEmpty() ? "png" : nullptr);
}

// ---- Auto-Type ----------------------------------------------------------

AutoTypeDialog::AutoTypeDialog(const QString& text, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("TuxAPE Auto-Type"));

    edit_ = new QPlainTextEdit;
    edit_->setObjectName("mText");
    edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setPlainText(text);

    auto* load = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Load"));
    load->setObjectName("bLoad");
    auto* save = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save"));
    save->setObjectName("bSave");
    const QString filter = tr("Text files (*.txt);;All files (*)");
    connect(load, &QPushButton::clicked, this, [this, filter] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Load Auto-Type Text"), QString(), filter);
        if (!path.isEmpty() && !loadText(path))
            QMessageBox::warning(this, windowTitle(), tr("Cannot read %1.").arg(QDir::toNativeSeparators(path)));
    });
    connect(save, &QPushButton::clicked, this, [this, filter] {
        QString path = QFileDialog::getSaveFileName(this, tr("Save Auto-Type Text"), QString(), filter);
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().isEmpty())
            path += ".txt";
        if (!saveText(path))
            QMessageBox::warning(this, windowTitle(), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
    });
    auto* row = new QHBoxLayout;
    row->addWidget(load);
    row->addWidget(save);
    row->addStretch(1);
    row->addWidget(okCancelHelp(this));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(edit_, 1);
    layout->addLayout(row);
    resize(420, 150);
}

QString AutoTypeDialog::text() const
{
    return edit_->toPlainText();
}

void AutoTypeDialog::setText(const QString& text)
{
    edit_->setPlainText(text);
}

bool AutoTypeDialog::loadText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    // The CPC's characters are Latin-1 as far as Auto-Type goes.
    edit_->setPlainText(QString::fromLatin1(file.readAll()).replace("\r\n", "\n"));
    return true;
}

bool AutoTypeDialog::saveText(const QString& path) const
{
    QFile file(path);
    const QByteArray data = text().toLatin1();
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
