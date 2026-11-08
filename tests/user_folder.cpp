// The user's TuxAPE folder, as the package's program fills it: everything
// the first time, the new files of a new version, and nothing at a start
// like any other. The Library's category folders, once renamed or removed
// by the user, are not made again (they were, at every start, until 1.1.2).

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "check.h"
#include "programfolder.h"

namespace {

void write(const QString& path, const QByteArray& content = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(content);
}

}  // namespace

int main()
{
    QTemporaryDir temp;
    const QString program = temp.path() + "/opt";
    const QString home = temp.path() + "/TuxAPE";
    QDir().mkpath(program + "/library/Games/Action");
    write(program + "/library/Utilities/xmem-install.dsk");
    write(program + "/assembly/hello.asm");

    // Installed: all of it.
    fillUserFolder(program, home, "1.1.2");
    CHECK(QDir(home + "/library/Games/Action").exists());
    CHECK(QFileInfo::exists(home + "/library/Utilities/xmem-install.dsk"));
    CHECK(QFileInfo::exists(home + "/assembly/hello.asm"));
    CHECK(QDir(home + "/dsk").exists() && QDir(home + "/savestates").exists() && QDir(home + "/plugins").exists());

    // The user renames a category and removes a file: the next starts
    // leave that alone.
    QDir().rename(home + "/library/Games/Action", home + "/library/Games/action");
    QFile::remove(home + "/assembly/hello.asm");
    fillUserFolder(program, home, "1.1.2");
    fillUserFolder(program, home, "1.1.2");
    CHECK(!QDir(home + "/library/Games/Action").exists());
    CHECK(QDir(home + "/library/Games/action").exists());
    CHECK(!QFileInfo::exists(home + "/assembly/hello.asm"));

    // A new version brings a new file: it comes, the folder made again for
    // nothing, a file the user changed not replaced.
    write(program + "/library/Demos/new.dsk");
    write(home + "/library/Utilities/xmem-install.dsk", "mine");
    fillUserFolder(program, home, "1.2");
    CHECK(QFileInfo::exists(home + "/library/Demos/new.dsk"));
    CHECK(!QDir(home + "/library/Games/Action").exists());
    QFile mine(home + "/library/Utilities/xmem-install.dsk");
    CHECK(mine.open(QIODevice::ReadOnly) && mine.readAll() == "mine");
    return checkSummary("user_folder");
}
