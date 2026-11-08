#include "programfolder.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "core/version.h"

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

// What the user's folder gets from the program's: each file it does not
// have yet, nothing replaced; and, the first time only, the folders, empty
// ones included (the Library's categories). After that the folders are the
// user's: renamed or removed, they are not made again.
void addMissing(const QString& from, const QString& to, bool folders)
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
    if (!folders)
        return;
    QDirIterator all(from, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (all.hasNext())
        QDir().mkpath(to + '/' + QDir(from).relativeFilePath(all.next()));
}

QString makeUserFolder()
{
    const QString program = programFolder();
    if (QFileInfo(program).isWritable())
        return program;
    const QString home = QDir::homePath() + "/TuxAPE";
    fillUserFolder(program, home, QString::fromLatin1(tuxape::versionString()));
    return home;
}

}  // namespace

void fillUserFolder(const QString& program, const QString& home, const QString& version)
{
    // Done when TuxAPE is installed: the first time it runs, and once more
    // with each new version (the files it brings), not at every start.
    const QString mark = home + "/.tuxape-version";
    const bool first = !QFileInfo::exists(home);
    QFile done(mark);
    if (!first && done.open(QIODevice::ReadOnly) && QString::fromUtf8(done.readAll()).trimmed() == version)
        return;
    done.close();
    for (const char* folder : {"library", "assembly", "img"})
        if (QDir(program).exists(folder))
            addMissing(program + '/' + folder, home + '/' + folder, first);
    if (first)
        for (const char* folder : {"dsk", "savestates", "profile", "roms", "plugins"})
            QDir().mkpath(home + '/' + folder);
    if (const QString readme = program + "/plugins/README.md"; QFileInfo::exists(readme))
        if (QDir(home + "/plugins").exists() && !QFileInfo::exists(home + "/plugins/README.md"))
            QFile::copy(readme, home + "/plugins/README.md");
    if (done.open(QIODevice::WriteOnly | QIODevice::Truncate))
        done.write(version.toUtf8() + '\n');
}

QString userFolder()
{
    static const QString folder = makeUserFolder();
    return folder;
}
