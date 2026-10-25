#include "printerwindow.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "contrast.h"
#include "emulator.h"

using tuxape::Dmp2000;

namespace {

constexpr double kNeedle = 1.0 / 72.0;
// A needle's dot: a needle is 0.3 mm across, its dot through the ribbon a
// little more, nearly a needle's step: a column of them all but joins up.
constexpr double kDotRadius = 0.0068;
// A little longer the way the head goes, for it moves while the needle
// is on the paper.
constexpr double kDotLength = 1.08;
// The nine needles, top to bottom: how hard each strikes (the lowest, at
// the end of the longest wire, least), and how far off its place it is,
// in inches, its wire a little bent.
constexpr float kForce[9] = {0.94f, 1.0f, 0.97f, 1.0f, 1.02f, 0.98f, 1.0f, 0.95f, 0.86f};
constexpr double kOffX[9] = {0.0, 0.0004, 0.0, -0.0003, 0.0, 0.0, 0.0005, 0.0, -0.0004};
constexpr double kOffY[9] = {0.0, 0.0, 0.0003, 0.0, 0.0, -0.0004, 0.0, 0.0, 0.0003};
// Dots struck right to left land this much further left than those
// struck left to right: the needle's flight, set right for one way only.
constexpr double kBackward = 0.0025;
// The ink of one dot at full strength, and the ribbon's weave in it: a
// thread every 1/400 inch.
constexpr double kInkAlpha = 0.86;
constexpr double kWeave = 400.0;
// The tractors' strips of fan fold paper, and their holes.
constexpr double kStrip = 0.5;
constexpr double kHolePitch = 0.5;
constexpr double kHoleRadius = 0.078;
// A while without needles striking: the next ones are a new print.
constexpr uint64_t kQuiet = 2000000;

// The paper, and what is printed on it: colours of a thing, not of the
// window, the same whatever the theme.
const QColor kPaper(0xFB, 0xFA, 0xF4);
const QColor kInk(0x24, 0x22, 0x2A);

uint64_t mix(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// 0 to 1, from a hash.
double unit(uint64_t h)
{
    return static_cast<double>(h >> 11) / 9007199254740992.0;
}

double between(const Dmp2000::Motion& m, uint64_t now)
{
    if (now <= m.start || m.end <= m.start)
        return now <= m.start ? m.from : m.to;
    if (now >= m.end)
        return m.to;
    return m.from + (m.to - m.from) * static_cast<double>(now - m.start) / static_cast<double>(m.end - m.start);
}

}  // namespace

// ---- the sheet --------------------------------------------------------------------

SheetView::SheetView(QWidget* parent)
    : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setPaper(Dmp2000::a4());
}

void SheetView::setPaper(const Dmp2000::Paper& paper)
{
    paper_ = paper;
    blankPicture();
    updateGeometry();
    update();
}

void SheetView::blankPicture()
{
    picture_ = QImage(static_cast<int>(std::lround(paper_.width * kDotsPerInch)),
                      static_cast<int>(std::lround(paper_.length * kDotsPerInch)), QImage::Format_ARGB32_Premultiplied);
    picture_.fill(Qt::transparent);
}

void SheetView::showSheet(const PrintedSheet& sheet)
{
    blankPicture();
    for (const Dmp2000::Strike& s : sheet.strikes)
        strike(s);
    update();
}

void SheetView::setImpression(double impression)
{
    impression_ = std::clamp(impression, 0.0, 1.0);
}

