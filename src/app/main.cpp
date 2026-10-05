#include <QApplication>

#include "core/version.h"
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("TuxAPE");
    QApplication::setApplicationVersion(tuxape::versionString());
    QApplication::setOrganizationName("TuxAPE");

    MainWindow window;
    window.show();

    return app.exec();
}
