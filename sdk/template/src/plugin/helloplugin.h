#pragma once

#include <QObject>
#include <QPointer>

#include "plugin.h"

class HelloWindow;

// What TuxAPE loads: an entry in the Plugins menu and a button on the
// control panel, which show the plugin's window.
class HelloPlugin : public QObject, public tuxape::Plugin {
    Q_OBJECT
    // The interface TuxAPE knows a plugin by: exactly this one.
    Q_PLUGIN_METADATA(IID "org.tuxape.Plugin/1")
    Q_INTERFACES(tuxape::Plugin)

public:
    bool start(tuxape::PluginHost* host, QString* error) override;
    void stop() override;

protected:
    // The look of the windows has changed, light or dark: the button's
    // picture is drawn again for it.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    tuxape::PluginHost* host_ = nullptr;
    QAction* action_ = nullptr;  // TuxAPE's: it takes it back at stop()
    QPointer<HelloWindow> window_;

    void showWindow();
};
