// The application's sound path, end to end: the emulated machine plays a
// note, the emulator hands the samples to SDL, and SDL's "disk" audio driver
// writes what would have gone to the sound card into a file, which is then
// checked for the right pitch.
// Runs without a display or a sound card.

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "audiooutput.h"
#include "check.h"
#include "settings.h"
#include "setupdialog.h"
#include "drivesound.h"
#include "core/screen_text.h"
#include "emulator.h"

int main(int argc, char* argv[])
{
    QTemporaryDir folder;
    // The settings this test saves are its own, never the user's.
    Settings::setFile(folder.filePath("TuxAPE.ini"));
    const QByteArray rawPath = folder.filePath("sound.raw").toLocal8Bit();
    setenv("SDL_AUDIODRIVER", "disk", 1);
    setenv("SDL_DISKAUDIOFILE", rawPath.constData(), 1);

    QApplication app(argc, argv);
    const int rate = 44100;
    {
        AudioOutput audio;
        if (!audio.open(rate)) {
            std::printf("SDL's disk audio driver is not available; skipping\n");
            return 77;
        }
        Emulator emulator;
        if (!emulator.setupMachine(tuxape::CpcModel::Cpc6128).isEmpty()) {
            std::printf("ROM images not found; skipping\n");
            return 77;
        }
        emulator.setAudioOutput(&audio);
        emulator.start();
        const auto screen = [&] {
            return emulator.withMachine([](tuxape::Cpc& cpc) { return tuxape::readScreenText(cpc); });
        };
        CHECK(QTest::qWaitFor([&] { return screen().find("Ready") != std::string::npos; }, 10000));
        // BASIC empties the keyboard buffer as it starts; give it a moment.
        QTest::qWait(500);

        // Two seconds of the A above middle C on channel A. Typing the
        // command takes a second and a half on top of that.
        emulator.autoType("SOUND 1,142,200,15\n");
        QTest::qWait(4500);

        // The queue of sound waiting to be played does not pile up. (How
        // low it gets says nothing here: the disk driver empties it faster
        // than a sound card would.)
        CHECK(audio.queue(nullptr, 0) < 0.15);

        // At another speed the sound goes on, its pitch following: the
        // same note at five quarters of the speed is a major third higher
        // and over sooner. (A slider a few per cent off 100 once left the
        // machine silent.)
        emulator.setSpeedPercent(125);
        emulator.autoType("SOUND 1,142,200,15\n");
        QTest::qWait(4000);
        CHECK(audio.queue(nullptr, 0) < 0.15);
        emulator.stop();
    }

    // With TUXAPE_TEST_GRAB_DIR set, keep the capture for inspection.
    if (const QByteArray dir = qgetenv("TUXAPE_TEST_GRAB_DIR"); !dir.isEmpty())
        QFile::copy(QString::fromLocal8Bit(rawPath), QString::fromLocal8Bit(dir) + "/sound.raw");

    QFile raw(QString::fromLocal8Bit(rawPath));
    CHECK(raw.open(QIODevice::ReadOnly));
    const QByteArray bytes = raw.readAll();
    const auto* samples = reinterpret_cast<const int16_t*>(bytes.constData());
    const size_t frames = static_cast<size_t>(bytes.size()) / 4;
    CHECK(frames > static_cast<size_t>(rate) * 5);

    // Find the cycles of the tone on the left channel.
    std::vector<size_t> rises;
    bool high = false;
    for (size_t i = 0; i < frames; ++i) {
        const int16_t left = samples[i * 2];
        if (!high && left > 8000) {
            high = true;
            rises.push_back(i);
        } else if (high && left < 2000) {
            high = false;
        }
        // Channel A is on the left only.
        if (samples[i * 2 + 1] != 0) {
            std::printf("sound on the right channel at frame %zu\n", i);
            ++g_failures;
            break;
        }
    }
    // The two notes, with the silence between them.
    size_t second = rises.size();
    for (size_t i = 1; i < rises.size(); ++i)
        if (rises[i] - rises[i - 1] > static_cast<size_t>(rate) / 10)
            second = i;
    const std::vector<size_t> notes[2] = {{rises.begin(), rises.begin() + static_cast<ptrdiff_t>(second)},
                                          {rises.begin() + static_cast<ptrdiff_t>(second), rises.end()}};
    // Two seconds at 440 Hz, then the same 880 cycles at five quarters of
    // the speed. The disk driver takes sound a few percent faster than a
    // sound card would, so the capture can have a gap or two where the
    // queue ran dry; that costs a cycle at most each time.
    const double speeds[2] = {1.0, 1.25};
    for (int note = 0; note < 2; ++note) {
        const std::vector<size_t>& cycles = notes[note];
        CHECK(cycles.size() > 870 && cycles.size() <= 881);
        if (cycles.size() <= 2)
            continue;
        const double seconds = static_cast<double>(cycles.back() - cycles.front()) / rate;
        CHECK(seconds > 1.95 / speeds[note] && seconds < 2.3 / speeds[note]);
        // The pitch is judged on the typical cycle, which gaps do not affect.
        std::vector<size_t> periods;
        for (size_t i = 1; i < cycles.size(); ++i)
            periods.push_back(cycles[i] - cycles[i - 1]);
        std::nth_element(periods.begin(), periods.begin() + static_cast<ptrdiff_t>(periods.size() / 2), periods.end());
        const double pitch = static_cast<double>(rate) / static_cast<double>(periods[periods.size() / 2]);
        const bool right = pitch > 436 * speeds[note] && pitch < 446 * speeds[note];
        if (!right)
            std::printf("note %d: %zu cycles over %.3f s, typical pitch %.1f Hz\n", note + 1, cycles.size(), seconds, pitch);
        CHECK(right);
    }

    // "Disc Drive Sounds": a whirr while a motor runs, a click for each
    // cylinder a head crosses, made from noise; nothing otherwise.
    {
        const auto loudest = [](const std::vector<int16_t>& sound) {
            int most = 0;
            for (int16_t sample : sound)
                most = std::max(most, std::abs(static_cast<int>(sample)));
            return most;
        };
        const std::vector<int16_t> quiet(4410 * 2, 0);  // a tenth of a second, in stereo
        DriveSound drive;
        std::vector<int16_t> sound = quiet;
        drive.mix(sound, 44100, false, 0);
        CHECK(sound == quiet);
        // The motor: it runs up, then whirrs evenly, well under the
        // machine's own sound; the same in both ears.
        int whirr = 0;
        for (int n = 0; n < 6; ++n) {
            sound = quiet;
            drive.mix(sound, 44100, true, 0);
            whirr = loudest(sound);
        }
        CHECK(whirr > 60 && whirr < 1500);
        bool same = true;
        for (size_t i = 0; i < sound.size(); i += 2)
            same = same && sound[i] == sound[i + 1];
        CHECK(same);
        // The head: three cylinders, three knocks, far louder, and over
        // within a few hundredths of a second.
        sound = quiet;
        drive.mix(sound, 44100, true, 3);
        CHECK(loudest(sound) > 1800);
        int knocks = 0;
        bool loud = false;
        for (size_t i = 0; i < sound.size(); i += 2) {
            const bool now = std::abs(static_cast<int>(sound[i])) > 1700;
            knocks += now && !loud && (i < 400 || std::abs(static_cast<int>(sound[i - 300])) < 1700);
            loud = now;
        }
        CHECK(knocks >= 3 && knocks <= 9);
        sound = quiet;
        drive.mix(sound, 44100, true, 0);
        CHECK(loudest(sound) < 1500);
        // The motor stopped: the whirr dies away.
        for (int n = 0; n < 6; ++n) {
            sound = quiet;
            drive.mix(sound, 44100, false, 0);
        }
        CHECK(sound == quiet);
        // Added to what is there, never wrapping round.
        std::vector<int16_t> full(4410 * 2, 32767);
        drive.mix(full, 44100, true, 5);
        CHECK(*std::min_element(full.begin(), full.end()) > 20000);
        drive.reset();
        sound = quiet;
        drive.mix(sound, 44100, false, 0);
        CHECK(sound == quiet);

        // A setting, with its box on the Sound page; off unless asked for.
        Settings settings;
        CHECK(!settings.discSounds);
        SetupDialog dialog(settings);
        auto* box = dialog.findChild<QCheckBox*>("ckDiscSound");
        CHECK(box && box->isEnabled() && !box->isChecked());
        if (box)
            box->setChecked(true);
        CHECK(dialog.settings().discSounds);
        settings.discSounds = true;
        CHECK(settings.save());
        Settings read;
        read.load();
        CHECK(read.discSounds);
    }
    return checkSummary("gui_sound");
}
