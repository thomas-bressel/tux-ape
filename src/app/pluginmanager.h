#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>

#include "plugin.h"

class QPluginLoader;

// A plugin as its plugin.json describes it, and what has become of it here.
struct PluginInfo {
    QString id;             // the name of its folder: small letters, digits, '-' and '_'
    QString name;
    QString version;
    QString author;
    QString licence;
    QString description;
    int api = 0;            // the version of plugin.h it was built against
    QStringList platforms;  // those it has a library for ("linux-x86_64"...)
    QString library;        // the one for this program, from the plugin's folder; empty if it has none
    QString folder;         // where it is installed; empty for one still in its archive
    bool enabled = true;    // the user's choice
    bool loaded = false;
    QString problem;        // why it cannot be loaded, when that is not the user's choice
};

// A plugin's host for as long as it is loaded: what the plugin added to
// the window goes with it.
class PluginSession : public tuxape::PluginHost {
public:
    virtual ~PluginSession() = default;
};

// The plugins of the plugins folder: found, loaded, installed from the
// .zip archives they come in, and removed.
//
// A plugin is a folder there, named by its id, that holds a plugin.json and
// a shared library for each kind of machine it runs on:
//
//   plugins/tuxasc/plugin.json
//   plugins/tuxasc/linux-x86_64/libtuxasc.so
//
// An archive holds that folder, or what is in it. Put in the plugins folder
// by hand, a plugin is found the next time TuxAPE starts.
class PluginManager : public QObject {
    Q_OBJECT

public:
    // Who gives a plugin its host. Without one nothing is loaded: plugins
    // are only listed, installed and removed, which is all a test of the
    // Setup window's page needs.
    using SessionMaker = std::function<std::unique_ptr<PluginSession>(const QString& id)>;

    explicit PluginManager(SessionMaker maker = nullptr, QObject* parent = nullptr);
    ~PluginManager() override;

    // Where plugins are kept: the folder TUXAPE_PLUGIN_DIR names, or else
    // the "plugins" folder beside the program, or else the one of the
    // source tree. If there is none yet, the first of them that can be
    // made is, when a plugin is installed.
    static QString folder();
    // What this program runs on, as plugins name it: "linux-x86_64".
    static QString platform();

    // A plugin's description, from the text of its plugin.json. Nothing,
    // with the reason, if the text is not one. A description that reads
    // but cannot be used here comes back with its `problem` said.
    static std::optional<PluginInfo> parse(const QByteArray& json, QString* error = nullptr);
    // The same, for the plugin an archive holds.
    static std::optional<PluginInfo> inspect(const QString& archive, QString* error = nullptr);

    // The plugins of the folder, in the order of their names; scan() reads
    // the folder again.
    const std::vector<PluginInfo>& plugins() const { return plugins_; }
    const PluginInfo* plugin(const QString& id) const;
    void scan();
    // The object a loaded plugin's library made, for the other plugins to
    // call; null if that plugin is not loaded.
    QObject* instance(const QString& id) const;

    // The plugins the user would rather not have loaded, by their ids.
    void setDisabled(const QStringList& ids);
    QStringList disabled() const { return disabled_; }

    // Loads the plugins that are not disabled. From then on what is
    // installed, enabled, disabled or removed takes effect at once.
    void start();
    bool started() const { return started_; }
    // Stops and unloads them all: before the window they talk to goes.
    void stopAll();

    // Unpacks a plugin into the folder, in place of the one of the same id
    // if there is one, and loads it. False, with the reason, if the archive
    // does not hold a plugin that can be used here, or cannot be unpacked.
    bool install(const QString& archive, QString* error = nullptr);
    // Unloads a plugin and deletes its folder.
    bool remove(const QString& id, QString* error = nullptr);

signals:
    // The list, or what it says of a plugin, is no longer what it was.
    void changed();

private:
    struct Loaded {
        std::unique_ptr<QPluginLoader> loader;
        tuxape::Plugin* plugin = nullptr;
        std::unique_ptr<PluginSession> session;
    };

    SessionMaker maker_;
    std::vector<PluginInfo> plugins_;
    std::map<QString, Loaded> loaded_;
    QStringList disabled_;
    bool started_ = false;

    PluginInfo* find(const QString& id);
    void load(PluginInfo& info);
    void unload(const QString& id);
    // Loads and unloads until what is loaded is what should be.
    void settle();
};
