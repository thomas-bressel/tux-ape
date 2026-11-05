#include "programfolder.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

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

namespace {

// What the user's folder starts with, from the program's: each file it
// does not have yet, nothing replaced.
void addMissing(const QString& from, const QString& to)
{
    QDir().mkpath(to);
    QDirIterator files(from, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString file = files.next();
        const QString target = to + '/' + QDir(from).relativeFilePath(file);
        if (QFileInfo::exists(target))
            continue;
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::copy(file, target);
        // Copied from where only the system writes, it is the user's now.
        QFile::setPermissions(target, QFile::permissions(target) | QFile::WriteOwner | QFile::WriteUser);
    }
    // The folders too, empty ones included (the Library's categories).
    QDirIterator folders(from, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (folders.hasNext())
        QDir().mkpath(to + '/' + QDir(from).relativeFilePath(folders.next()));
}

QString makeUserFolder()
{
    const QString program = programFolder();
    if (QFileInfo(program).isWritable())
        return program;
    const QString home = QDir::homePath() + "/TuxAPE";
    for (const char* folder : {"library", "assembly", "img"})
        if (QDir(program).exists(folder))
            addMissing(program + '/' + folder, home + '/' + folder);
    for (const char* folder : {"dsk", "savestates", "profile", "roms", "plugins"})
        QDir().mkpath(home + '/' + folder);
    if (const QString readme = program + "/plugins/README.md"; QFileInfo::exists(readme))
        if (!QFileInfo::exists(home + "/plugins/README.md"))
            QFile::copy(readme, home + "/plugins/README.md");
    return home;
}

}  // namespace

QString userFolder()
{
    static const QString folder = makeUserFolder();
    return folder;
}
