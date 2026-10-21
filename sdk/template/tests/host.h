#pragma once

#include <QAction>
#include <QMainWindow>
#include <QStringList>

#include "plugin.h"

// A TuxAPE of the tests' own: every function of PluginHost, of every
// version, noting what the plugin asks of it. A plugin's tests run with it,
// without TuxAPE; a test sets what TuxAPE would answer.
class Host : public tuxape::PluginHost {
public:
    explicit Host(const QString& folder) : folder_(folder) {}

    QMainWindow frame;
    QList<QAction*> actions;
    QList<QAction*> tilesActions;
    int pictures = 0;      // the buttons' pictures given, again when the look changes
    QStringList typed;     // what was typed on the CPC's keyboard
    tuxape::PluginScreen shown;
    tuxape::PluginTiles tiles;
    QList<tuxape::PluginDisc> discs{{}, {}};
    QStringList putFiles;  // "A:NAME" for each file put on a disc
    QString speaks = QStringLiteral("en");

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

    // ---- Version 2 ----
    QAction* addTilesAction(const QString& text) override
    {
        auto* action = new QAction(text, &frame);
        tilesActions << action;
        return action;
    }
    tuxape::PluginTiles selectedTiles() override { return tiles; }

    // ---- Version 3 ----
    int drives() override { return static_cast<int>(discs.size()); }
    tuxape::PluginDisc disc(int drive) override { return discs.value(drive); }
    QString putFile(int drive, const QString& name, const QByteArray&) override
    {
        if (!discs.value(drive).present)
            return QStringLiteral("There is no disc in that drive.");
        putFiles << QString("%1:%2").arg(QChar('A' + drive)).arg(name);
        return {};
    }
    QObject* plugin(const QString&) override { return nullptr; }

    // ---- Version 4 ----
    QString language() override { return speaks; }

private:
    QString folder_;
};
