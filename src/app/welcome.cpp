#include "welcome.h"

#include <utility>

#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSplashScreen>
#include <QTimer>

// The picture is in the program itself; a library's pictures are only
// there once asked for.
static void needPictures()
{
    Q_INIT_RESOURCE(tuxape);
}

QPixmap welcomePicture()
{
    needPictures();
    return QPixmap(QStringLiteral(":/welcome.png"));
}

namespace {

// Tells, once, that the picture has gone: hidden by a click, or its time up.
class Leaving : public QObject {
public:
    Leaving(QObject* parent, std::function<void()> gone)
        : QObject(parent)
        , gone_(std::move(gone))
    {
    }

    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Hide && gone_)
            std::exchange(gone_, {})();
        return false;
    }

private:
    std::function<void()> gone_;
};

}  // namespace

QSplashScreen* showWelcome(std::function<void()> gone, int milliseconds)
{
    QPixmap picture = welcomePicture();
    if (picture.isNull()) {
        if (gone)
            gone();
        return nullptr;
    }
    const QScreen* screen = QGuiApplication::primaryScreen();
    const int most = screen ? screen->availableGeometry().height() / 2 : 480;
    if (picture.height() > most)
        picture = picture.scaledToHeight(most, Qt::SmoothTransformation);
    auto* splash = new QSplashScreen(picture, Qt::WindowStaysOnTopHint);
    splash->setObjectName("Welcome");
    splash->installEventFilter(new Leaving(splash, std::move(gone)));
    splash->show();
    // A click hides it; when its time is up it goes for good either way.
    QTimer::singleShot(milliseconds, splash, [splash] {
        splash->hide();
        splash->deleteLater();
    });
    return splash;
}
