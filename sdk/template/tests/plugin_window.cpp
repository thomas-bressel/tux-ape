// The plugin as TuxAPE finds it: its library loaded with QPluginLoader and
// taken only if it is a tuxape::Plugin, started with a host of the test's
// own, its entry chosen and its window shown, then stopped and unloaded.
// Runs without a display (QT_QPA_PLATFORM=offscreen).

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPluginLoader>
#include <QPushButton>
#include <QTemporaryDir>

#include "check.h"
#include "host.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Host host(folder.path());
    host.shown.mode = 2;
    host.shown.address = 0xC000;

    QPluginLoader loader(QStringLiteral(HELLO_PLUGIN));
    QObject* instance = loader.instance();
    CHECK(instance != nullptr);
    if (!instance) {
        std::printf("%s\n", qPrintable(loader.errorString()));
        return checkSummary("plugin_window");
    }
    // The interface TuxAPE asks of a plugin.
    CHECK(loader.metaData().value("IID").toString() == QStringLiteral("org.tuxape.Plugin/1"));
    auto* plugin = qobject_cast<tuxape::Plugin*>(instance);
    CHECK(plugin != nullptr);
    if (!plugin)
        return checkSummary("plugin_window");

    QString error;
    CHECK(plugin->start(&host, &error));
    // One entry, without a key of its own, and a button with its picture.
    CHECK(host.actions.size() == 1 && host.actions[0]->shortcut().isEmpty());
    CHECK(host.actions.size() == 1 && host.actions[0]->text().startsWith("Hello"));
    CHECK_EQ(host.pictures, 1);
    // The look changed: the picture again.
    QApplication::setPalette(QPalette(Qt::darkGray));
    CHECK_EQ(host.pictures, 2);

    // Chosen: the window, which says what the screen is and types.
    if (!host.actions.isEmpty())
        host.actions[0]->trigger();
    auto* window = host.frame.findChild<QWidget*>("HelloWindow");
    CHECK(window && window->isVisible());
    auto* screen = window ? window->findChild<QLabel*>("lScreen") : nullptr;
    CHECK(screen && screen->text() == "Mode 2 (2 colours), from &C000");
    auto* type = window ? window->findChild<QPushButton*>("bType") : nullptr;
    if (type)
        type->click();
    CHECK(host.typed == QStringList{"PRINT \"HELLO\"\n"});
    // What it keeps is in the folder TuxAPE gives it.
    CHECK(QFile::exists(folder.filePath("seen.txt")));

    // Stopped: its window goes, and the library can be unloaded.
    plugin->stop();
    CHECK(host.frame.findChild<QWidget*>("HelloWindow") == nullptr);
    CHECK(loader.unload());
    return checkSummary("plugin_window");
}
