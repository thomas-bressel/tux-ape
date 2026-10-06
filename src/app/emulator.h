#pragma once

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include <QImage>
#include <QObject>
#include <QString>

#include "avirecorder.h"
#include "hostjoystick.h"

#include "core/autotype.h"
#include "core/cpc.h"
#include "core/recording.h"
#include "core/session.h"
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
    // The drive whose light is on (0 for A:) times 256, plus the cylinder
    // its head is on; -1 when none is at work. As of the last frame.
    int driveLight() const { return driveLight_; }

    // ---- Recording ----
    // The sound, as a WAV file, and the sound chip's registers, as a YM
    // file, from now until stopped. What is recorded is the machine's
    // sound in the machine's time, whatever the speed it is run at. Each
    // start returns false if the file cannot be written.
    bool startWavRecording(const QString& path);
    void stopWavRecording();
    bool recordingWav() const { return recordingWav_; }
    bool startYmRecording(const QString& path);
    bool stopYmRecording();
    bool recordingYm() const { return recordingYm_; }
    // The picture and the sound together, as an AVI file.
    bool startAviRecording(const QString& path);
    void stopAviRecording();
    bool recordingAvi() const { return recordingAvi_; }

    // ---- Sessions ----
    // Records what the keyboard and the joysticks do, from the machine's
    // present state or from a cold reset, until stopped: the session is
    // then handed back.
    void startSessionRecording(bool fromColdReset);
    tuxape::Session stopSessionRecording();
    bool recordingSession() const { return recordingSession_; }
    // Plays a session back. The keyboard is the recording's until it ends
    // (playbackFinished() is then sent) or is stopped. False, with a
    // message, if its snapshot cannot be loaded.
    bool playSession(const tuxape::Session& session, QString* error = nullptr);
    void stopPlayback();
    bool playingSession() const { return playingSession_; }
    // Frames played so far and frames in all, for a clock.
    std::pair<uint32_t, uint32_t> playbackPosition();

    // ---- For the debugger ----
    // Breakpoints: the machine pauses just before the instruction at one of
    // these addresses, and stopped() is sent. They can all be switched off
    // and on together.
    void setBreakpoints(const std::set<uint16_t>& addresses);
    std::set<uint16_t> breakpoints() const { return breakpoints_; }
    void setBreakpointsEnabled(bool enabled);
    bool breakpointsEnabled() const { return breakpointsEnabled_; }
    // What any breakpoint may be given: a condition (see core/condition.h)
    // without which it does not count, and a number of times it must
    // count before the machine pauses, 0 for every time. `count` is how
    // many times it has counted since it last paused the machine.
    struct BreakProps {
        std::string condition;
        int passCount = 0;
        int count = 0;
    };
    // A code breakpoint's; default ones for an address without any.
    BreakProps breakProps(uint16_t address);
    void setBreakProps(uint16_t address, const std::string& condition, int passCount);
    // The machine pauses after an instruction that reads, or one that
    // writes, memory in a range.
    struct MemoryBreak {
        uint16_t address = 0;
        int size = 1;
        bool write = false;
        BreakProps props;
    };
    std::vector<MemoryBreak> memoryBreaks();
    void setMemoryBreaks(const std::vector<MemoryBreak>& breaks);
    // And after one that reads or writes a port: one that is `port` under
    // `mask`.
    struct IoBreak {
        std::string description;  // the device's name, or nothing
        std::string filter;       // what the device asks of the value, as a condition
        uint16_t port = 0;
        uint16_t mask = 0xFFFF;
        bool input = true;
        bool output = true;
        BreakProps props;
    };
    std::vector<IoBreak> ioBreaks();
    void setIoBreaks(const std::vector<IoBreak>& breaks);
    // Timers: a condition's timer_start(id) starts one, and timer_stop(id)
    // gives the microseconds gone by since (65535 at most) and counts
    // them here. reset_cycles(value) sets the debugger's count of
    // microseconds, which cycleBase() is the origin of.
    struct TimerState {
        int id = 0;
        uint64_t count = 0;  // times stopped
        uint64_t last = 0;   // microseconds, the last time
        uint64_t least = 0;
        uint64_t most = 0;
        uint64_t total = 0;
    };
    std::vector<TimerState> timers();
    void clearTimers();
    uint64_t cycleBase();
    void resetCycles(uint64_t value = 0);

    // Whether a condition can be made sense of.
    bool conditionValid(const std::string& condition);
    // The symbols conditions may name: those of the last assembly, in
    // upper case.
    void setSymbols(std::map<std::string, int32_t> symbols);

    // ED FF in a program pauses the machine after it.
    void setBreakInstructions(bool on);
    bool breakInstructions() const { return breakInstructions_; }
    // With the machine paused. One instruction, or one turn of one that
    // repeats. The same, but a CALL or a repeating instruction is run
    // through to the instruction after it (if the program ever gets there:
    // the machine runs until then). And on, up to an address.
    void stepInto();
    void stepOver();
    void runTo(uint16_t address);

signals:
    // A new picture is available from frame().
    void frameReady();
    // The machine has paused by itself: on a breakpoint, a break
    // instruction, or at the end of a step.
    void stopped();
    // The drive at work has changed, or its head has moved: see
    // driveLight().
    void driveLightChanged();
    // A session has been played to its end.
    void playbackFinished();
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

    // Debugging. All but stopRequested_ are only touched with the
    // machine's lock held.
    std::atomic<int> driveLight_{-1};
    std::set<uint16_t> breakpoints_;
    std::map<uint16_t, BreakProps> breakProps_;
    std::vector<MemoryBreak> memoryBreaks_;
    std::vector<IoBreak> ioBreaks_;
    std::map<std::string, int32_t> symbols_;
    struct Timer {
        TimerState state;
        uint64_t started = 0;
        bool running = false;
    };
    std::map<int, Timer> timers_;
    uint64_t cycleBase_ = 0;
    std::optional<int32_t> conditionFunction(const std::string& name, const std::vector<int32_t>& args, bool act);
    bool counts(BreakProps& props, uint32_t address, uint32_t value, uint32_t previous);
    void watchMemory();
    bool breakpointsEnabled_ = true;
    bool breakInstructions_ = false;
    int temporaryBreak_ = -1;  // the address a step over or a run-to waits for
    int passOnce_ = -1;        // the breakpoint the machine is leaving
    bool stopRequested_ = false;
    void watchAddresses();
    void leave();

    std::mutex frameMutex_;
    QImage frame_;
    std::atomic<bool> framePending_{false};
    uint64_t lastFrameNumber_ = 0;

    tuxape::SessionRecorder sessionRecorder_;
    tuxape::SessionPlayer sessionPlayer_;
    std::atomic<bool> recordingSession_{false};
    std::atomic<bool> playingSession_{false};

    tuxape::WavRecorder wav_;
    tuxape::YmRecorder ym_;
    AviRecorder avi_;
    std::atomic<bool> recordingWav_{false};
    std::atomic<bool> recordingYm_{false};
    std::atomic<bool> recordingAvi_{false};
    bool recordingOwnsMixer_ = false;  // the mixer runs for a recording alone
    int startMixerForRecording();
    void stopMixerForRecording();
    QString ymPath_;

    AudioOutput* device_ = nullptr;  // the sound device, if the host has one
    AudioOutput* audio_ = nullptr;   // ... when sound is wanted
    bool soundOn_ = true;
    bool eightBit_ = false;
    bool audioSilenced_ = false;

    void threadMain();
    void publishFrame();
    void playSound();
};
