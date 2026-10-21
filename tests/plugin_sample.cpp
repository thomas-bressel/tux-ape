// A plugin for gui_plugins to install: an entry in the Plugins menu and a
// button for it. Chosen, it leaves on TuxAPE's window, as properties, how
// often it was and what it read of the machine. It has an entry in Find
// Graphics' menu too, which leaves there the tiles it was chosen for. Told
// a file's name, it puts the file on the disc in drive A: and leaves there
// what it then sees of the drives, and of the plugins loaded. It refuses to
// start when the window says it must, which is how a plugin that fails is
// tried.

#include <QAction>
#include <QObject>
#include <QVariant>
#include <QWidget>

#include "plugin.h"

class SamplePlugin : public QObject, public tuxape::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.tuxape.Plugin/1")
    Q_INTERFACES(tuxape::Plugin)

public:
    bool start(tuxape::PluginHost* host, QString* error) override
    {
        if (host->window()->property("sampleRefuses").toBool()) {
            *error = "it was told not to";
            return false;
        }
        host_ = host;
        QAction* action = host->addAction("&Sample Plugin...", QKeySequence(Qt::CTRL | Qt::Key_M));
        host->addButton(action, QIcon());
        connect(action, &QAction::triggered, this, [this] {
            QWidget* window = host_->window();
            // Given a file's name, it puts the file on the disc in A:, and
            // does no more.
            if (const QString name = window->property("sampleFile").toString(); !name.isEmpty()) {
                window->setProperty("samplePut", host_->putFile(0, name, window->property("sampleContent").toByteArray()));
                const tuxape::PluginDisc disc = host_->disc(0);
                window->setProperty("sampleDisc", QString("%1 %2 %3 %4")
                                                      .arg(int(disc.present))
                                                      .arg(int(disc.writable))
                                                      .arg(disc.name, disc.files.join(',')));
                window->setProperty("sampleFree", disc.freeBytes);
                window->setProperty("sampleDrives", host_->drives());
                window->setProperty("sampleNoDrive", host_->disc(host_->drives()).present);
                window->setProperty("sampleItself", host_->plugin("sample") == this);
                window->setProperty("sampleNobody", host_->plugin("nobody") == nullptr);
                return;
            }
            // Given a source, it has it assembled and run, and does no more.
            if (const QString source = window->property("sampleSource").toString(); !source.isEmpty()) {
                window->setProperty("sampleRan", host_->assembleAndRun(source));
                return;
            }
            const tuxape::PluginScreen screen = host_->screen();
            window->setProperty("sampleChosen", window->property("sampleChosen").toInt() + 1);
            window->setProperty("sampleMode", screen.mode);
            window->setProperty("sampleInks", static_cast<int>(screen.inks.size()));
            window->setProperty("sampleAddress", screen.address);
            window->setProperty("sampleStart", screen.crtcStart);
            window->setProperty("sampleBytes", static_cast<int>(host_->readMemory(screen.address, 0x4000).size()));
            window->setProperty("sampleFolder", host_->dataFolder());
        });
        if (QAction* tiles = host->addTilesAction("To the &Sample...")) {
            connect(tiles, &QAction::triggered, this, [this] {
                const tuxape::PluginTiles selected = host_->selectedTiles();
                QWidget* window = host_->window();
                window->setProperty("sampleTiles", selected.count);
                window->setProperty("sampleTileBytes", selected.bytes);
                window->setProperty("sampleTileShape", QString("%1x%2 at %3 in mode %4")
                                                           .arg(selected.width)
                                                           .arg(selected.height)
                                                           .arg(selected.address, 4, 16, QLatin1Char('0'))
                                                           .arg(selected.mode));
            });
        }
        // The language TuxAPE speaks, for the plugin to speak it too.
        host->window()->setProperty("sampleLanguage", host->language());
        host->window()->setProperty("sampleStarted", true);
        return true;
    }

    void stop() override
    {
        host_->window()->setProperty("sampleStarted", false);
        host_ = nullptr;
    }

private:
    tuxape::PluginHost* host_ = nullptr;
};

#include "plugin_sample.moc"
