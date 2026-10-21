#include "filedialogs.h"

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// Each file window, as it is shown, given the places it does not have yet.
class PlacesFilter : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show)
            if (auto* dialog = qobject_cast<QFileDialog*>(watched)) {
                QList<QUrl> urls = dialog->sidebarUrls();
                for (const QUrl& url : fileDialogPlaces())
                    if (!urls.contains(url))
                        urls << url;
                dialog->setSidebarUrls(urls);
            }
        return false;
    }
};

}  // namespace

QList<QUrl> fileDialogPlaces()
{
    QList<QUrl> places;
    const auto add = [&places](const QString& folder) {
        const QUrl url = QUrl::fromLocalFile(QFileInfo(folder).absoluteFilePath());
        if (!folder.isEmpty() && QFileInfo(folder).isDir() && !places.contains(url))
            places << url;
    };
    for (const auto location : {QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation,
                                QStandardPaths::PicturesLocation, QStandardPaths::MusicLocation,
                                QStandardPaths::DownloadLocation, QStandardPaths::MoviesLocation})
        // Without its own folder, a place is the home folder, which Qt has.
        if (const QString folder = QStandardPaths::writableLocation(location);
            QFileInfo(folder) != QFileInfo(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)))
            add(folder);
    // A line of the bookmarks: the folder's address, then the name the user
    // gave it, if any.
    QFile bookmarks(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/gtk-3.0/bookmarks");
    if (bookmarks.open(QIODevice::ReadOnly | QIODevice::Text))
        while (!bookmarks.atEnd()) {
            const QString line = QString::fromUtf8(bookmarks.readLine()).trimmed();
            const QUrl url(line.section(' ', 0, 0), QUrl::StrictMode);
            if (url.isLocalFile())
                add(url.toLocalFile());
        }
    return places;
}

void useOwnFileDialogs()
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    // Watching the application's events costs a comparison an event.
    qApp->installEventFilter(new PlacesFilter(qApp));
}
