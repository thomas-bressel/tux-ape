// Disc handling through the application: creating a blank image, saving to
// it from BASIC, writing it back to its file, reading it in the other
// drive, and the flip, swap and remove operations.
// Runs without a display (QT_QPA_PLATFORM=offscreen).
//
// With TUXAPE_TEST_GRAB_DIR set, pictures of the dialogs are saved there.

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "check.h"
#include "core/screen_text.h"
#include "discdialogs.h"
#include "discmanager.h"
#include "emulator.h"
#include "mainwindow.h"
#include "core/cpc.h"
#include "settings.h"
#include "screenwidget.h"

namespace {

std::string screenText(Emulator& emulator)
{
    return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); });
}

bool waitForText(Emulator& emulator, const char* what, int ms = 15000)
{
    return QTest::qWaitFor([&] { return screenText(emulator).find(what) != std::string::npos; }, ms);
}

void grab(QWidget& widget, const char* name)
{
    const QByteArray dir = qgetenv("TUXAPE_TEST_GRAB_DIR");
    if (!dir.isEmpty())
        widget.grab().save(QString::fromLocal8Bit(dir) + QLatin1Char('/') + QLatin1String(name));
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; skipping\n");
        return 77;
    }
    MainWindow window(&emulator);
    window.show();
    DiscManager* discs = window.discs();
    emulator.setSpeedPercent(1000);
    emulator.start();
    CHECK(waitForText(emulator, "Ready"));

    QTemporaryDir folder;
    const QString imagePath = folder.filePath("work.dsk");

    // A new blank disc exists as a file straight away.
    CHECK(!discs->info(0).present);
    CHECK(discs->createBlank(0, imagePath, tuxape::defaultDiscFormat()).isEmpty());
    CHECK(QFile::exists(imagePath));
    DiscManager::Info info = discs->info(0);
    CHECK(info.present);
    CHECK(!info.modified);
    CHECK(info.description.contains("DATA (SS 40)"));
    const qint64 blankSize = QFile(imagePath).size();
    CHECK_EQ(blankSize, 0x100 + 40 * (0x100 + 9 * 512));

    // Save a program on it from BASIC; the drive light comes on meanwhile.
    emulator.autoType("10 REM on disc\nSAVE\"PROG\"\n");
    bool lit = false;
    CHECK(QTest::qWaitFor(
        [&] {
            lit = lit || discs->activity().activeDrive == 0;
            return discs->info(0).modified;
        },
        15000));
    CHECK(lit);
    CHECK(QTest::qWaitFor([&] { return screenText(emulator).find("PROG\"\nReady") != std::string::npos; }, 15000));

    // Writing it back changes the file but not its size.
    QFile before(imagePath);
    CHECK(before.open(QIODevice::ReadOnly));
    const QByteArray blankBytes = before.readAll();
    before.close();
    CHECK(discs->save(0).isEmpty());
    CHECK(!discs->info(0).modified);
    QFile after(imagePath);
    CHECK(after.open(QIODevice::ReadOnly));
    const QByteArray savedBytes = after.readAll();
    CHECK_EQ(savedBytes.size(), blankBytes.size());
    CHECK(savedBytes != blankBytes);
    CHECK(savedBytes.contains("PROG    BAS"));

    // The same file read in drive B: shows the program.
    CHECK(discs->insert(1, imagePath).isEmpty());
    emulator.autoType("CLS:|B:CAT\n");
    CHECK(waitForText(emulator, "PROG    .BAS"));
    CHECK(waitForText(emulator, "Drive B: user  0"));

    // Something that is not a disc image is refused and changes nothing.
    const QString bogus = folder.filePath("bogus.dsk");
    QFile junk(bogus);
    CHECK(junk.open(QIODevice::WriteOnly));
    junk.write(QByteArray(4000, 'x'));
    junk.close();
    CHECK(!discs->insert(1, bogus).isEmpty());
    CHECK(discs->info(1).path == imagePath);
    CHECK(!discs->insert(1, folder.filePath("missing.dsk")).isEmpty());

    // Flip puts the spare (empty at first) in the drive, and back.
    discs->flip(0);
    CHECK(!discs->info(0).present);
    discs->flip(0);
    CHECK(discs->info(0).path == imagePath);

    // Swap, then remove.
    discs->remove(1);
    discs->swap();
    CHECK(!discs->info(0).present);
    CHECK(discs->info(1).path == imagePath);
    discs->remove(1);
    CHECK(!discs->info(1).present);

    // A read-only image is write protected unless temporary writes are on.
    QFile::setPermissions(imagePath, QFileDevice::ReadOwner);
    CHECK(discs->insert(0, imagePath).isEmpty());
    CHECK(discs->info(0).readOnly);
    CHECK(emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.fdc().drive(0).disc->writeProtected; }));
    discs->setAllowTemporaryWrites(true);
    CHECK(!emulator.withMachine([](tuxape::Cpc& cpc) { return cpc.fdc().drive(0).disc->writeProtected; }));

    DriveSetupDialog setup(discs, &window);
    setup.show();
    QTest::qWait(50);
    grab(setup, "drive_setup.png");
    FormatDialog format(&window);
    format.show();
    QTest::qWait(50);
    grab(format, "format.png");
    CHECK(format.format().firstSectorId == 0xC1);
    grab(window, "main.png");

    // The drive's light on the picture: there only when asked for, in the
    // top right corner while a drive's motor runs, with its letter and the
    // cylinder its head is on.
    {
        ScreenWidget* screen = window.screen();
        emulator.withMachine([](tuxape::Cpc& cpc) { cpc.out(0xFA7E, 1); });
        CHECK(QTest::qWaitFor([&] { return emulator.driveLight() >= 0; }, 3000));
        CHECK(emulator.driveLight() >> 8 <= 1);  // the drive last used, A: or B:
        QTest::qWait(50);
        CHECK(screen->driveLightRect().isEmpty());
        Settings settings = window.settings();
        CHECK(!settings.driveLed && !settings.showDriveCylinders);
        settings.driveLed = true;
        window.applySettings(settings);
        const QRect small = screen->driveLightRect();
        CHECK(!small.isEmpty() && small.left() > screen->width() * 3 / 4 && small.bottom() < screen->height() / 4);
        settings.showDriveCylinders = true;
        window.applySettings(settings);
        const QRect light = screen->driveLightRect();
        CHECK(light.width() > small.width());
        const QImage picture = screen->grab().toImage();
        CHECK(picture.pixelColor(light.left() + 1, light.top() + 1) == QColor(255, 32, 32));
        grab(window, "drive_light.png");
        emulator.withMachine([](tuxape::Cpc& cpc) { cpc.out(0xFA7E, 0); });
        CHECK(QTest::qWaitFor([&] { return emulator.driveLight() < 0; }, 3000));
        QTest::qWait(50);
        CHECK(screen->driveLightRect().isEmpty());
        // The two choices are kept with the settings.
        CHECK(settings.save());
        Settings saved;
        saved.load();
        CHECK(saved.driveLed && saved.showDriveCylinders);
    }

    if (g_failures)
        std::printf("screen was:\n%s\n", screenText(emulator).c_str());
    emulator.stop();
    return checkSummary("gui_discs");
}
