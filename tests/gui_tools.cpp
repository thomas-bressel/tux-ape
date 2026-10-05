// The small windows of the File menu: Save Screenshot and Auto-Type. Runs
// without a display (QT_QPA_PLATFORM=offscreen).
//
//   gui_tools [prefix]   also saves pictures of the two windows as
//                        <prefix>screenshot.png and <prefix>autotype.png,
//                        and the film it records as <prefix>film.avi

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include "check.h"
#include "core/inifile.h"
#include "core/screen_text.h"
#include "emulator.h"
#include "mainwindow.h"
#include "screenwidget.h"
#include "settings.h"
#include "tooldialogs.h"

namespace {

QAction* actionNamed(MainWindow& window, const char* text)
{
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text().remove(QLatin1Char('&')) == QLatin1String(text))
            return action;
    return nullptr;
}

// A picture with runs of one colour, single pixels, and bytes of every
// kind, the ones PCX has to escape among them.
QImage testPicture(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int block = (x / 7 + y / 3) % 5;
            image.setPixel(x, y, block == 0   ? qRgb(0xFF, 0xC0, 0x00)
                                 : block == 1 ? qRgb(x * 5 & 0xFF, y * 9 & 0xFF, (x + y) & 0xFF)
                                 : block == 2 ? qRgb(0x00, 0x00, 0x80)
                                 : block == 3 ? qRgb(0xC1, 0xFE, 0xD0)
                                              : qRgb(y & 0xFF, 0x7F, 0xFF));
        }
    return image;
}

int word(const QByteArray& data, int at)
{
    return static_cast<unsigned char>(data[at]) | static_cast<unsigned char>(data[at + 1]) << 8;
}

// Readers for the two formats written by hand, as their specifications
// have them.
QImage readTarga(const QByteArray& data)
{
    if (data.size() < 18 || data[2] != 2 || data[16] != 24 || data[17] != 0x20)
        return {};
    const int width = word(data, 12), height = word(data, 14);
    if (data.size() != 18 + width * height * 3)
        return {};
    QImage image(width, height, QImage::Format_RGB32);
    int at = 18;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x, at += 3)
            image.setPixel(x, y, qRgb(static_cast<unsigned char>(data[at + 2]), static_cast<unsigned char>(data[at + 1]),
                                      static_cast<unsigned char>(data[at])));
    return image;
}

QImage readPcx(const QByteArray& data)
{
    if (data.size() < 128 || data[0] != 0x0A || data[1] != 5 || data[2] != 1 || data[3] != 8 || data[65] != 3)
        return {};
    const int width = word(data, 8) + 1, height = word(data, 10) + 1, bytesPerLine = word(data, 66);
    QImage image(width, height, QImage::Format_RGB32);
    int at = 128;
    QByteArray line(bytesPerLine * 3, '\0');
    for (int y = 0; y < height; ++y) {
        for (int filled = 0; filled < line.size();) {
            if (at >= data.size())
                return {};
            const unsigned char byte = static_cast<unsigned char>(data[at++]);
            int run = 1;
            unsigned char value = byte;
            if ((byte & 0xC0) == 0xC0) {
                run = byte & 0x3F;
                if (at >= data.size())
                    return {};
                value = static_cast<unsigned char>(data[at++]);
            }
            for (int i = 0; i < run && filled < line.size(); ++i)
                line[filled++] = static_cast<char>(value);
        }
        for (int x = 0; x < width; ++x)
            image.setPixel(x, y, qRgb(static_cast<unsigned char>(line[x]),
                                      static_cast<unsigned char>(line[bytesPerLine + x]),
                                      static_cast<unsigned char>(line[2 * bytesPerLine + x])));
    }
    return at == data.size() ? image : QImage();
}

