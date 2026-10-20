#pragma once

#include <QDialog>

#include "plugin.h"

class QLabel;

// The plugin's window: what the CPC's screen is, a button that types on its
// keyboard, and the folder where the plugin keeps what it has to keep.
class HelloWindow : public QDialog {
    Q_OBJECT

public:
    HelloWindow(tuxape::PluginHost* host, QWidget* parent = nullptr);

    void refresh();
    QString screenText() const;

private:
    tuxape::PluginHost* host_;
    QLabel* screen_;
};
