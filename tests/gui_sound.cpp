// The application's sound path, end to end: the emulated machine plays a
// note, the emulator hands the samples to SDL, and SDL's "disk" audio driver
// writes what would have gone to the sound card into a file, which is then
// checked for the right pitch.
// Runs without a display or a sound card.

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "audiooutput.h"
#include "check.h"
#include "core/screen_text.h"
#include "emulator.h"

int main(int argc, char* argv[])
{
    QTemporaryDir folder;
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
    // Two seconds at 440 Hz. The disk driver takes sound a few percent
    // faster than a sound card would, so the capture can have a gap or two
    // where the queue ran dry; that costs a cycle at most each time.
    CHECK(rises.size() > 870 && rises.size() <= 881);
    if (rises.size() > 2) {
        const double seconds = static_cast<double>(rises.back() - rises.front()) / rate;
        CHECK(seconds > 1.95 && seconds < 2.3);
        // The pitch is judged on the typical cycle, which gaps do not affect.
        std::vector<size_t> periods;
        for (size_t i = 1; i < rises.size(); ++i)
            periods.push_back(rises[i] - rises[i - 1]);
        std::nth_element(periods.begin(), periods.begin() + static_cast<ptrdiff_t>(periods.size() / 2), periods.end());
        const double pitch = static_cast<double>(rate) / static_cast<double>(periods[periods.size() / 2]);
        if (!(pitch > 436 && pitch < 446))
            std::printf("tone: %zu cycles over %.3f s, typical pitch %.1f Hz\n", rises.size(), seconds, pitch);
        CHECK(pitch > 436 && pitch < 446);
    }

    return checkSummary("gui_sound");
}