void SheetView::paint(QImage& picture, double dotsPerInch, const Dmp2000::Strike& strike, double impression)
{
    // Struck harder (the lever nearer the platen), a dot is larger and
    // darker.
    const double radius = kDotRadius * (0.88 + 0.12 * impression) * dotsPerInch;
    const double strength = kInkAlpha * strike.ink * (0.62 + 0.38 * impression);
    // The dot's edge, soft over two thousandths of an inch, or over a pixel
    // when the picture is coarser.
    const double edge = std::max(0.0018 * dotsPerInch, 0.75);
    const int r = kInk.red(), g = kInk.green(), b = kInk.blue();
    // Or the ink in grey on white paper, a third of the memory.
    const bool grey = picture.format() == QImage::Format_Grayscale8;
    const int inkGrey = qGray(kInk.rgb());
    const int w = picture.width(), h = picture.height();
    for (int pin = 0; pin < 9; ++pin) {
        if (!(strike.pins & (0x100 >> pin)))
            continue;
        const double x = (strike.x + kOffX[pin] - (strike.backward ? kBackward : 0.0)) * dotsPerInch;
        const double y = (strike.y + strike.slip + pin * kNeedle + kOffY[pin]) * dotsPerInch;
        // Not quite round, and not twice the same: its outline in three
        // waves, from where and when it was struck.
        const uint64_t seed = mix(mix(static_cast<uint64_t>(std::llround(strike.x * 14400.0))) ^
                                  static_cast<uint64_t>(std::llround(strike.y * 14400.0)) * 131u ^ strike.microsecond * 9u ^ pin);
        double wave[3][3];  // amount, cosine and sine of its turn
        for (int k = 0; k < 3; ++k) {
            const double turn = 2.0 * M_PI * unit(mix(seed + k * 2 + 1));
            wave[k][0] = (0.07 - 0.02 * k) * (0.5 + unit(mix(seed + k * 2 + 2)));
            wave[k][1] = std::cos(turn);
            wave[k][2] = std::sin(turn);
        }
        const float force = kForce[pin];
        const double reach = radius * 1.3 + edge;
        const int x0 = std::max(0, static_cast<int>(std::floor(x - reach))), x1 = std::min(w - 1, static_cast<int>(std::ceil(x + reach)));
        const int y0 = std::max(0, static_cast<int>(std::floor(y - reach))), y1 = std::min(h - 1, static_cast<int>(std::ceil(y + reach)));
        for (int py = y0; py <= y1; ++py) {
            auto* line = reinterpret_cast<QRgb*>(picture.scanLine(py));
            for (int px = x0; px <= x1; ++px) {
                const double dx = (px + 0.5 - x) / kDotLength, dy = py + 0.5 - y;
                const double d = std::hypot(dx, dy);
                // The outline at this angle: cos and sin of n times it.
                const double c1 = d > 0 ? dx / d : 1.0, s1 = d > 0 ? dy / d : 0.0;
                const double c2 = c1 * c1 - s1 * s1, s2 = 2 * c1 * s1;
                const double c3 = c2 * c1 - s2 * s1, s3 = s2 * c1 + c2 * s1;
                const double outline = radius * (1.0 + wave[0][0] * (c1 * wave[0][1] + s1 * wave[0][2]) +
                                                 wave[1][0] * (c2 * wave[1][1] + s2 * wave[1][2]) +
                                                 wave[2][0] * (c3 * wave[2][1] + s3 * wave[2][2]));
                const double cover = std::clamp((outline - d) / edge + 0.5, 0.0, 1.0);
                if (cover <= 0.0)
                    continue;
                // Denser where the needle's middle pressed; the weave's
                // threads, where they are on the paper.
                const double middle = 0.88 + 0.12 * std::max(0.0, 1.0 - (d * d) / (radius * radius));
                const uint64_t thread = mix(static_cast<uint64_t>((px + 0.5) / dotsPerInch * kWeave) * 0x10001u +
                                            static_cast<uint64_t>((py + 0.5) / dotsPerInch * kWeave));
                const double weave = 0.76 + 0.24 * unit(thread);
                const double ink = std::min(1.0, cover * strength * force * middle * weave);
                // On what is there already: a dot on a dot is darker.
                if (grey) {
                    uchar& pixel = reinterpret_cast<uchar*>(line)[px];
                    pixel = static_cast<uchar>(pixel - std::lround((pixel - inkGrey) * ink));
                    continue;
                }
                QRgb& pixel = line[px];
                const int a = qAlpha(pixel);
                const int now = a + static_cast<int>(std::lround((255 - a) * ink));
                pixel = qRgba(r * now / 255, g * now / 255, b * now / 255, now);
            }
        }
    }
}

