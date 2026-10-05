#include "emulator.h"

#include <QFileInfo>

#include "core/disasm.h"
#include "core/files.h"
#include "core/snapshot.h"

#include "audiooutput.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef __linux__
#include <pthread.h>
#endif

using tuxape::Cpc;
using tuxape::Monitor;

Emulator::Emulator(QObject* parent)
    : QObject(parent)
    , autoType_(cpc_.keyboard())
    , frame_(Monitor::kWidth, Monitor::kHeight, QImage::Format_RGB32)
{
    frame_.fill(Qt::black);
    // Called on the emulation thread, for the addresses watched: those of
    // the breakpoints and the one a step is waiting for.
    cpc_.setExecHook([this](uint16_t pc) {
        if (passOnce_ == pc) {
            passOnce_ = -1;
            return;
        }
        if (temporaryBreak_ == pc || (breakpointsEnabled_ && breakpoints_.count(pc))) {
            cpc_.stopRun();
            stopRequested_ = true;
        }
    });
}

Emulator::~Emulator()
{
    stop();
}

QString Emulator::setupMachine(tuxape::CpcModel model)
{
    return setupMachine(tuxape::stockMachine(model), true);
}

QString Emulator::setupMachine(const tuxape::MachineConfig& config, bool reset)
{
    return withMachine([&](Cpc& cpc) {
        std::string error;
        tuxape::applyMachine(cpc, config, tuxape::defaultRomDir(), &error);
        if (reset)
            cpc.coldReset();
        machine_ = config;
        model_ = tuxape::modelOf(config);
        return QString::fromStdString(error);
    });
}

tuxape::MachineConfig Emulator::machine()
{
    return withMachine([&](Cpc& cpc) {
        tuxape::MachineConfig config = machine_;
        config.ram = cpc.memory().ramExpansion();
        config.siliconDisc = cpc.memory().siliconDisc();
        return config;
    });
}

void Emulator::start()
{
    if (running_)
        return;
    running_ = true;
    thread_ = std::thread([this] { threadMain(); });
}

void Emulator::stop()
{
    {
        // Under the lock, so the thread cannot miss the wake-up while it is
        // about to wait.
        std::lock_guard lock(wakeMutex_);
        if (!running_)
            return;
        running_ = false;
    }
    wake_.notify_all();
    thread_.join();
}

void Emulator::setAudioOutput(AudioOutput* output)
{
    withMachine([&](Cpc& cpc) {
        device_ = output;
        audio_ = soundOn_ && device_ && device_->isOpen() ? device_ : nullptr;
        cpc.audio().setSampleRate(audio_ ? audio_->sampleRate() : 0);
    });
}

void Emulator::setSound(bool on, int sampleRate, bool sixteenBit, bool stereo, int volume, double extraFrames)
{
    withMachine([&](Cpc& cpc) {
        soundOn_ = on;
        eightBit_ = !sixteenBit;
        cpc.audio().setStereo(stereo);
        cpc.audio().setVolume(volume);
        if (device_) {
            // Another sample rate means opening the device again.
            if (on && device_->sampleRate() != sampleRate)
                device_->open(sampleRate);
            device_->setTargetLatency(AudioOutput::kTargetLatency + extraFrames * 0.02);
            if (!on && device_->isOpen())
                device_->clear();
        }
        audio_ = on && device_ && device_->isOpen() ? device_ : nullptr;
        cpc.audio().setSampleRate(audio_ ? audio_->sampleRate() : 0);
        cpc.audio().samples().clear();
    });
}

