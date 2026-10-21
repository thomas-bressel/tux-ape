#include "helloplugin.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPixmap>

#include "hellowindow.h"

namespace hello {

namespace {

// The button's picture, in the colours of the look in use.
QIcon helloIcon()
{
    QPixmap picture(32, 32);
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QApplication::palette().color(QPalette::ButtonText), 3));
    painter.drawEllipse(QRectF(4, 4, 24, 24));
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(16);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, 32, 32), Qt::AlignCenter, QStringLiteral("H"));
    return QIcon(picture);
}

}  // namespace

bool HelloPlugin::start(tuxape::PluginHost* host, QString*)
{
    host_ = host;
    // The plugin's texts in TuxAPE's language, before its first tr(): the
    // translations tuxape_add_plugin(TRANSLATIONS ...) built into the
    // library. For English, the language of the sources, its plurals
    // alone; English too for a language the plugin has no translation for.
    const QString translations = QStringLiteral(":/i18n/hello/hello_%1.qm");
    if (translator_.load(translations.arg(host->language())) || translator_.load(translations.arg("en")))
        QCoreApplication::installTranslator(&translator_);
    // No key of its own: every key that is not a function key is the
    // CPC's (see PLUGINS.md).
    action_ = host->addAction(tr("Hello – &Example Plugin..."));
    host->addButton(action_, helloIcon());
    connect(action_, &QAction::triggered, this, &HelloPlugin::showWindow);
    // Qt tells the application when the look changes: its events are
    // watched for that one, which costs a comparison an event.
    qApp->installEventFilter(this);
    return true;
}

void HelloPlugin::stop()
{
    // TuxAPE unloads the library next: nothing of it may be left running,
    // nor its translator installed.
    qApp->removeEventFilter(this);
    delete window_;
    QCoreApplication::removeTranslator(&translator_);
    action_ = nullptr;
    host_ = nullptr;
}

bool HelloPlugin::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange && host_ && action_)
        host_->addButton(action_, helloIcon());
    return false;
}

void HelloPlugin::showWindow()
{
    if (!window_)
        window_ = new HelloWindow(host_, host_->window());
    window_->refresh();
    window_->show();
    window_->raise();
    window_->activateWindow();
}

}  // namespace hello