void SheetView::strike(const Dmp2000::Strike& strike)
{
    paint(picture_, kDotsPerInch, strike, impression_);
    const double s = scale() * kDotsPerInch;
    const QRectF touched(strike.x - 0.02, strike.y - 0.02, 0.04, 9 * kNeedle + 0.04);
    update(QRectF(touched.x() * s, touched.y() * s, touched.width() * s, touched.height() * s).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void SheetView::setHead(double x, double y, bool shown)
{
    if (shown == headShown_ && std::fabs(x - headX_) < 1e-4 && std::fabs(y - headY_) < 1e-4)
        return;
    // Only where it was and where it is drawn again.
    if (headShown_)
        update(headRect().toAlignedRect().adjusted(-2, -2, 2, 2));
    headX_ = x;
    headY_ = y;
    headShown_ = shown;
    if (headShown_)
        update(headRect().toAlignedRect().adjusted(-2, -2, 2, 2));
}

QRectF SheetView::headRect() const
{
    // The head: its nine needles' height, a little wider than a
    // character, over the line it prints.
    const double inch = scale() * kDotsPerInch;
    return {(headX_ - 0.06) * inch, (headY_ - 0.03) * inch, 0.12 * inch, (9 * kNeedle + 0.06) * inch};
}

double SheetView::scale() const
{
    return picture_.width() > 0 ? static_cast<double>(width()) / picture_.width() : 1.0;
}

int SheetView::lineInView() const
{
    return static_cast<int>(headY_ * kDotsPerInch * scale());
}

QSize SheetView::sizeHint() const
{
    return {720, static_cast<int>(720 * paper_.length / paper_.width)};
}

void SheetView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // As tall as the paper is for this width.
    const int tall = static_cast<int>(std::lround(width() * paper_.length / paper_.width));
    if (minimumHeight() != tall)
        setMinimumHeight(tall);
}

void SheetView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const double inch = scale() * kDotsPerInch;
    painter.fillRect(rect(), kPaper);
    if (paper_.fanFold) {
        // The tractors' strips: their holes, and the perforations that
        // tear them off; the sheet's top and bottom perforated too.
        QPen perforation(QColor(0xB8, 0xB4, 0xA8), 1, Qt::DashLine);
        painter.setPen(perforation);
        for (double x : {kStrip, paper_.width - kStrip})
            painter.drawLine(QPointF(x * inch, 0), QPointF(x * inch, height()));
        painter.drawLine(QPointF(0, 1), QPointF(width(), 1));
        painter.drawLine(QPointF(0, height() - 1), QPointF(width(), height() - 1));
        painter.setPen(QColor(0xC8, 0xC4, 0xB8));
        painter.setBrush(palette().color(QPalette::Window));
        for (double y = kHolePitch / 2; y < paper_.length; y += kHolePitch)
            for (double x : {kStrip / 2, paper_.width - kStrip / 2})
                painter.drawEllipse(QPointF(x * inch, y * inch), kHoleRadius * inch, kHoleRadius * inch);
    }
    painter.drawImage(rect(), picture_);
    if (headShown_) {
        QColor body(0x4A, 0x4D, 0x55);
        body.setAlphaF(0.55);
        painter.setPen(Qt::NoPen);
        painter.setBrush(body);
        painter.drawRoundedRect(headRect(), 2, 2);
    }
}

// ---- the window -------------------------------------------------------------------

