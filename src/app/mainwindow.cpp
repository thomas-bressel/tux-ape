#include "mainwindow.h"

#include <QApplication>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("TuxAPE %1").arg(QApplication::applicationVersion()));
    resize(768, 540);
}