// Hands the frame's sound to the audio device. Called with the machine
// locked.
void Emulator::playSound()
{
    std::vector<int16_t>& samples = cpc_.audio().samples();
    if (wav_.active())
        wav_.write(samples);
    if (!audio_) {
        samples.clear();
        return;
    }
    // Speeded up or slowed down, the sound would only be noise.
    if (speedPercent_ != 100 || displayEvery_ != 0) {
        if (!audioSilenced_)
            audio_->clear();
        audioSilenced_ = true;
        samples.clear();
        return;
    }
    audioSilenced_ = false;

    // "8 bit": 256 levels instead of 65536, as a sound card of the time
    // would have had.
    if (eightBit_)
        for (int16_t& sample : samples)
            sample = static_cast<int16_t>(sample & ~0xFF);

    const double target = audio_->targetLatency();
    double queued = audio_->queue(samples.data(), samples.size());
    samples.clear();
    // After a stall the backlog would play late for ever: start afresh.
    if (queued > target * 4) {
        audio_->clear();
        queued = target;
    }
    // The emulator's clock and the sound card's never agree exactly. Make
    // slightly more or fewer samples to keep the queue at its target length;
    // the half percent this takes at most is inaudible.
    const double error = std::clamp((queued - target) / target, -1.0, 1.0);
    cpc_.audio().setSampleRate(audio_->sampleRate() * (1.0 - 0.005 * error));
}

void Emulator::setPaused(bool paused)
{
    if (!paused && paused_)
        withMachine([this](Cpc&) { leave(); });
    {
        std::lock_guard lock(wakeMutex_);
        paused_ = paused;
    }
    wake_.notify_all();
}

// ---- recording -------------------------------------------------------------------

bool Emulator::startWavRecording(const QString& path)
{
    return withMachine([&](Cpc& cpc) {
        // With the sound off the mixer is at rest: it runs for the file.
        wavOwnsMixer_ = !cpc.audio().enabled();
        const int rate = audio_ ? audio_->sampleRate() : 44100;
        if (!wav_.start(path.toStdString(), rate))
            return false;
        if (wavOwnsMixer_)
            cpc.audio().setSampleRate(rate);
        recordingWav_ = true;
        return true;
    });
}

void Emulator::stopWavRecording()
{
    withMachine([&](Cpc& cpc) {
        if (!wav_.active())
            return;
        wav_.write(cpc.audio().samples());
        wav_.stop();
        if (wavOwnsMixer_) {
            cpc.audio().setSampleRate(0);
            cpc.audio().samples().clear();
        }
        recordingWav_ = false;
    });
}

bool Emulator::startYmRecording(const QString& path)
{
    // The file is written at the end; better to know now that it can be.
    if (!tuxape::writeFile(path.toStdString(), {}))
        return false;
    withMachine([&](Cpc&) {
        ymPath_ = path;
        ym_.start();
        recordingYm_ = true;
    });
    return true;
}

bool Emulator::stopYmRecording()
{
    return withMachine([&](Cpc&) {
        if (!ym_.active())
            return false;
        recordingYm_ = false;
        const std::vector<uint8_t> file = ym_.finish(QFileInfo(ymPath_).completeBaseName().toStdString());
        return tuxape::writeFile(ymPath_.toStdString(), file);
    });
}

// ---- sessions --------------------------------------------------------------------

void Emulator::startSessionRecording(bool fromColdReset)
{
    using tuxape::CpcModel;
    using tuxape::SnapshotMachine;
    withMachine([&](Cpc& cpc) {
        if (fromColdReset)
            cpc.coldReset();
        const SnapshotMachine machine = model_ == CpcModel::Cpc464     ? SnapshotMachine::Cpc464
                                        : model_ == CpcModel::Cpc664   ? SnapshotMachine::Cpc664
                                        : model_ == CpcModel::Plus464  ? SnapshotMachine::Plus464
                                        : model_ == CpcModel::Plus6128 ? SnapshotMachine::Plus6128
                                                                       : SnapshotMachine::Cpc6128;
        tuxape::Session session;
        session.snapshot = tuxape::saveSnapshot(cpc, machine);
        // The machine goes on from the snapshot as a playback will: what
        // the snapshot does not hold has no say in what follows.
        tuxape::beginSession(cpc, session);
        std::memset(keyHolds_, 0, sizeof keyHolds_);
        std::memset(pcDown_, 0, sizeof pcDown_);
        sessionPlayer_.stop();
        playingSession_ = false;
        sessionRecorder_.start(std::move(session.snapshot));
        recordingSession_ = true;
    });
}

tuxape::Session Emulator::stopSessionRecording()
{
    return withMachine([&](Cpc&) {
        recordingSession_ = false;
        return sessionRecorder_.finish();
    });
}

