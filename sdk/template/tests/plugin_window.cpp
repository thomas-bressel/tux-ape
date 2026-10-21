// The plugin as TuxAPE finds it: its library loaded with QPluginLoader and
// taken only if it is a tuxape::Plugin, started with a host of the test's
// own, its entry chosen and its window shown, then stopped and unloaded.
// Then once more in French, as TuxAPE starts it when its windows are in
// French. Runs without a display (QT_QPA_PLATFORM=offscreen).

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPluginLoader>
#include <QPushButton>
#include <QTemporaryDir>

#include "check.h"
#include "host.h"

namespace {

// One load of the plugin, in the language the host speaks: what its
// window says of the screen, and the text of its entry.
struct Run {
    QString entry;
    QString title;
    QString screen;
};

Run runOnce(const QString& language, const QString& folder)
{
    Run run;
    Host host(folder);
    host.speaks = language;
    host.shown.mode = 2;
    host.shown.address = 0xC000;

    QPluginLoader loader(QStringLiteral(HELLO_PLUGIN));
    QObject* instance = loader.instance();
    CHECK(instance != nullptr);
    if (!instance) {
        std::printf("%s\n", qPrintable(loader.errorString()));
        return run;
    }
    // The interface TuxAPE asks of a plugin.
    CHECK(loader.metaData().value("IID").toString() == QStringLiteral("org.tuxape.Plugin/1"));
    auto* plugin = qobject_cast<tuxape::Plugin*>(instance);
    CHECK(plugin != nullptr);
    if (!plugin)
        return run;

    QString error;
    CHECK(plugin->start(&host, &error));
    // One entry, without a key of its own, and a button with its picture.
    CHECK(host.actions.size() == 1 && host.actions[0]->shortcut().isEmpty());
    CHECK_EQ(host.pictures, 1);
    // The look changed: the picture again.
    QApplication::setPalette(QPalette(language == "en" ? Qt::darkGray : Qt::lightGray));
    CHECK_EQ(host.pictures, 2);
    if (host.actions.isEmpty())
        return run;
    run.entry = host.actions[0]->text();

    // Chosen: the window, which says what the screen is and types.
    host.actions[0]->trigger();
    auto* window = host.frame.findChild<QWidget*>("HelloWindow");
    CHECK(window && window->isVisible());
    auto* screen = window ? window->findChild<QLabel*>("lScreen") : nullptr;
    run.title = window ? window->windowTitle() : QString();
    run.screen = screen ? screen->text() : QString();
    auto* type = window ? window->findChild<QPushButton*>("bType") : nullptr;
    if (type)
        type->click();
    CHECK(host.typed == QStringList{"PRINT \"HELLO\"\n"});
    // What it keeps is in the folder TuxAPE gives it.
    auto* note = window ? window->findChild<QPushButton*>("bNote") : nullptr;
    if (note)
        note->click();
    CHECK(QFile::exists(folder + "/note.txt"));

    // Stopped: its window goes, and so does its watch on the look and its
    // translation; the library can be unloaded.
    plugin->stop();
    CHECK(host.frame.findChild<QWidget*>("HelloWindow") == nullptr);
    QApplication::setPalette(QPalette(Qt::darkCyan));
    CHECK_EQ(host.pictures, 2);
    CHECK(QCoreApplication::translate("hello::HelloWindow", "&Type HELLO") == "&Type HELLO");
    CHECK(loader.unload());
    return run;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;

    // In English, the language of the sources.
    const Run english = runOnce("en", folder.path());
    CHECK(english.entry == QString::fromUtf8("Hello – &Example Plugin..."));
    CHECK(english.title.startsWith("Hello"));
    CHECK(english.screen == "Mode 2 (2 colours), from &C000");

    // In French: TuxAPE's language, the plugin's own translation.
    const Run french = runOnce("fr", folder.path());
    CHECK(french.entry == QString::fromUtf8("Hello – Plugin d'&exemple..."));
    CHECK(french.title == QString::fromUtf8("Hello – Plugin d'exemple"));
    CHECK(french.screen == QString::fromUtf8("Mode 2 (2 couleurs), à partir de &C000"));

    // A language the plugin has no translation for: English.
    const Run other = runOnce("xx", folder.path());
    CHECK(other.screen == english.screen);
    return checkSummary("plugin_window");
}
