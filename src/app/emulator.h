#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

#include <QImage>
#include <QObject>
#include <QString>

#include "hostjoystick.h"

#include "core/autotype.h"
#include "core/cpc.h"
#include "core/keymap.h"
#include "core/setup.h"

class AudioOutput;

// Runs the emulated machine on its own thread, at the right speed, and hands
// finished pictures to the user interface and sound to the audio output.
//
// The machine is shared between that thread and the GUI thread. Every access
// from the GUI goes through withMachine(), which waits for the current slice
// of emulation (at most one frame) to finish.
class Emulator : public QObject {
    Q_OBJECT

public:
    explicit Emulator(QObject* parent = nullptr);
    ~Emulator() override;

    // Fits a stock machine and cold-resets it. Returns an error message, or
    // an empty string.
    QString setupMachine(tuxape::CpcModel model);
    // Fits the RAM and ROMs described, with a cold reset or, as WinAPE does
    // when its settings change, without any. ROM images that cannot be read
    // leave their place empty and are named in the message returned.
    QString setupMachine(const tuxape::MachineConfig& config, bool reset);
    // What is fitted: the last configuration given, with the RAM the
    // machine has now (a snapshot can bring more).
    tuxape::MachineConfig machine();
    tuxape::CpcModel model() const { return model_; }

    void start();
    void stop();

    // Where the machine's sound goes; nullptr for none. The output must
    // outlive the emulator's use of it. Sound is only played at normal
    // speed.
    void setAudioOutput(AudioOutput* output);

    // Runs `fn(tuxape::Cpc&)` with exclusive access to the machine.
    template <class Fn>
    auto withMachine(Fn&& fn)
    {
        MachineLock lock(*this);
        return std::forward<Fn>(fn)(cpc_);
    }

    void setPaused(bool paused);
    bool isPaused() const { return paused_; }

    // Emulation speed, 100 being the speed of a real CPC.
    void setSpeedPercent(int percent);
    int speedPercent() const { return speedPercent_; }
    // WinAPE's "Display Every n frame(s)": the machine runs as fast as the
    // host allows and only one picture in `frames` is shown. 0 turns this
    // off, and the speed above applies again.
    void setDisplayEvery(int frames);
    int displayEvery() const { return displayEvery_; }

    void setCrtcType(tuxape::CrtcType type);
    tuxape::CrtcType crtcType();
    // Disc drives without their mechanical delays.
    void setFastDisc(bool fast);
    bool fastDisc();

    // Sound, as the Sound page of the Setup window has it: on or off, the
    // sample rate (22050 or 44100), 16 or 8 bits per sample, stereo or
    // mono, the volume (0 to 15) and how many frames of sound are kept in
    // hand on top of the usual (the "buffer synchronisation").
    void setSound(bool on, int sampleRate, bool sixteenBit, bool stereo, int volume, double extraFrames);
    bool soundOn() const { return soundOn_; }

    // The monitor: its kind (0 colour, 1 green, 2 greyscale), WinAPE's
    // "linear palette", the brightness knob (-100 to 100) and the vertical
    // hold (lines the picture is moved by).
    void setMonitor(int kind, bool linearPalette, int brightness);
    void setVerticalHold(int lines);

    void reset(bool cold);

    // A key of the PC keyboard went down or up. `pcKey` is a DirectInput
    // scan code (see keymap.h).
    void pcKeyEvent(uint8_t pcKey, bool numLock, bool pressed);
    void releaseAllKeys();
    // Which PC keys press which CPC keys.
    void setKeyMap(const tuxape::KeyMap& map);
    tuxape::KeyMap keyMap();
    // WinAPE's "Enable Joystick": the host's joystick or game pad moves the
    // CPC's first joystick.
    void setJoystickEnabled(bool enabled);
    bool joystickEnabled() const { return joystickEnabled_; }
    // What the host's stick and buttons are doing (HostJoystick bits),
    // handed to the CPC's joystick. Called for each frame; also the way in
    // for tests.
    void applyJoystick(unsigned bits);

    // Types text on the CPC keyboard (WinAPE Auto-Type syntax).
    void autoType(const QString& text);

    // The latest finished picture: 768 x 270, to be shown twice as tall.
    QImage frame();

signals:
    // A new picture is available from frame().
    void frameReady();
    // Sent about once a second: achieved speed and pictures per second.
    void statsChanged(int speedPercent, int framesPerSecond);

private:
    class MachineLock {
    public:
        explicit MachineLock(Emulator& e)
            : emulator_(e)
        {
            ++emulator_.waiters_;
            emulator_.machineMutex_.lock();
            --emulator_.waiters_;
        }
        ~MachineLock() { emulator_.machineMutex_.unlock(); }

    private:
        Emulator& emulator_;
    };

    tuxape::Cpc cpc_;

    tuxape::CpcModel model_ = tuxape::CpcModel::Cpc6128;
    tuxape::MachineConfig machine_;
    tuxape::KeyMap keyMap_;
    tuxape::AutoType autoType_;
    // How many PC keys currently hold each CPC key down.
    int keyHolds_[tuxape::kCpcKeyCount] = {};
    // PC keys that are down, with the Num Lock state they went down in.
    bool pcDown_[2][256] = {};
    HostJoystick joystick_;
    std::atomic<bool> joystickEnabled_{false};
    unsigned joystickBits_ = 0;  // what the host's joystick holds down

    std::thread thread_;
    std::mutex machineMutex_;
    std::atomic<int> waiters_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> speedPercent_{100};
    std::atomic<int> displayEvery_{0};
    std::mutex wakeMutex_;
    std::condition_variable wake_;

    std::mutex frameMutex_;
    QImage frame_;
    std::atomic<bool> framePending_{false};
    uint64_t lastFrameNumber_ = 0;

    AudioOutput* device_ = nullptr;  // the sound device, if the host has one
    AudioOutput* audio_ = nullptr;   // ... when sound is wanted
    bool soundOn_ = true;
    bool eightBit_ = false;
    bool audioSilenced_ = false;

    void threadMain();
    void publishFrame();
    void playSound();
};
