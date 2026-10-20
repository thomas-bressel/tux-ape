#include "hellowindow.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "greeting.h"

HelloWindow::HelloWindow(tuxape::PluginHost* host, QWidget* parent)
    : QDialog(parent)
    , host_(host)
{
    // The windows of a plugin are named after it.
    setWindowTitle(tr("Hello – Example Plugin"));
    setObjectName("HelloWindow");
    auto* layout = new QVBoxLayout(this);
    screen_ = new QLabel;
    screen_->setObjectName("lScreen");
    layout->addWidget(screen_);
    auto* type = new QPushButton(tr("&Type HELLO"));
    type->setObjectName("bType");
    connect(type, &QPushButton::clicked, this, [this] { host_->autoType(QStringLiteral("PRINT \"HELLO\"\n")); });
    layout->addWidget(type);
    // What the plugin keeps goes into the folder TuxAPE gives it, never into
    // its own folder (part of its signature) nor anywhere else.
    auto* where = new QLabel(tr("Kept in: %1").arg(QDir::toNativeSeparators(host_->dataFolder())));
    where->setObjectName("lFolder");
    where->setWordWrap(true);
    layout->addWidget(where);
    QFile seen(host_->dataFolder() + "/seen.txt");
    if (seen.open(QIODevice::WriteOnly))
        seen.write("The window was opened.\n");
}

void HelloWindow::refresh()
{
    const tuxape::PluginScreen screen = host_->screen();
    screen_->setText(QString::fromStdString(hello::describeScreen(screen.mode, screen.address)));
}

QString HelloWindow::screenText() const
{
    return screen_->text();
}
