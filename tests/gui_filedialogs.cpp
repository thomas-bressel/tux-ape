// The file windows: Qt's own once useOwnFileDialogs() is called, for a
// window made by hand and for one a static function makes, as plugins do;
// each with the user's folders and GNOME's bookmarks on its side, those
// that are folders here. Runs without a display (QT_QPA_PLATFORM=offscreen),
// with a configuration folder of its own: what Qt's file windows keep goes
// there, not to the user's.

#include <functional>
#include <memory>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>

#include "check.h"
#include "core/inifile.h"
#include "filedialogs.h"
#include "settings.h"

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
    // Never the user's own settings.
    Settings::setFile(home.filePath("settings/TuxAPE.ini"));
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

    // Each window opens where its kind of file was last taken from or put.
    const QString first = home.filePath("first"), second = home.filePath("second"), gone = home.filePath("gone later");
    for (const QString& folder : {first, second, gone})
        CHECK(QDir().mkpath(folder));
    const auto touch = [](const QString& file) {
        QFile out(file);
        return out.open(QIODevice::WriteOnly);
    };
    CHECK(touch(first + "/game.cpr") && touch(second + "/other.cpr") && touch(gone + "/old.cpr") && touch(second + "/disc.dsk"));
    // What a window does, and where it was as it came up.
    const auto when = [](const std::function<void(QFileDialog*)>& act) {
        auto where = std::make_shared<QString>();
        QTimer::singleShot(100, [=] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            CHECK(dialog != nullptr);
            if (dialog) {
                *where = dialog->directory().absolutePath();
                act(dialog);
            }
            // One that will not go is a failure, not a test that never ends.
            QTimer::singleShot(300, [left = QPointer<QFileDialog>(dialog)] {
                if (left) {
                    std::printf("still up: %s, in %s, selected %s\n", qPrintable(left->windowTitle()),
                                qPrintable(left->directory().absolutePath()), qPrintable(left->selectedFiles().join(' ')));
                    CHECK(false);
                    left->reject();
                }
            });
        });
        return where;
    };
    // Typed in, as someone does: with the keyboard on that field Qt takes
    // no notice of selectFile().
    const auto type = [](QFileDialog* dialog, const QString& file) {
        auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
        CHECK(name != nullptr);
        if (name)
            name->setText(file);
        static_cast<QDialog*>(dialog)->accept();
    };
    const auto choose = [&type](const QString& file) { return [&type, file](QFileDialog* dialog) { type(dialog, file); }; };
    const auto cancel = [](QFileDialog* dialog) { dialog->reject(); };
    const QString cartridges = "Cartridges (*.cpr);;All files (*)";

    // Nothing kept yet: where it was told to open. Then, whatever it is told,
    // where the last file was: another window of the kind too, in another
    // language.
    auto where = when(choose(first + "/game.cpr"));
    CHECK(QFileDialog::getOpenFileName(nullptr, "Load Cartridge", second, cartridges) == first + "/game.cpr");
    CHECK(*where == second);
    where = when(cancel);
    CHECK(QFileDialog::getOpenFileName(nullptr, "Cartouche", second, "Cartouches (*.CPR);;Tous les fichiers (*)").isEmpty());
    CHECK(*where == first);
    // Another kind has its own.
    where = when(cancel);
    CHECK(QFileDialog::getOpenFileName(nullptr, "Insert Disc", second, "Discs (*.dsk *.hfe)").isEmpty());
    CHECK(*where == second);
    // Going elsewhere and giving up changes nothing.
    where = when([&](QFileDialog* dialog) {
        dialog->setDirectory(second);
        dialog->reject();
    });
    CHECK(QFileDialog::getOpenFileName(nullptr, "Load Cartridge", QString(), cartridges).isEmpty());
    where = when(cancel);
    CHECK(QFileDialog::getOpenFileName(nullptr, "Load Cartridge", QString(), cartridges).isEmpty());
    CHECK(*where == first);
    // Saving counts, and a name given for the file stays.
    where = when([](QFileDialog* dialog) { static_cast<QDialog*>(dialog)->accept(); });
    CHECK(QFileDialog::getSaveFileName(nullptr, "Save", second + "/print.pdf", "PDF (*.pdf)") == second + "/print.pdf");
    where = when([](QFileDialog* dialog) { static_cast<QDialog*>(dialog)->accept(); });
    CHECK(QFileDialog::getSaveFileName(nullptr, "Save", first + "/new.pdf", "PDF (*.pdf)") == second + "/new.pdf");
    CHECK(*where == second);
    // A window with no kind of file goes by its title; a folder chosen is
    // that folder.
    where = when(choose(second));
    CHECK(QFileDialog::getExistingDirectory(nullptr, "Extract To", first) == second);
    where = when(cancel);
    CHECK(QFileDialog::getExistingDirectory(nullptr, "Extract To", first).isEmpty());
    CHECK(*where == second);
    // A window made by hand, as the plugins may.
    {
        QFileDialog dialog(nullptr, "By Hand", second, cartridges);
        dialog.show();
        CHECK(dialog.directory().absolutePath() == first);
        dialog.reject();
    }
    // A folder that has gone: where it was told.
    where = when(choose(gone + "/old.cpr"));
    CHECK(QFileDialog::getOpenFileName(nullptr, "Load Cartridge", second, cartridges) == gone + "/old.cpr");
    CHECK(QFile::remove(gone + "/old.cpr") && QDir().rmdir(gone));
    where = when(cancel);
    CHECK(QFileDialog::getOpenFileName(nullptr, "Load Cartridge", second, cartridges).isEmpty());
    CHECK(*where == second);
    // Kept for the next run, beside the settings.
    tuxape::IniFile kept;
    CHECK(kept.load((QFileInfo(Settings::file()).absolutePath() + "/FileFolders.ini").toStdString()));
    CHECK(kept.get("Folders", "*.pdf") == second.toStdString());
    CHECK(kept.get("Folders", "Extract To") == second.toStdString());
    return checkSummary("gui_filedialogs");
}
