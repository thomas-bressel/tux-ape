#include "hellowindow.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "greeting.h"

namespace hello {

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
    auto* note = new QPushButton(tr("&Keep a Note"));
    note->setObjectName("bNote");
    connect(note, &QPushButton::clicked, this, &HelloWindow::keepNote);
    layout->addWidget(note);
    auto* where = new QLabel(tr("Kept in: %1").arg(QDir::toNativeSeparators(host_->dataFolder())));
    where->setObjectName("lFolder");
    where->setWordWrap(true);
    layout->addWidget(where);
}

void HelloWindow::refresh()
{
    // The engine gives the data; the window words it, so that it can be
    // translated: whole sentences, %n for what is counted.
    const tuxape::PluginScreen screen = host_->screen();
    const ScreenInfo info = screenInfo(screen.mode, screen.address);
    const QString address = QString("%1").arg(info.address, 4, 16, QLatin1Char('0')).toUpper();
    screen_->setText(info.colours ? tr("Mode %1 (%n colours), from &%2", nullptr, info.colours).arg(info.mode).arg(address)
                                  : tr("Mode %1, which the CPC has not, from &%2").arg(info.mode).arg(address));
}

QString HelloWindow::screenText() const
{
    return screen_->text();
}

// What the plugin keeps goes into the folder TuxAPE gives it, never into its
// own folder (part of its signature) nor anywhere else.
void HelloWindow::keepNote()
{
    QFile note(QDir(host_->dataFolder()).filePath("note.txt"));
    if (note.open(QIODevice::WriteOnly))
        note.write(screenText().toUtf8() + '\n');
}

}  // namespace hello