bool Emulator::playSession(const tuxape::Session& session, QString* error)
{
    const bool loaded = withMachine([&](Cpc& cpc) {
        std::string message;
        if (!tuxape::beginSession(cpc, session, &message)) {
            if (error)
                *error = QString::fromStdString(message);
            return false;
        }
        if (sessionRecorder_.active())
            sessionRecorder_.finish();
        recordingSession_ = false;
        std::memset(keyHolds_, 0, sizeof keyHolds_);
        std::memset(pcDown_, 0, sizeof pcDown_);
        autoType_.cancel();
        sessionPlayer_.start(session);
        playingSession_ = true;
        return true;
    });
    if (loaded)
        setPaused(false);
    return loaded;
}

void Emulator::stopPlayback()
{
    withMachine([&](Cpc& cpc) {
        if (!sessionPlayer_.active())
            return;
        sessionPlayer_.stop();
        playingSession_ = false;
        cpc.keyboard().releaseAll();
    });
}

std::pair<uint32_t, uint32_t> Emulator::playbackPosition()
{
    return withMachine([&](Cpc&) { return std::make_pair(sessionPlayer_.position(), sessionPlayer_.frames()); });
}

// ---- debugging -------------------------------------------------------------------

void Emulator::watchAddresses()
{
    for (int address = 0; address < 0x10000; ++address)
        cpc_.watchAddress(static_cast<uint16_t>(address), false);
    for (const uint16_t address : breakpoints_)
        cpc_.watchAddress(address);
    if (temporaryBreak_ >= 0)
        cpc_.watchAddress(static_cast<uint16_t>(temporaryBreak_));
}

// The machine is about to run on from where it stands: a breakpoint there
// has done its work.
void Emulator::leave()
{
    const uint16_t pc = cpc_.cpu().pc;
    passOnce_ = breakpoints_.count(pc) || temporaryBreak_ == pc ? pc : -1;
}

void Emulator::setBreakpoints(const std::set<uint16_t>& addresses)
{
    withMachine([&](Cpc&) {
        breakpoints_ = addresses;
        watchAddresses();
    });
}

void Emulator::setBreakpointsEnabled(bool enabled)
{
    withMachine([&](Cpc&) { breakpointsEnabled_ = enabled; });
}

void Emulator::setBreakInstructions(bool on)
{
    withMachine([&](Cpc& cpc) {
        breakInstructions_ = on;
        cpc.setBreakInstructions(on);
    });
}

void Emulator::stepInto()
{
    if (!paused_)
        return;
    withMachine([this](Cpc& cpc) {
        cpc.stepInstruction();
        // The picture as it now stands, part-drawn or not.
        publishFrame();
    });
    emit stopped();
}

void Emulator::stepOver()
{
    if (!paused_)
        return;
    const int next = withMachine([](Cpc& cpc) {
        const uint16_t pc = cpc.cpu().pc;
        const tuxape::Instruction instruction = tuxape::disassemble(pc, [&](uint16_t a) { return cpc.memory().read(a); });
        using Flow = tuxape::Instruction::Flow;
        const bool through = instruction.flow == Flow::Call || instruction.flow == Flow::Repeat
                             || instruction.flow == Flow::Halt;
        return through ? static_cast<uint16_t>(pc + instruction.length) : -1;
    });
    if (next < 0)
        stepInto();
    else
        runTo(static_cast<uint16_t>(next));
}

void Emulator::runTo(uint16_t address)
{
    if (!paused_)
        return;
    withMachine([&](Cpc&) {
        temporaryBreak_ = address;
        watchAddresses();
    });
    setPaused(false);
}

void Emulator::setSpeedPercent(int percent)
{
    speedPercent_ = std::clamp(percent, 5, 1000);
}

void Emulator::setDisplayEvery(int frames)
{
    displayEvery_ = std::clamp(frames, 0, 50);
}

void Emulator::setCrtcType(tuxape::CrtcType type)
{
    withMachine([type](Cpc& cpc) { cpc.crtc().setType(type); });
}

tuxape::CrtcType Emulator::crtcType()
{
    return withMachine([](Cpc& cpc) { return cpc.crtc().type(); });
}

void Emulator::setFastDisc(bool fast)
{
    withMachine([fast](Cpc& cpc) { cpc.fdc().setFast(fast); });
}

bool Emulator::fastDisc()
{
    return withMachine([](Cpc& cpc) { return cpc.fdc().fast(); });
}

