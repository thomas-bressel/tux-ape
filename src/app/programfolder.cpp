#include "programfolder.h"

#include <QCoreApplication>
#include <QDir>

QString programFolder()
{
    const QString program = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    // TuxAPE.app/Contents/MacOS: three folders up.
    QDir folder(program);
    if (folder.dirName() == "MacOS" && folder.cdUp() && folder.dirName() == "Contents" && folder.cdUp()
        && folder.dirName().endsWith(".app") && folder.cdUp())
        return folder.absolutePath();
#endif
    return program;
}