PrinterWindow::PrinterWindow(Emulator* emulator, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , emulator_(emulator)
{
    setWindowTitle(tr("DMP2000"));
    setObjectName("PrinterWindow");
    // Shown when it starts printing, it leaves the keyboard to the machine.
    setAttribute(Qt::WA_ShowWithoutActivating);

    view_ = new SheetView;
    view_->setObjectName("SheetView");
    scroll_ = new QScrollArea;
    scroll_->setObjectName("saSheet");
    scroll_->setWidget(view_);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // The lamps, lit or not by their sign as well as their colour.
    const auto lamp = [](const char* name) {
        auto* label = new QLabel;
        label->setObjectName(name);
        label->setTextFormat(Qt::RichText);
        return label;
    };
    power_ = lamp("lPower");
    onlineLamp_ = lamp("lOnLine");
    paperOutLamp_ = lamp("lPaperOut");

    online_ = new QPushButton(tr("ON LINE"));
    online_->setObjectName("bOnLine");
    online_->setToolTip(tr("Off line, the printer takes nothing in, and its LF and FF buttons move the paper"));
    formFeed_ = new QPushButton(tr("FF"));
    formFeed_->setObjectName("bFormFeed");
    formFeed_->setToolTip(tr("Form feed: the paper to the next page's top, a cut sheet out (off line only)"));
    lineFeed_ = new QPushButton(tr("LF"));
    lineFeed_->setObjectName("bLineFeed");
    lineFeed_->setToolTip(tr("Line feed: the paper a sixth of an inch up (off line only)"));
    loadPaper_ = new QPushButton(tr("Insert a Sheet"));
    loadPaper_->setObjectName("bLoadPaper");
    loadPaper_->setToolTip(tr("A new sheet fed in, when the last one has gone out"));
    connect(online_, &QPushButton::clicked, this, [this] {
        if (emulator_)
            emulator_->setDmp2000Online(!emulator_->dmp2000Lamps().online);
        updateLamps();
    });
    connect(formFeed_, &QPushButton::clicked, this, [this] {
        if (emulator_)
            emulator_->dmp2000FormFeed();
    });
    connect(lineFeed_, &QPushButton::clicked, this, [this] {
        if (emulator_)
            emulator_->dmp2000LineFeed();
    });
    connect(loadPaper_, &QPushButton::clicked, this, [this] {
        if (emulator_)
            emulator_->dmp2000LoadPaper();
        updateLamps();
    });
    finish_ = new QPushButton(tr("Finish..."));
    finish_->setObjectName("bFinish");
    finish_->setToolTip(tr("The print is done: its sheets saved as a PDF, then taken away"));
    connect(finish_, &QPushButton::clicked, this, &PrinterWindow::finish);

    previous_ = new QToolButton;
    previous_->setObjectName("tbPreviousSheet");
    previous_->setArrowType(Qt::LeftArrow);
    previous_->setToolTip(tr("The sheet before"));
    next_ = new QToolButton;
    next_->setObjectName("tbNextSheet");
    next_->setArrowType(Qt::RightArrow);
    next_->setToolTip(tr("The sheet after"));
    sheetLabel_ = new QLabel;
    sheetLabel_->setObjectName("lSheet");
    connect(previous_, &QToolButton::clicked, this, [this] { showSheet(shown_ - 1); });
    connect(next_, &QToolButton::clicked, this, [this] { showSheet(shown_ + 1); });

    auto* panel = new QHBoxLayout;
    panel->addWidget(power_);
    panel->addWidget(onlineLamp_);
    panel->addWidget(paperOutLamp_);
    panel->addSpacing(12);
    panel->addWidget(online_);
    panel->addWidget(formFeed_);
    panel->addWidget(lineFeed_);
    panel->addSpacing(12);
    panel->addWidget(loadPaper_);
    panel->addWidget(finish_);
    panel->addStretch(1);
    panel->addWidget(previous_);
    panel->addWidget(sheetLabel_);
    panel->addWidget(next_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(scroll_, 1);
    layout->addLayout(panel);
    resize(780, 900);

    if (emulator_) {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &PrinterWindow::poll);
        timer_->start(30);
    } else {
        timer_ = nullptr;
    }
    updateLamps();
    updateSheetControls();
}

void PrinterWindow::setPaper(const Dmp2000::Paper& paper)
{
    if (paper == paper_ && !sheets_.empty())
        return;
    paper_ = paper;
    view_->setPaper(paper);
    if (shown_ >= 0)
        view_->showSheet(sheets_[static_cast<size_t>(shown_)]);
}

void PrinterWindow::setImpression(double impression)
{
    if (std::fabs(impression - view_->impression()) < 1e-9)
        return;
    view_->setImpression(impression);
    if (shown_ >= 0)
        view_->showSheet(sheets_[static_cast<size_t>(shown_)]);
}

void PrinterWindow::newPrinter()
{
    for (PrintedSheet& s : sheets_)
        s.done = true;
    int next = 0;
    for (const PrintedSheet& s : sheets_)
        next = std::max(next, s.number + 1);
    sheetBase_ = next;
    waiting_.clear();
    carriageKnown_ = paperKnown_ = false;
    striking_ = false;
}

void PrinterWindow::poll()
{
    uint64_t now = 0;
    int printer = 0;
    std::vector<Dmp2000::Event> events = emulator_->takeDmp2000Events(&now, &printer);
    if (printer != printer_) {
        if (printer_ >= 0)
            newPrinter();
        printer_ = printer;
    }
    feed(std::move(events), now);
    updateLamps();
}

void PrinterWindow::feed(std::vector<Dmp2000::Event> events, uint64_t now)
{
    // In with those waiting, in the order of their times.
    if (!events.empty()) {
        std::deque<Dmp2000::Event> merged;
        std::merge(waiting_.begin(), waiting_.end(), events.begin(), events.end(), std::back_inserter(merged),
                   [](const Dmp2000::Event& a, const Dmp2000::Event& b) { return a.microsecond < b.microsecond; });
        waiting_.swap(merged);
    }
    const bool wasQuiet = !striking_ || (now > lastStrike_ + kQuiet);
    bool struck = false;
    while (!waiting_.empty() && waiting_.front().microsecond <= now) {
        const Dmp2000::Event event = waiting_.front();
        waiting_.pop_front();
        struck = struck || event.kind == Dmp2000::Event::Kind::Strike;
        apply(event);
    }
    // The carriage and the paper as they are now, their motion begun.
    updateHead(now);
    if (struck && wasQuiet)
        emit printingStarted();
}

PrintedSheet& PrinterWindow::sheet(int number)
{
    for (PrintedSheet& s : sheets_)
        if (s.number == number)
            return s;
    PrintedSheet added;
    added.number = number;
    sheets_.push_back(added);
    return sheets_.back();
}

void PrinterWindow::apply(const Dmp2000::Event& event)
{
    using Kind = Dmp2000::Event::Kind;
    switch (event.kind) {
    case Kind::Strike: {
        PrintedSheet& s = sheet(sheetBase_ + event.strike.sheet);
        s.strikes.push_back(event.strike);
        s.strikes.back().sheet = s.number;
        if (shown_ >= 0 && sheets_[static_cast<size_t>(shown_)].number == s.number)
            view_->strike(event.strike);
        striking_ = true;
        lastStrike_ = event.microsecond;
        break;
    }
    case Kind::Printed: {
        PrintedSheet& s = sheet(sheetBase_ + event.printed.sheet);
        s.text.push_back(event.printed);
        s.text.back().sheet = s.number;
        break;
    }
    case Kind::Motion:
        if (event.motion.kind == Dmp2000::Motion::Kind::Carriage) {
            carriage_ = event.motion;
            carriageKnown_ = true;
        } else {
            paperMotion_ = event.motion;
            paperKnown_ = true;
        }
        break;
    case Kind::SheetStarted: {
        sheet(sheetBase_ + event.sheet);
        if (following_ || shown_ < 0) {
            showSheet(static_cast<int>(sheets_.size()) - 1);
            following_ = true;
        }
        // A new sheet is at its top: the paper's motion was the last one's.
        paperKnown_ = false;
        break;
    }
    case Kind::SheetDone:
        sheet(sheetBase_ + event.sheet).done = true;
        break;
    case Kind::PaperOut:
    case Kind::Beep:
        break;
    }
    updateSheetControls();
}

void PrinterWindow::showSheet(int index)
{
    if (index < 0 || index >= static_cast<int>(sheets_.size()))
        return;
    shown_ = index;
    following_ = index == static_cast<int>(sheets_.size()) - 1;
    view_->showSheet(sheets_[static_cast<size_t>(index)]);
    updateSheetControls();
}

void PrinterWindow::updateHead(uint64_t now)
{
    const bool current = following_ && shown_ >= 0;
    if (!current || !carriageKnown_) {
        view_->setHead(0, 0, false);
        return;
    }
    const double x = between(carriage_, now);
    double y = paperKnown_ ? between(paperMotion_, now) : (paper_.fanFold ? 0.0 : 1.0 / 3.0);
    // The last strike says where the line is if the paper has not moved.
    const PrintedSheet& s = sheets_[static_cast<size_t>(shown_)];
    if (!paperKnown_ && !s.strikes.empty())
        y = s.strikes.back().y;
    view_->setHead(x, y, true);
    // The line kept in sight.
    const int line = view_->lineInView();
    QScrollBar* bar = scroll_->verticalScrollBar();
    const int page = scroll_->viewport()->height();
    if (line < bar->value() + page / 6 || line > bar->value() + page * 5 / 6)
        bar->setValue(line - page / 2);
}

void PrinterWindow::updateLamps()
{
    const Emulator::Dmp2000Lamps lamps = emulator_ ? emulator_->dmp2000Lamps() : Emulator::Dmp2000Lamps{true, true, false};
    const QColor window = palette().color(QPalette::Window);
    const auto show = [&](QLabel* label, const QString& name, bool lit, const QColor& colour) {
        // Lit: a full disc in the lamp's colour; out: a ring.
        const QColor sign = lit ? readable(colour, window, kGraphicContrast) : palette().color(QPalette::WindowText);
        label->setText(QStringLiteral("<span style=\"color:%1\">%2</span> %3")
                           .arg(sign.name(), lit ? QStringLiteral("●") : QStringLiteral("○"), name.toHtmlEscaped()));
        label->setToolTip(lit ? tr("%1: lit").arg(name) : tr("%1: out").arg(name));
    };
    show(power_, tr("POWER"), lamps.present, QColor(0xE0, 0x3C, 0x31));
    show(onlineLamp_, tr("ON LINE"), lamps.present && lamps.online, QColor(0x2E, 0xA0, 0x43));
    show(paperOutLamp_, tr("PAPER OUT"), lamps.present && lamps.paperOut, QColor(0xE0, 0x3C, 0x31));
    online_->setEnabled(lamps.present);
    formFeed_->setEnabled(lamps.present && !lamps.online);
    lineFeed_->setEnabled(lamps.present && !lamps.online);
    loadPaper_->setEnabled(lamps.present && lamps.paperOut);
}

void PrinterWindow::updateSheetControls()
{
    const int count = static_cast<int>(sheets_.size());
    sheetLabel_->setText(count ? tr("Sheet %1 of %2").arg(shown_ + 1).arg(count) : tr("No sheet"));
    previous_->setEnabled(shown_ > 0);
    next_->setEnabled(shown_ >= 0 && shown_ < count - 1);
    finish_->setEnabled(std::any_of(sheets_.begin(), sheets_.end(), [](const PrintedSheet& s) { return !s.strikes.empty(); }));
}

void PrinterWindow::finish()
{
    if (!finish_->isEnabled())
        return;
    if (pdfFolder_.isEmpty())
        pdfFolder_ = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(this, tr("Save the Print"), QDir(pdfFolder_).filePath(tr("print.pdf")),
                                                tr("PDF Files (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");
    pdfFolder_ = QFileInfo(path).absolutePath();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool written = writePdf(path);
    QApplication::restoreOverrideCursor();
    if (!written) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot write the print to %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    takeAway();
}

void PrinterWindow::takeAway()
{
    // The sheet still in the printer stays, blank above the head: what was
    // on it has gone with the others.
    std::vector<PrintedSheet> kept;
    if (!sheets_.empty() && !sheets_.back().done) {
        PrintedSheet blank;
        blank.number = sheets_.back().number;
        kept.push_back(blank);
    }
    sheets_.swap(kept);
    shown_ = -1;
    following_ = true;
    if (!sheets_.empty())
        showSheet(0);
    else
        view_->setPaper(paper_);
    updateSheetControls();
}

bool PrinterWindow::writePdf(const QString& path, int dotsPerInch) const
{
    std::vector<const PrintedSheet*> printed;
    for (const PrintedSheet& s : sheets_)
        if (!s.strikes.empty())
            printed.push_back(&s);
    if (printed.empty())
        return false;
    QPdfWriter pdf(path);
    pdf.setCreator(QStringLiteral("TuxAPE"));
    pdf.setTitle(tr("DMP2000 print"));
    pdf.setResolution(dotsPerInch);
    // Pages the paper's size, printed to their edges.
    pdf.setPageLayout(QPageLayout(QPageSize(QSizeF(paper_.width, paper_.length), QPageSize::Inch, QString(), QPageSize::ExactMatch),
                                  QPageLayout::Portrait, QMarginsF()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    pdf.setColorModel(QPdfWriter::ColorModel::Grayscale);
#endif
    QPainter painter;
    if (!painter.begin(&pdf))
        return false;
    // The dots as they are, not as JPEG would have them.
    painter.setRenderHint(QPainter::LosslessImageRendering);
    const int width = static_cast<int>(std::lround(paper_.width * dotsPerInch));
    const int height = static_cast<int>(std::lround(paper_.length * dotsPerInch));
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    // The text over the dots, as big as they are: the nine needles' height.
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(dotsPerInch * 9.0 / 72.0))));
    painter.setFont(font);
    const QFontMetricsF metrics(font, &pdf);
    for (size_t page = 0; page < printed.size(); ++page) {
        if (page > 0)
            pdf.newPage();
        // The dots, in grey on white: the ink of each and the paper's
        // white, not the screen's paper.
        {
            QImage dots(width, height, QImage::Format_Grayscale8);
            dots.fill(255);
            for (const Dmp2000::Strike& strike : printed[page]->strikes)
                SheetView::paint(dots, dotsPerInch, strike, view_->impression());
            painter.drawImage(QRectF(0, 0, width, height), dots);
        }
        // The characters, where they are printed and unseen, to be found
        // and copied: a line's run at a time, spaces where the head went
        // by without printing.
        std::vector<Dmp2000::Printed> text = printed[page]->text;
        std::stable_sort(text.begin(), text.end(), [](const Dmp2000::Printed& a, const Dmp2000::Printed& b) {
            return std::fabs(a.y - b.y) > 1e-6 ? a.y < b.y : a.x < b.x;
        });
        painter.setPen(QColor(0, 0, 0, 0));
        for (size_t i = 0; i < text.size();) {
            const Dmp2000::Printed& first = text[i];
            QString run = QString::fromUcs4(&first.character, 1);
            double end = first.x + first.width;
            size_t j = i + 1;
            for (; j < text.size(); ++j) {
                const Dmp2000::Printed& next = text[j];
                if (std::fabs(next.y - first.y) > 1e-6 || std::fabs(next.width - first.width) > 1e-6 || next.x < end - 1e-6)
                    break;
                const double gap = (next.x - end) / first.width;
                if (std::fabs(gap - std::round(gap)) > 0.05)
                    break;
                run += QString(static_cast<int>(std::lround(gap)), QLatin1Char(' '));
                run += QString::fromUcs4(&next.character, 1);
                end = next.x + next.width;
            }
            const double natural = metrics.horizontalAdvance(run);
            if (natural > 0) {
                painter.save();
                // The top needle's line, then the base line at the
                // seventh needle.
                painter.translate(first.x * dotsPerInch, (first.y + 7.0 / 72.0) * dotsPerInch);
                painter.scale((end - first.x) * dotsPerInch / natural, 1.0);
                painter.drawText(QPointF(0, 0), run);
                painter.restore();
            }
            i = j;
        }
    }
    return painter.end();
}