void Emulator::setMonitor(int kind, bool linearPalette, int brightness)
{
    withMachine([&](Cpc& cpc) {
        cpc.gateArray().setMonitor(static_cast<tuxape::MonitorKind>(std::clamp(kind, 0, 2)), linearPalette, brightness);
    });
}

void Emulator::setVerticalHold(int lines)
{
    withMachine([lines](Cpc& cpc) { cpc.monitor().setVerticalHold(lines); });
}

void Emulator::reset(bool cold)
{
    withMachine([cold](Cpc& cpc) {
        if (cold)
            cpc.coldReset();
        else
            cpc.reset();
    });
}

void Emulator::pcKeyEvent(uint8_t pcKey, bool numLock, bool pressed)
{
    // While a session plays, the keys are its own.
    if (playingSession_)
        return;
    withMachine([&](Cpc& cpc) {
        if (pressed) {
            if (pcDown_[numLock][pcKey])
                return;
            pcDown_[numLock][pcKey] = true;
            for (tuxape::CpcKey key : keyMap_.cpcKeys(pcKey, numLock))
                if (keyHolds_[static_cast<int>(key)]++ == 0)
                    cpc.keyboard().set(key, true);
            return;
        }
        // Release under whichever Num Lock state the key went down in, in
        // case Num Lock was toggled meanwhile.
        for (int state = 0; state < 2; ++state) {
            if (!pcDown_[state][pcKey])
                continue;
            pcDown_[state][pcKey] = false;
            for (tuxape::CpcKey key : keyMap_.cpcKeys(pcKey, state))
                if (--keyHolds_[static_cast<int>(key)] == 0)
                    cpc.keyboard().set(key, false);
        }
    });
}

void Emulator::releaseAllKeys()
{
    withMachine([this](Cpc& cpc) {
        std::memset(pcDown_, 0, sizeof pcDown_);
        std::memset(keyHolds_, 0, sizeof keyHolds_);
        joystickBits_ = 0;
        cpc.keyboard().releaseAll();
    });
}

void Emulator::setKeyMap(const tuxape::KeyMap& map)
{
    // Keys held down under the old layout are let go first.
    releaseAllKeys();
    withMachine([&](Cpc&) { keyMap_ = map; });
}

tuxape::KeyMap Emulator::keyMap()
{
    return withMachine([this](Cpc&) { return keyMap_; });
}

void Emulator::setJoystickEnabled(bool enabled)
{
    joystickEnabled_ = enabled;
    if (!enabled)
        withMachine([this](Cpc&) { applyJoystick(0); });
}

// Called with the machine locked. The joystick's directions and buttons
// hold CPC keys down like PC keys do, so that both can be used together.
void Emulator::applyJoystick(unsigned bits)
{
    static constexpr struct {
        unsigned bit;
        tuxape::CpcKey key;
    } kLines[] = {
        {HostJoystick::Up, tuxape::CpcKey::JoyUp},       {HostJoystick::Down, tuxape::CpcKey::JoyDown},
        {HostJoystick::Left, tuxape::CpcKey::JoyLeft},   {HostJoystick::Right, tuxape::CpcKey::JoyRight},
        // The CPC's main fire button is the one its firmware calls "fire 2".
        {HostJoystick::Fire1, tuxape::CpcKey::JoyFire2}, {HostJoystick::Fire2, tuxape::CpcKey::JoyFire1},
        {HostJoystick::Fire3, tuxape::CpcKey::JoyFire3},
    };
    const unsigned changed = bits ^ joystickBits_;
    joystickBits_ = bits;
    for (const auto& line : kLines) {
        if (!(changed & line.bit))
            continue;
        int& holds = keyHolds_[static_cast<int>(line.key)];
        if (bits & line.bit) {
            if (holds++ == 0)
                cpc_.keyboard().set(line.key, true);
        } else if (--holds == 0) {
            cpc_.keyboard().set(line.key, false);
        }
    }
}

void Emulator::autoType(const QString& text)
{
    // The CPC's character set is ASCII for everything Auto-Type can enter.
    const QByteArray bytes = text.toLatin1();
    withMachine([&](Cpc&) { autoType_.type(std::string_view(bytes.constData(), static_cast<size_t>(bytes.size()))); });
}

