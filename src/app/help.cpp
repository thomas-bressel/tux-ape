#include "help.h"

#include <utility>

#include <QAbstractButton>

namespace {

std::function<void()>& handler()
{
    static std::function<void()> show;
    return show;
}

}  // namespace

void setHelpHandler(std::function<void()> show)
{
    handler() = std::move(show);
}

void requestHelp()
{
    if (handler())
        handler()();
}

void wireHelp(QAbstractButton* button)
{
    button->setEnabled(true);
    button->setToolTip(QString());
    QObject::connect(button, &QAbstractButton::clicked, button, [] { requestHelp(); });
}
