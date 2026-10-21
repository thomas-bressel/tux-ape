// The file windows: Qt's own once useOwnFileDialogs() is called, for a
// window made by hand and for one a static function makes, as plugins do;
// each with the user's folders and GNOME's bookmarks on its side, those
// that are folders here. Runs without a display (QT_QPA_PLATFORM=offscreen),
// with a configuration folder of its own: what Qt's file windows keep goes
// there, not to the user's.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QTimer>

#include "check.h"
#include "filedialogs.h"

int main(int argc, char* argv[])
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(home.filePath("config")));
    QDir(home.path()).mkpath("config/gtk-3.0");
    QDir(home.path()).mkpath("games/airwolf reloaded");
    {
        // A bookmark with a name, one without, one gone, one elsewhere.
        QFile bookmarks(home.filePath("config/gtk-3.0/bookmarks"));
        CHECK(bookmarks.open(QIODevice::WriteOnly));
        bookmarks.write(QUrl::fromLocalFile(home.filePath("games")).toEncoded() + " Games\n");
        bookmarks.write(QUrl::fromLocalFile(home.filePath("games/airwolf reloaded")).toEncoded() + "\n");
        bookmarks.write(QUrl::fromLocalFile(home.filePath("gone")).toEncoded() + " Gone\n");
        bookmarks.write("sftp://example.org/home Away\n");
    }
    QApplication app(argc, argv);
    const QUrl games = QUrl::fromLocalFile(home.filePath("games"));
    const QUrl airwolf = QUrl::fromLocalFile(home.filePath("games/airwolf reloaded"));

    CHECK(!QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
    useOwnFileDialogs();
    CHECK(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs));

    // The bookmarks that are folders here, in their order.
    const QList<QUrl> places = fileDialogPlaces();
    CHECK(places.contains(games) && places.contains(airwolf));
    CHECK(places.indexOf(games) < places.indexOf(airwolf));
    CHECK(!places.contains(QUrl::fromLocalFile(home.filePath("gone"))));
    CHECK(!places.contains(QUrl("sftp://example.org/home")));

    // A window made by hand: Qt's, with Qt's places, then these.
    {
        QFileDialog dialog(nullptr, "Choose", home.path());
        dialog.show();
        const QList<QUrl> side = dialog.sidebarUrls();
        CHECK(side.contains(games) && side.contains(airwolf));
        CHECK(side.contains(QUrl::fromLocalFile(QDir::homePath())));
        // Shown again, nothing twice.
        dialog.hide();
        dialog.show();
        CHECK(dialog.sidebarUrls().count(games) == 1);
        dialog.reject();
    }

    // As a plugin asks for a file: the static function's window has them
    // too, and it is Qt's: a widget shown, not a hidden one standing for
    // the system's.
    bool seen = false;
    QTimer::singleShot(200, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        CHECK(dialog != nullptr);
        if (dialog) {
            seen = dialog->isVisible() && dialog->sidebarUrls().contains(airwolf);
            dialog->reject();
        }
    });
    const QString path = QFileDialog::getOpenFileName(nullptr, "YM Song", home.path(), "YM songs (*.ym *.YM);;All files (*)");
    CHECK(path.isEmpty() && seen);
    return checkSummary("gui_filedialogs");
}