QImage Emulator::frame()
{
    std::lock_guard lock(frameMutex_);
    framePending_ = false;
    return frame_.copy();
}

void Emulator::publishFrame()
{
    const uint64_t number = cpc_.monitor().frameNumber();
    if (number == lastFrameNumber_)
        return;
    lastFrameNumber_ = number;
    {
        std::lock_guard lock(frameMutex_);
        const size_t size = static_cast<size_t>(Monitor::kWidth) * Monitor::kHeight * sizeof(uint32_t);
        // A picture that has not changed is not drawn again: a screen that
        // stands still costs the user interface nothing.
        if (std::memcmp(frame_.constBits(), cpc_.monitor().frame(), size) == 0)
            return;
        std::memcpy(frame_.bits(), cpc_.monitor().frame(), size);
    }
    // One notification is enough until the GUI has collected the picture.
    if (!framePending_.exchange(true))
        emit frameReady();
}

void Emulator::threadMain()
{
#ifdef __linux__
    // So that the emulation and the user interface can be told apart in a
    // process viewer.
    pthread_setname_np(pthread_self(), "tuxape-emu");
#endif
    using Clock = std::chrono::steady_clock;
    using std::chrono::duration;
    using std::chrono::duration_cast;

    auto deadline = Clock::now();
    auto statsStart = deadline;
    uint64_t statsMicroseconds = 0;
    uint64_t statsFrames = cpc_.monitor().frameNumber();
    int unshown = 0;  // frames run since the last picture shown

    while (running_) {
        if (paused_) {
            std::unique_lock lock(wakeMutex_);
            wake_.wait(lock, [this] { return !paused_ || !running_; });
            deadline = statsStart = Clock::now();
            statsMicroseconds = 0;
            continue;
        }

        machineMutex_.lock();
        bool playbackOver = false;
        if (sessionPlayer_.active()) {
            // The keyboard is the recording's.
            if (!sessionPlayer_.frame(cpc_.keyboard())) {
                playingSession_ = false;
                playbackOver = true;
            }
        } else {
            if (joystickEnabled_)
                applyJoystick(joystick_.poll());
            autoType_.frame();
            sessionRecorder_.frame(cpc_.keyboard());
        }
        cpc_.runFrame();
        ym_.frame(cpc_.psg());
        // A breakpoint, the end of a step, or a break instruction: the
        // machine has stopped short of the frame's end, and stays there.
        const bool stop = stopRequested_ || cpc_.breakInstructionHit();
        if (stop) {
            stopRequested_ = false;
            if (temporaryBreak_ >= 0) {
                temporaryBreak_ = -1;
                watchAddresses();
            }
            paused_ = true;
        }
        const int every = displayEvery_;
        if (++unshown >= every || stop) {
            publishFrame();
            unshown = 0;
        }
        playSound();
        const uint64_t frames = cpc_.monitor().frameNumber();
        machineMutex_.unlock();
        if (playbackOver)
            emit playbackFinished();
        if (stop) {
            emit stopped();
            continue;
        }
        // At full throttle this thread would otherwise retake the lock
        // before a waiting GUI call gets a chance.
        if (waiters_ > 0)
            std::this_thread::yield();

        statsMicroseconds += Cpc::kFrameMicroseconds;
        const auto now = Clock::now();
        if (now - statsStart >= std::chrono::seconds(1)) {
            const double wall = duration<double, std::micro>(now - statsStart).count();
            emit statsChanged(static_cast<int>(statsMicroseconds * 100.0 / wall + 0.5),
                              static_cast<int>((frames - statsFrames) * 1e6 / wall + 0.5));
            statsStart = now;
            statsMicroseconds = 0;
            statsFrames = frames;
        }

        if (every != 0) {
            // Flat out.
            deadline = now;
            continue;
        }
        deadline += duration_cast<Clock::duration>(
            duration<double, std::micro>(Cpc::kFrameMicroseconds * 100.0 / speedPercent_));
        // If the host cannot keep up, do not try to make up the lost time.
        if (deadline < now - std::chrono::milliseconds(100))
            deadline = now;
        std::this_thread::sleep_until(deadline);
    }
}