void testScreenshots(const QString& folder, const QString& picture)
{
    const QImage screen = testPicture(768, 540);
    ScreenshotDialog dialog(screen);
    dialog.show();
    auto* halfSize = dialog.findChild<QCheckBox*>("ckHalfSize");
    auto* halfHeight = dialog.findChild<QCheckBox*>("ckHalfHeight");
    auto* preview = dialog.findChild<QLabel*>("Image1");
    CHECK(halfSize && halfHeight && preview);
    if (!(halfSize && halfHeight && preview))
        return;
    CHECK(halfSize->text() == "Half Size" && halfHeight->text() == "Half Height");

    // Full size: the picture as it is.
    CHECK(!dialog.halfSize() && !dialog.halfHeight());
    CHECK(dialog.result() == screen);
    CHECK(!preview->pixmap(Qt::ReturnByValue).isNull());

    // Half height keeps every other line, as they are.
    halfHeight->setChecked(true);
    QImage result = dialog.result();
    CHECK(result.size() == QSize(768, 270));
    CHECK(result.pixel(10, 100) == screen.pixel(10, 200));
    CHECK(result.pixel(700, 269) == screen.pixel(700, 538));
    // Half size blends the pixels of each pair.
    halfSize->setChecked(true);
    result = dialog.result();
    CHECK(result.size() == QSize(384, 270));
    halfHeight->setChecked(false);
    CHECK(dialog.result().size() == QSize(384, 540));
    dialog.setHalfSize(false);
    dialog.setHalfHeight(true);
    CHECK(!halfSize->isChecked() && halfHeight->isChecked());

    // The kinds of file, and each one read back the same.
    const QString filter = ScreenshotDialog::fileFilter();
    for (const char* kind : {"*.png", "*.bmp", "*.pcx", "*.tga"})
        CHECK(filter.contains(kind));
    const QImage small = testPicture(37, 11);  // an odd width: PCX pads its lines
    CHECK(ScreenshotDialog::save(small, folder + "/shot.png"));
    CHECK(QImage(folder + "/shot.png").convertToFormat(QImage::Format_RGB32) == small);
    CHECK(ScreenshotDialog::save(small, folder + "/shot.bmp"));
    CHECK(QImage(folder + "/shot.bmp").convertToFormat(QImage::Format_RGB32) == small);
    CHECK(ScreenshotDialog::save(small, folder + "/no ending"));
    CHECK(QImage(folder + "/no ending", "png").convertToFormat(QImage::Format_RGB32) == small);
    auto contents = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    CHECK(ScreenshotDialog::save(small, folder + "/shot.tga"));
    CHECK(readTarga(contents(folder + "/shot.tga")) == small);
    CHECK(ScreenshotDialog::save(small, folder + "/shot.PCX"));
    CHECK(readPcx(contents(folder + "/shot.PCX")) == small);
    CHECK(ScreenshotDialog::save(screen, folder + "/full.pcx"));
    CHECK(readPcx(contents(folder + "/full.pcx")) == screen);
    CHECK(!ScreenshotDialog::save(small, folder + "/no such folder/shot.png"));
    CHECK(!ScreenshotDialog::save(small, folder + "/no such folder/shot.tga"));

    // The choices are kept, under WinAPE's names.
    Settings settings;
    settings.screenshotHalfHeight = true;
    settings.screenshotFolder = folder;
    CHECK(settings.save());
    tuxape::IniFile ini;
    CHECK(ini.load(Settings::file().toStdString()));
    CHECK(ini.get("Screenshots", "Half Size") == "false");
    CHECK(ini.get("Screenshots", "Half Height") == "true");
    CHECK(QString::fromStdString(ini.get("Screenshots", "Path")) == folder);
    Settings again;
    again.load();
    CHECK(again == settings);
    QFile::remove(Settings::file());

    if (!picture.isEmpty()) {
        QApplication::processEvents();
        CHECK(dialog.grab().save(picture));
    }
}

