#include "welcome.h"

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

QSplashScreen* showWelcome(int milliseconds)
{
    QPixmap picture = welcomePicture();
    if (picture.isNull())
        return nullptr;
    const QScreen* screen = QGuiApplication::primaryScreen();
    const int most = screen ? screen->availableGeometry().height() / 2 : 480;
    if (picture.height() > most)
        picture = picture.scaledToHeight(most, Qt::SmoothTransformation);
    auto* splash = new QSplashScreen(picture, Qt::WindowStaysOnTopHint);
    splash->setObjectName("Welcome");
    splash->show();
    // A click hides it; when its time is up it goes for good either way.
    QTimer::singleShot(milliseconds, splash, &QObject::deleteLater);
    return splash;
}
