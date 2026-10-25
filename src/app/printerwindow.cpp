#include "printerwindow.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "contrast.h"
#include "emulator.h"

using tuxape::Dmp2000;

namespace {

constexpr double kNeedle = 1.0 / 72.0;
// A needle's dot: 0.3 mm across, a little less than a needle's step.
constexpr double kDotRadius = 0.0062;
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

void SheetView::dot(double x, double y)
{
    QPainter painter(&picture_);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    QColor ink = kInk;
    ink.setAlphaF(0.82);
    painter.setBrush(ink);
    const double r = kDotRadius * kDotsPerInch;
    painter.drawEllipse(QPointF(x * kDotsPerInch, y * kDotsPerInch), r, r);
}

void SheetView::strike(const Dmp2000::Strike& strike)
{
    QRectF touched;
    for (int pin = 0; pin < 9; ++pin) {
        if (!(strike.pins & (0x100 >> pin)))
            continue;
        const double y = strike.y + pin * kNeedle;
        dot(strike.x, y);
        touched |= QRectF(strike.x - 0.02, y - 0.02, 0.04, 0.04);
    }
    if (!touched.isEmpty()) {
        const double s = scale() * kDotsPerInch;
        update(QRectF(touched.x() * s, touched.y() * s, touched.width() * s, touched.height() * s).toAlignedRect().adjusted(-2, -2, 2, 2));
    }
}

void SheetView::setHead(double x, double y, bool shown)
{
    if (shown == headShown_ && std::fabs(x - headX_) < 1e-4 && std::fabs(y - headY_) < 1e-4)
        return;
    headX_ = x;
    headY_ = y;
    headShown_ = shown;
    update();
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
    painter.setRenderHint(QPainter::Antialiasing);
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
        // The head: its nine needles' height, a little wider than a
        // character, over the line it prints.
        const QRectF head((headX_ - 0.06) * inch, (headY_ - 0.03) * inch, 0.12 * inch, (9 * kNeedle + 0.06) * inch);
        QColor body(0x4A, 0x4D, 0x55);
        body.setAlphaF(0.55);
        painter.setPen(Qt::NoPen);
        painter.setBrush(body);
        painter.drawRoundedRect(head, 2, 2);
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

void PrinterWindow::poll()
{
    uint64_t now = 0;
    std::vector<Dmp2000::Event> events = emulator_->takeDmp2000Events(&now);
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
        PrintedSheet& s = sheet(event.strike.sheet);
        s.strikes.push_back(event.strike);
        if (shown_ >= 0 && sheets_[static_cast<size_t>(shown_)].number == event.strike.sheet)
            view_->strike(event.strike);
        striking_ = true;
        lastStrike_ = event.microsecond;
        break;
    }
    case Kind::Printed:
        sheet(event.printed.sheet).text.push_back(event.printed);
        break;
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
        sheet(event.sheet);
        if (following_ || shown_ < 0) {
            showSheet(static_cast<int>(sheets_.size()) - 1);
            following_ = true;
        }
        // A new sheet is at its top: the paper's motion was the last one's.
        paperKnown_ = false;
        break;
    }
    case Kind::SheetDone:
        sheet(event.sheet).done = true;
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
}
