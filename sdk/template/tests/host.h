#pragma once

#include <QAction>
#include <QMainWindow>
#include <QStringList>

#include "plugin.h"

// A TuxAPE of the tests' own: every function of PluginHost, noting what the
// plugin asks of it. A plugin's tests run with it, without TuxAPE.
class Host : public tuxape::PluginHost {
public:
    explicit Host(const QString& folder) : folder_(folder) {}

    QMainWindow frame;
    QList<QAction*> actions;
    int pictures = 0;      // the buttons' pictures given, again when the look changes
    QStringList typed;     // what was typed on the CPC's keyboard
    tuxape::PluginScreen shown;

    // ---- Version 1 ----
    QWidget* window() override { return &frame; }
    QAction* addAction(const QString& text, const QKeySequence& shortcut) override
    {
        auto* action = new QAction(text, &frame);
        action->setShortcut(shortcut);
        actions << action;
        return action;
    }
    void addButton(QAction* action, const QIcon& icon) override
    {
        if (actions.contains(action) && !icon.isNull())
            ++pictures;
    }
    bool insertDisc(int, const QString&) override { return false; }
    bool loadSnapshot(const QString&) override { return false; }
    void autoType(const QString& text) override { typed << text; }
    void reset(bool) override {}
    QByteArray readMemory(int, int size) override { return QByteArray(size, '\0'); }
    tuxape::PluginScreen screen() override { return shown; }
    void openInAssembler(const QString&) override {}
    QString dataFolder() override { return folder_; }
    bool assembleAndRun(const QString&) override { return false; }
    // Versions 2 and 3 have answers of their own: a plugin built for
    // version 1 does not call them.

private:
    QString folder_;
};