void testAutoType(const QString& folder, const QString& picture)
{
    const QString script = QStringLiteral("~F1~~DEL~~PAUSE 50~\nrun \"disc\"\n");
    AutoTypeDialog dialog(script);
    dialog.show();
    auto* edit = dialog.findChild<QPlainTextEdit*>("mText");
    CHECK(edit && dialog.findChild<QPushButton*>("bLoad") && dialog.findChild<QPushButton*>("bSave"));
    CHECK(dialog.text() == script);
    if (edit) {
        edit->setPlainText("10 PRINT \"HELLO\"\n");
        CHECK(dialog.text() == "10 PRINT \"HELLO\"\n");
    }
    const QString path = folder + "/typed.txt";
    CHECK(dialog.saveText(path));
    dialog.setText(QString());
    CHECK(dialog.text().isEmpty());
    CHECK(dialog.loadText(path));
    CHECK(dialog.text() == "10 PRINT \"HELLO\"\n");
    CHECK(!dialog.loadText(folder + "/missing.txt"));
    CHECK(dialog.text() == "10 PRINT \"HELLO\"\n");
    // A file from Windows, with its line endings.
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly) && file.write("a\r\nb\r\n") == 6);
    file.close();
    CHECK(dialog.loadText(path));
    CHECK(dialog.text() == "a\nb\n");

    if (!picture.isEmpty()) {
        dialog.setText(script);
        QApplication::processEvents();
        CHECK(dialog.grab().save(picture));
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QString prefix = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    testScreenshots(folder.path(), prefix.isEmpty() ? QString() : prefix + "screenshot.png");
    testAutoType(folder.path(), prefix.isEmpty() ? QString() : prefix + "autotype.png");

    Emulator emulator;
    if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
        std::printf("ROM images not found; the rest is skipped\n");
        return checkSummary("gui_tools");
    }
    MainWindow window(&emulator);
    window.show();
    emulator.setSpeedPercent(1000);
    emulator.start();
    auto screenText = [&] {
        return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); });
    };
    CHECK(QTest::qWaitFor([&] { return screenText().find("Ready") != std::string::npos; }, 15000));

    QAction* screenshot = actionNamed(window, "Save Screenshot...");
    QAction* autoType = actionNamed(window, "Auto Type...");
    CHECK(screenshot && screenshot->isEnabled());
    CHECK(autoType && autoType->isEnabled());

    // What Auto-Type types reaches the machine, pauses and named keys
    // included. The firmware prints "Ready" a moment before it reads the
    // keyboard: typing waits for that.
    QTest::qWait(500);
    emulator.autoType(QStringLiteral("a=6~PAUSE 5~*7:?a~RETURN~"));
    CHECK(QTest::qWaitFor([&] { return screenText().find(" 42") != std::string::npos; }, 15000));

    // The screen as a screenshot: full size, with the firmware's colours.
    QTest::qWait(200);
    const QImage shot = window.screen()->screenshot();
    CHECK(shot.size() == QSize(768, 540));
    int blue = 0;
    for (int y = 100; y < 440; y += 20)
        for (int x = 100; x < 660; x += 20)
            blue += qBlue(shot.pixel(x, y)) > 0x40 && qRed(shot.pixel(x, y)) < 0x40 ? 1 : 0;
    CHECK(blue > 200);  // mostly the blue paper

    // Recordings of the sound: a WAV file and a YM file, each started and
    // ended from its entry of the File menu (here without the file box).
    QAction* recordWav = actionNamed(window, "Record WAV...");
    QAction* recordYm = actionNamed(window, "Record YM...");
    CHECK(recordWav && recordWav->isEnabled() && recordWav->isCheckable() && !recordWav->isChecked());
    CHECK(recordYm && recordYm->isEnabled() && recordYm->isCheckable());
    const QString wavPath = folder.filePath("sound.wav"), ymPath = folder.filePath("tune.ym");
    CHECK(!emulator.recordingWav() && !emulator.recordingYm());
    CHECK(emulator.startWavRecording(wavPath));
    CHECK(emulator.startYmRecording(ymPath));
    CHECK(emulator.recordingWav() && emulator.recordingYm());
    emulator.autoType(QStringLiteral("SOUND 1,200,20,15~RETURN~"));
    QTest::qWait(400);  // some four seconds of the machine's time
    // The entries end what is running.
    recordWav->trigger();
    recordYm->trigger();
    CHECK(!emulator.recordingWav() && !emulator.recordingYm());
    CHECK(!recordWav->isChecked() && !recordYm->isChecked());
    {
        QFile wav(wavPath);
        CHECK(wav.open(QIODevice::ReadOnly));
        const QByteArray data = wav.readAll();
        CHECK(data.startsWith("RIFF") && data.mid(8, 4) == "WAVE");
        // At least a second of sound, and not all of it silence.
        CHECK(data.size() > 44 + 44100 * 4);
        bool heard = false;
        for (qsizetype i = 44; i + 1 < data.size() && !heard; i += 2)
            heard = data[i] != 0 || data[i + 1] != 0;
        CHECK(heard);
        QFile ym(ymPath);
        CHECK(ym.open(QIODevice::ReadOnly));
        const QByteArray tune = ym.readAll();
        CHECK(tune.startsWith("YM5!LeOnArD!") && tune.endsWith("End!"));
        const int frames = static_cast<uint8_t>(tune[14]) << 8 | static_cast<uint8_t>(tune[15]);
        CHECK(frames > 50);
        CHECK(tune.size() > 34 + frames * 16);
    }
    // A film of the screen with its sound: an AVI file of JPEG pictures.
    QAction* recordAvi = actionNamed(window, "Record AVI...");
    CHECK(recordAvi && recordAvi->isEnabled() && recordAvi->isCheckable() && !recordAvi->isChecked());
    const QString aviPath = folder.filePath("film.avi");
    CHECK(emulator.startAviRecording(aviPath));
    CHECK(emulator.recordingAvi());
    emulator.autoType(QStringLiteral("SOUND 1,100,20,15~RETURN~"));
    QTest::qWait(300);
    recordAvi->trigger();
    CHECK(!emulator.recordingAvi() && !recordAvi->isChecked());
    {
        QFile avi(aviPath);
        CHECK(avi.open(QIODevice::ReadOnly));
        const QByteArray data = avi.readAll();
        auto quad = [&](qsizetype at) {
            return static_cast<uint32_t>(static_cast<uint8_t>(data[at]) | static_cast<uint8_t>(data[at + 1]) << 8
                                         | static_cast<uint8_t>(data[at + 2]) << 16)
                   | static_cast<uint32_t>(static_cast<uint8_t>(data[at + 3])) << 24;
        };
        CHECK(data.startsWith("RIFF") && data.mid(8, 8) == "AVI LIST" && data.mid(20, 8) == "hdrlavih");
        CHECK_EQ(quad(4), data.size() - 8);
        CHECK_EQ(quad(32), 312 * 64);  // microseconds to a frame
        const uint32_t frames = quad(48);
        CHECK(frames > 20);
        CHECK_EQ(quad(64), 768);
        CHECK_EQ(quad(68), 540);
        // The first picture, where the list of pictures starts, is a JPEG
        // of the screen at its usual size.
        const qsizetype movi = data.indexOf("movi");
        CHECK(movi > 0 && data.mid(movi + 4, 4) == "00dc");
        const QImage first = QImage::fromData(data.mid(movi + 12, quad(movi + 8)), "JPEG");
        CHECK(first.size() == QSize(768, 540));
        // Sound goes with it, and the index lists every piece.
        CHECK(data.indexOf("01wb", movi) > 0);
        const qsizetype index = data.lastIndexOf("idx1");
        CHECK(index > movi);
        CHECK(quad(index + 4) >= frames * 16);
        CHECK_EQ(index + 8 + quad(index + 4), data.size());
        // The first entry leads to the first picture.
        CHECK(data.mid(index + 8, 4) == "00dc");
        CHECK_EQ(movi + quad(index + 16), movi + 4);
        // As many pictures in the index as the header says.
        uint32_t pictures = 0;
        for (qsizetype at = index + 8; at < data.size(); at += 16)
            pictures += data.mid(at, 4) == "00dc" ? 1 : 0;
        CHECK_EQ(pictures, frames);
    }
    if (!prefix.isEmpty())
        QFile::copy(aviPath, prefix + "film.avi");
    CHECK(!emulator.startAviRecording(folder.filePath("missing/film.avi")));
    CHECK(!emulator.startWavRecording(folder.filePath("missing/sound.wav")));
    CHECK(!emulator.startYmRecording(folder.filePath("missing/tune.ym")));

    emulator.stop();
    return checkSummary("gui_tools");
}
