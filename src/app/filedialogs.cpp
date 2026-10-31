#include "filedialogs.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include "core/inifile.h"
#include "settings.h"

namespace {

constexpr const char* kFolders = "Folders";

QString foldersFile()
{
    return QFileInfo(Settings::file()).absolutePath() + QStringLiteral("/FileFolders.ini");
}

// The kind of file a window is for: the first pattern it offers, in lower
// case, which is the same in any language and for loading and saving. One
// that offers none goes by its title.
QString kindOf(const QFileDialog* dialog)
{
    static const QRegularExpression inBrackets(QStringLiteral("\\(([^)]*)\\)"));
    for (const QString& filter : dialog->nameFilters()) {
        const QRegularExpressionMatch found = inBrackets.match(filter);
        for (const QString& pattern : (found.hasMatch() ? found.captured(1) : filter).split(' ', Qt::SkipEmptyParts))
            if (pattern != QLatin1String("*") && pattern != QLatin1String("*.*"))
                return pattern.toLower();
    }
    return dialog->windowTitle();
}

QString lastFolder(const QString& kind)
{
    tuxape::IniFile ini;
    ini.load(foldersFile().toStdString());
    return QString::fromStdString(ini.get(kFolders, kind.toStdString()));
}

void keepFolder(const QString& kind, const QString& chosen)
{
    const QFileInfo file(chosen);
    const QString folder = file.isDir() ? file.absoluteFilePath() : file.absolutePath();
    tuxape::IniFile ini;
    ini.load(foldersFile().toStdString());
    ini.set(kFolders, kind.toStdString(), folder.toStdString());
    QDir().mkpath(QFileInfo(foldersFile()).absolutePath());
    ini.save(foldersFile().toStdString());
}

// Each file window, as it is shown, given the places it does not have yet,
// and sent where its kind of file was last; what is chosen there is kept. A
// name it was given for a file to save stays.
class FileDialogFilter : public QObject {
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

                const QString kind = kindOf(dialog);
                if (const QString folder = lastFolder(kind); !folder.isEmpty() && QFileInfo(folder).isDir())
                    dialog->setDirectory(folder);
                if (!dialog->property("tuxapeKeepsFolder").toBool()) {
                    dialog->setProperty("tuxapeKeepsFolder", true);
                    connect(dialog, &QFileDialog::filesSelected, this, [dialog](const QStringList& files) {
                        if (!files.isEmpty())
                            keepFolder(kindOf(dialog), files.first());
                    });
                }
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
    qApp->installEventFilter(new FileDialogFilter(qApp));
}
