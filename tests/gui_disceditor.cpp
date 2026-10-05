// The disc editor: the files of the disc in a drive, copied in from the
// host and out to it, renamed, deleted, hidden; and its sectors, changed
// byte by byte. Runs without a display (QT_QPA_PLATFORM=offscreen); no ROM
// is needed.
//
//   gui_disceditor [prefix]   also saves pictures of the two pages as
//                             <prefix>files.png and <prefix>sectors.png

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include "check.h"
#include "core/cpc.h"
#include "core/disc.h"
#include "core/discfiles.h"
#include "core/setup.h"
#include "disceditordialog.h"
#include "discmanager.h"
#include "emulator.h"
#include "mainwindow.h"
#include "settings.h"

namespace {

void writeHost(const QString& path, const QByteArray& data)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QByteArray readHost(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("?");
}

QAction* actionNamed(QWidget& window, const char* text, int which = 0)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text) && which-- == 0)
            return action;
    return nullptr;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    Emulator emulator;
    emulator.setupMachine(tuxape::stockMachine(tuxape::CpcModel::Cpc6128), true);
    MainWindow window(&emulator);
    window.show();

    // Without a disc there is nothing to edit.
    QAction* editA = actionNamed(window, "Edit Disc...", 0);
    QAction* editB = actionNamed(window, "Edit Disc...", 1);
    CHECK(editA && editB && !editA->isEnabled() && !editB->isEnabled());
    CHECK(window.discs()->createBlank(0, folder.filePath("work.dsk"), tuxape::defaultDiscFormat()).isEmpty());
    CHECK(editA && editA->isEnabled() && editA->shortcut() == QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_F1));
    CHECK(editB && !editB->isEnabled());

    // Files of the host: a text, a program that is not text, and one whose
    // name a disc cannot hold as it is.
    const QByteArray text = "10 PRINT \"hello\"\r\n20 GOTO 10\r\n";
    QByteArray code;
    for (int i = 0; i < 3000; ++i)
        code.append(static_cast<char>(i * 5 + 1));
    const QString textPath = folder.filePath("readme.txt"), codePath = folder.filePath("code.bin");
    const QString longPath = folder.filePath("A long name here.basic");
    writeHost(textPath, text);
    writeHost(codePath, code);
    writeHost(longPath, text);

    {
        DiscEditorDialog editor(&emulator, 0);
        editor.show();
        CHECK(editor.listedFiles().isEmpty());
        CHECK(editor.freeText() == "DATA (SS 40) - 178K free");

        // In: a text goes as it is, a program gets AMSDOS's header.
        QString error;
        CHECK(editor.addFile(textPath, &error));
        CHECK(editor.addFile(codePath, &error));
        CHECK(editor.addFile(longPath, &error));
        CHECK(editor.listedFiles() == (QStringList{"README.TXT", "CODE.BIN", "ALONGN.BAS"}));
        CHECK(editor.freeText() == "DATA (SS 40) - 172K free");  // 1K, 4K (the header counts) and 1K
        const auto onDisc = emulator.withMachine([](tuxape::Cpc& cpc) {
            tuxape::DiscFiles files(*cpc.fdc().drive(0).disc);
            const auto list = files.list();
            return std::make_pair(files.read(list[0]), files.read(list[1]));
        });
        CHECK(onDisc.first && !tuxape::amsdosHeader(*onDisc.first));
        const auto header = onDisc.second ? tuxape::amsdosHeader(*onDisc.second) : std::nullopt;
        CHECK(header && header->type == 2 && header->length == 3000);
        CHECK(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.fdc().drive(0).disc->modified; }));

        // Out again: what went in, to the byte, the header taken off.
        const QString out = folder.filePath("out");
        QDir().mkpath(out);
        CHECK(editor.extractFile("README.TXT", out + "/readme.txt", &error));
        CHECK(editor.extractFile("code.bin", out + "/code.bin", &error));
        CHECK(readHost(out + "/readme.txt") == text);
        CHECK(readHost(out + "/code.bin") == code);
        // With the option off, files travel as the disc has them.
        editor.setHeaders(false);
        CHECK(editor.extractFile("CODE.BIN", out + "/code.raw", &error));
        const QByteArray raw = readHost(out + "/code.raw");
        CHECK_EQ(raw.size(), 128 + 3000);
        CHECK(raw.mid(128) == code);
        CHECK(editor.addFile(codePath, &error));  // in place of the one there, without a header
        CHECK(editor.extractFile("CODE.BIN", out + "/code.pad", &error));
        CHECK_EQ(readHost(out + "/code.pad").size(), 3072);  // whole records: nothing says where it ends
        CHECK(readHost(out + "/code.pad").startsWith(code));
        editor.setHeaders(true);
        CHECK(!editor.extractFile("NOSUCH.BIN", out + "/x", &error));
        CHECK(error == "NOSUCH.BIN is not on the disc.");

        // Renamed, hidden, shown, deleted.
        CHECK(editor.renameFile("ALONGN.BAS", "prog.bas"));
        CHECK(!editor.renameFile("PROG.BAS", "README.TXT"));
        CHECK(!editor.renameFile("PROG.BAS", "no good name"));
        CHECK(editor.setFileAttributes("PROG.BAS", true, true));
        CHECK(editor.listedFiles() == (QStringList{"README.TXT", "CODE.BIN"}));
        editor.setShowSystem(true);
        CHECK(editor.listedFiles() == (QStringList{"README.TXT", "CODE.BIN", "PROG.BAS"}));
        CHECK(editor.findChild<QTreeWidget*>("lvFiles")->topLevelItem(2)->text(2) == "Read-only, System");
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            editor.grab().save(prefix + "files.png");
        }
        CHECK(editor.deleteFile("PROG.BAS"));
        CHECK(!editor.deleteFile("PROG.BAS"));
        CHECK(editor.listedFiles() == (QStringList{"README.TXT", "CODE.BIN"}));

        // No room, no name: a word for the user, and the disc as it was.
        const QString hugePath = folder.filePath("huge.bin");
        writeHost(hugePath, QByteArray(200 * 1024, 'x'));
        CHECK(!editor.addFile(hugePath, &error));
        CHECK(error == "There is no room on the disc for huge.bin.");
        writeHost(folder.filePath("what?.txt"), text);
        CHECK(!editor.addFile(folder.filePath("what?.txt"), &error));
        CHECK(!editor.addFile(folder.filePath("missing.txt"), &error));
        CHECK(editor.listedFiles() == (QStringList{"README.TXT", "CODE.BIN"}));

        // ---- The Sector Editor: the first track's sectors, in the order
        // they come round, and the directory in the first of them.
        editor.findChild<QTabWidget*>("PageControl")->setCurrentIndex(1);
        CHECK(editor.selectSector(0, 0, 0));
        CHECK(editor.sectorNames() == (QStringList{"C1", "C6", "C2", "C7", "C3", "C8", "C4", "C9", "C5"}));
        CHECK(editor.sectorInfo() == "(00 00 C1 02)  512 bytes");
        MemoryDumpView* view = editor.sectorView();
        CHECK(view->lineText(0).startsWith("0000 00 52 45 41 44 4D 45 20 20 54 58 54"));
        CHECK(view->lineText(0).endsWith("README  TXT...."));
        CHECK(view->lineText(31).startsWith("01F0"));
        CHECK(view->lineText(32).isEmpty());  // a sector is 512 bytes
        if (!prefix.isEmpty()) {
            QTest::qWait(50);
            editor.grab().save(prefix + "sectors.png");
        }
        // A byte changed there is changed on the disc: here a file's name.
        view->setCursor(1);
        view->typeDigit(0x5);
        view->typeDigit(0x3);
        CHECK(view->lineText(0).startsWith("0000 00 53 45 41"));
        CHECK(editor.listedFiles() == (QStringList{"SEADME.TXT", "CODE.BIN"}));
        // Other sectors, other tracks.
        CHECK(editor.selectSector(0, 0, 1));
        CHECK(editor.sectorInfo() == "(00 00 C6 02)  512 bytes");
        CHECK(editor.selectSector(39, 0, 8));
        CHECK(editor.sectorInfo() == "(27 00 C5 02)  512 bytes");
        CHECK(view->lineText(0).startsWith("0000 E5 E5 E5"));
        CHECK(!editor.selectSector(40, 0, 0));
        CHECK(!editor.selectSector(0, 0, 9));
        CHECK(!editor.selectSector(0, 1, 0));
    }

    // A disc from a file that cannot be written: looked at, not changed.
    window.discs()->save(0);
    QFile::setPermissions(folder.filePath("work.dsk"), QFile::ReadOwner);
    CHECK(window.insertDiscFile(0, folder.filePath("work.dsk")));
    {
        DiscEditorDialog editor(&emulator, 0);
        CHECK(editor.listedFiles() == (QStringList{"SEADME.TXT", "CODE.BIN"}));
        QString error;
        CHECK(!editor.addFile(textPath, &error));
        CHECK(error == "The disc cannot be written to.");
        CHECK(!editor.deleteFile("CODE.BIN"));
        CHECK(!editor.findChild<QPushButton*>("bAdd")->isEnabled());
        CHECK(!editor.findChild<QPushButton*>("bDelete")->isEnabled());
        CHECK(editor.findChild<QPushButton*>("bExtract")->isEnabled());
        CHECK(editor.extractFile("SEADME.TXT", folder.filePath("again.txt"), &error));
        CHECK(readHost(folder.filePath("again.txt")) == text);
    }
    QFile::setPermissions(folder.filePath("work.dsk"), QFile::ReadOwner | QFile::WriteOwner);

    // From the menu.
    bool opened = false;
    QTimer::singleShot(0, [&] {
        if (auto* editor = qobject_cast<DiscEditorDialog*>(QApplication::activeModalWidget())) {
            opened = editor->listedFiles().size() == 2;
            editor->accept();
        }
    });
    if (editA)
        editA->trigger();
    CHECK(opened);

    return checkSummary("gui_disceditor");
}
