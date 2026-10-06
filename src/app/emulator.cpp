#include "emulator.h"

#include <QFileInfo>

#include "core/condition.h"
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
        bool stop = temporaryBreak_ == pc;
        if (breakpointsEnabled_ && breakpoints_.count(pc)) {
            const auto props = breakProps_.find(pc);
            stop = props == breakProps_.end() || counts(props->second, pc, 0, 0) || stop;
        }
        if (stop) {
            cpc_.stopRun();
            stopRequested_ = true;
        }
    });
    cpc_.setMemoryHook([this](uint16_t address, uint8_t value, uint8_t previous, bool write) {
        if (!breakpointsEnabled_)
            return;
        for (MemoryBreak& point : memoryBreaks_) {
            if (point.write != write || address < point.address || address - point.address >= point.size)
                continue;
            if (counts(point.props, address, value, previous)) {
                cpc_.stopRun();
                stopRequested_ = true;
            }
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

void Emulator::setAmDrum(bool on)
{
    withMachine([&](Cpc& cpc) { cpc.setAmDrum(on); });
}

bool Emulator::setPrinter(int mode, const QString& file)
{
    return withMachine([&](Cpc& cpc) {
        cpc.setPrinterHook(nullptr);
        cpc.setDigiblaster(mode == 1);
        printerFile_.close();
        if (mode == 3) {
            // What is printed is added to the file, a line at a time.
            printerFile_.setFileName(file);
            if (file.isEmpty() || !printerFile_.open(QIODevice::WriteOnly | QIODevice::Append))
                return false;
            cpc.setPrinterHook([this](uint8_t character) {
                printerFile_.putChar(static_cast<char>(character));
                if (character == '\n')
                    printerFile_.flush();
            });
        } else if (mode == 4) {
            cpc.setPrinterHook([this](uint8_t character) {
                bool first = false;
                {
                    std::lock_guard lock(printerMutex_);
                    first = printerBuffer_.isEmpty();
                    printerBuffer_ += static_cast<char>(character);
                }
                // One notice is enough until the text has been taken.
                if (first)
                    emit printerOutput();
            });
        }
        return true;
    });
}

QByteArray Emulator::takePrinterOutput()
{
    std::lock_guard lock(printerMutex_);
    QByteArray text;
    text.swap(printerBuffer_);
    return text;
}

void Emulator::setTapeSounds(bool on)
{
    withMachine([&](Cpc& cpc) { cpc.setTapeSound(on); });
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

    // The drives are heard beside the machine, not through it: their
    // sounds are in what is played, not in what is recorded.
    if (discSounds_) {
        int steps = 0;
        for (int drive = 0; drive < 4; ++drive) {
            const int at = cpc_.fdc().drive(drive).cylinder;
            steps += std::abs(at - headAt_[drive]);
            headAt_[drive] = at;
        }
        driveSound_.mix(samples, audio_->sampleRate(), cpc_.fdc().motor(), steps);
    }

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

void Emulator::setDisplayClock(bool on)
{
    {
        std::lock_guard lock(tickMutex_);
        displayClock_ = on;
        ticked_ = false;
        lastTick_ = {};
    }
    tick_.notify_all();
}

void Emulator::displayTick()
{
    {
        std::lock_guard lock(tickMutex_);
        ticked_ = true;
        lastTick_ = std::chrono::steady_clock::now();
    }
    tick_.notify_all();
}

// ---- recording -------------------------------------------------------------------

// With the sound off the mixer is at rest: it is set running for a
// recording, and stopped when the last recording ends. Both are called
// with the machine's lock held. The first gives the rate of the samples.
int Emulator::startMixerForRecording()
{
    const int rate = audio_ ? audio_->sampleRate() : 44100;
    if (!cpc_.audio().enabled()) {
        cpc_.audio().setSampleRate(rate);
        recordingOwnsMixer_ = true;
    }
    return rate;
}

void Emulator::stopMixerForRecording()
{
    if (!recordingOwnsMixer_ || wav_.active() || avi_.active())
        return;
    recordingOwnsMixer_ = false;
    cpc_.audio().setSampleRate(0);
    cpc_.audio().samples().clear();
}

bool Emulator::startWavRecording(const QString& path)
{
    return withMachine([&](Cpc&) {
        if (!wav_.start(path.toStdString(), audio_ ? audio_->sampleRate() : 44100))
            return false;
        startMixerForRecording();
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
        stopMixerForRecording();
        recordingWav_ = false;
    });
}

bool Emulator::startAviRecording(const QString& path)
{
    return withMachine([&](Cpc&) {
        if (!avi_.start(path, audio_ ? audio_->sampleRate() : 44100))
            return false;
        startMixerForRecording();
        recordingAvi_ = true;
        return true;
    });
}

void Emulator::stopAviRecording()
{
    withMachine([&](Cpc&) {
        avi_.stop();
        stopMixerForRecording();
        recordingAvi_ = false;
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
        // A breakpoint taken away takes its condition and its count with it.
        std::erase_if(breakProps_, [&](const auto& props) { return !breakpoints_.count(props.first); });
        watchAddresses();
    });
}

// Whether a breakpoint that has been reached stops the machine: its
// condition must hold, and that as many times as its pass count says.
bool Emulator::counts(BreakProps& props, uint32_t address, uint32_t value, uint32_t previous)
{
    if (!props.condition.empty()) {
        tuxape::ConditionContext context;
        context.address = address;
        context.value = value;
        context.previous = previous;
        context.symbol = [this](const std::string& name) -> std::optional<int32_t> {
            const auto found = symbols_.find(name);
            return found == symbols_.end() ? std::nullopt : std::optional<int32_t>(found->second);
        };
        context.function = [this](const std::string& name, const std::vector<int32_t>& args) {
            return conditionFunction(name, args, true);
        };
        const std::optional<int32_t> holds = tuxape::evaluateCondition(props.condition, cpc_, context);
        if (!holds || *holds == 0)
            return false;
    }
    ++props.count;
    if (props.passCount > 0 && props.count < props.passCount)
        return false;
    if (props.passCount > 0)
        props.count = 0;
    return true;
}

Emulator::BreakProps Emulator::breakProps(uint16_t address)
{
    return withMachine([&](Cpc&) {
        const auto found = breakProps_.find(address);
        return found == breakProps_.end() ? BreakProps() : found->second;
    });
}

void Emulator::setBreakProps(uint16_t address, const std::string& condition, int passCount)
{
    withMachine([&](Cpc&) {
        if (condition.empty() && passCount <= 0) {
            breakProps_.erase(address);
        } else {
            BreakProps& props = breakProps_[address];
            props.condition = condition;
            props.passCount = passCount;
            props.count = 0;
        }
    });
}

std::vector<Emulator::MemoryBreak> Emulator::memoryBreaks()
{
    return withMachine([&](Cpc&) { return memoryBreaks_; });
}

void Emulator::setMemoryBreaks(const std::vector<MemoryBreak>& breaks)
{
    withMachine([&](Cpc& cpc) {
        memoryBreaks_ = breaks;
        cpc.clearMemoryWatches();
        for (const MemoryBreak& point : memoryBreaks_)
            for (int n = 0; n < point.size; ++n)
                cpc.watchMemory(static_cast<uint16_t>(point.address + n), !point.write, point.write);
    });
}

std::vector<Emulator::IoBreak> Emulator::ioBreaks()
{
    return withMachine([&](Cpc&) { return ioBreaks_; });
}

void Emulator::setIoBreaks(const std::vector<IoBreak>& breaks)
{
    withMachine([&](Cpc& cpc) {
        ioBreaks_ = breaks;
        // Nothing is asked of the machine while there are none.
        if (ioBreaks_.empty()) {
            cpc.setIoHook(nullptr);
            return;
        }
        cpc.setIoHook([this](uint16_t port, uint8_t value, bool write) {
            if (!breakpointsEnabled_)
                return;
            for (IoBreak& point : ioBreaks_) {
                if (!(write ? point.output : point.input) || ((port ^ point.port) & point.mask))
                    continue;
                if (!point.filter.empty()) {
                    tuxape::ConditionContext context;
                    context.address = port;
                    context.value = value;
                    if (tuxape::evaluateCondition(point.filter, cpc_, context).value_or(0) == 0)
                        continue;
                }
                if (counts(point.props, port, value, 0)) {
                    cpc_.stopRun();
                    stopRequested_ = true;
                }
            }
        });
    });
}

// The functions the emulator adds to conditions. Without `act` they are
// only checked: nothing starts or stops.
std::optional<int32_t> Emulator::conditionFunction(const std::string& name, const std::vector<int32_t>& args, bool act)
{
    if (args.size() > 1)
        return std::nullopt;
    const int id = args.empty() ? 0 : args[0];
    const uint64_t now = cpc_.instructionTime();
    if (name == "TIMER_START") {
        if (act) {
            Timer& timer = timers_[id];
            timer.state.id = id;
            timer.started = now;
            timer.running = true;
        }
        return 0;
    }
    if (name == "TIMER_STOP") {
        if (!act)
            return 1;
        const auto found = timers_.find(id);
        if (found == timers_.end() || !found->second.running)
            return 0;
        Timer& timer = found->second;
        TimerState& state = timer.state;
        state.last = now - timer.started;
        state.least = state.count ? std::min(state.least, state.last) : state.last;
        state.most = std::max(state.most, state.last);
        state.total += state.last;
        ++state.count;
        return static_cast<int32_t>(std::min<uint64_t>(state.last, 0xFFFF));
    }
    if (name == "RESET_CYCLES") {
        if (act)
            cycleBase_ = now - static_cast<uint64_t>(id);
        return 0;
    }
    return std::nullopt;
}

std::vector<Emulator::TimerState> Emulator::timers()
{
    return withMachine([&](Cpc&) {
        std::vector<TimerState> states;
        for (const auto& [id, timer] : timers_)
            states.push_back(timer.state);
        return states;
    });
}

void Emulator::clearTimers()
{
    withMachine([&](Cpc&) { timers_.clear(); });
}

uint64_t Emulator::cycleBase()
{
    return withMachine([&](Cpc&) { return cycleBase_; });
}

void Emulator::resetCycles(uint64_t value)
{
    withMachine([&](Cpc& cpc) { cycleBase_ = cpc.instructionTime() - value; });
}

bool Emulator::conditionValid(const std::string& condition)
{
    return withMachine([&](Cpc& cpc) {
        tuxape::ConditionContext context;
        context.symbol = [this](const std::string& name) -> std::optional<int32_t> {
            const auto found = symbols_.find(name);
            return found == symbols_.end() ? std::nullopt : std::optional<int32_t>(found->second);
        };
        context.function = [this](const std::string& name, const std::vector<int32_t>& args) {
            return conditionFunction(name, args, false);
        };
        // poke() must not go off while the condition is only being read:
        // the machine's memory is put back as it was.
        std::vector<uint8_t> before(0x10000);
        for (int a = 0; a < 0x10000; ++a)
            before[static_cast<size_t>(a)] = cpc.memory().readRam(static_cast<uint16_t>(a));
        const bool valid = tuxape::evaluateCondition(condition, cpc, context).has_value();
        for (int a = 0; a < 0x10000; ++a)
            if (cpc.memory().readRam(static_cast<uint16_t>(a)) != before[static_cast<size_t>(a)])
                cpc.memory().write(static_cast<uint16_t>(a), before[static_cast<size_t>(a)]);
        return valid;
    });
}

void Emulator::setSymbols(std::map<std::string, int32_t> symbols)
{
    withMachine([&](Cpc&) { symbols_ = std::move(symbols); });
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

void Emulator::setTurbo(bool on)
{
    withMachine([on](Cpc& cpc) { cpc.setTurbo(on); });
}

void Emulator::setPlusPpi(bool on)
{
    withMachine([on](Cpc& cpc) { cpc.setPlusPpi(on); });
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

void Emulator::multifaceStop()
{
    withMachine([](Cpc& cpc) { cpc.multifaceStop(); });
}

bool Emulator::hasMultiface()
{
    return withMachine([](Cpc& cpc) { return cpc.hasMultiface(); });
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

void Emulator::setAmxMouse(bool enabled)
{
    amxMouse_ = enabled;
    amxDx_ = amxDy_ = 0;
    amxButtons_ = 0;
    withMachine([enabled](Cpc& cpc) { cpc.setAmxMouse(enabled); });
}

void Emulator::amxMouseMoved(int dx, int dy)
{
    amxDx_ += dx;
    amxDy_ += dy;
}

void Emulator::amxMouseButtons(unsigned buttons)
{
    amxButtons_ = buttons;
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

QImage Emulator::frameInProgress()
{
    QImage picture(Monitor::kWidth, Monitor::kHeight, QImage::Format_RGB32);
    withMachine([&](Cpc& cpc) {
        const Monitor& monitor = cpc.monitor();
        const size_t row = static_cast<size_t>(Monitor::kWidth) * sizeof(uint32_t);
        const int line = std::clamp(monitor.rasterLine(), 0, static_cast<int>(Monitor::kHeight));
        std::memcpy(picture.bits(), monitor.frame(), row * Monitor::kHeight);
        std::memcpy(picture.bits(), monitor.drawing(), row * static_cast<size_t>(line));
        if (line < Monitor::kHeight) {
            const int column = std::clamp(monitor.beamColumn(), 0, static_cast<int>(Monitor::kWidth));
            std::memcpy(picture.scanLine(line), monitor.drawing() + static_cast<size_t>(line) * Monitor::kWidth,
                        static_cast<size_t>(column) * sizeof(uint32_t));
        }
    });
    return picture;
}

void Emulator::setDiscSounds(bool on)
{
    withMachine([&](Cpc&) {
        discSounds_ = on;
        driveSound_.reset();
    });
}

void Emulator::publishFrame()
{
    const uint64_t number = cpc_.monitor().frameNumber();
    if (number == lastFrameNumber_)
        return;
    lastFrameNumber_ = number;
    // The drive's light, for the picture's corner.
    const int drive = cpc_.fdc().activeDrive();
    const int light = drive < 0 ? -1 : drive << 8 | (cpc_.fdc().drive(drive).cylinder & 0xFF);
    if (driveLight_.exchange(light) != light)
        emit driveLightChanged();
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
            if (amxMouse_) {
                // What the host's mouse has done since the last frame.
                cpc_.moveAmxMouse(amxDx_.exchange(0), amxDy_.exchange(0));
                const unsigned buttons = amxButtons_;
                cpc_.setAmxButtons(buttons & 1, buttons & 4, buttons & 2);
            }
            autoType_.frame();
            sessionRecorder_.frame(cpc_.keyboard());
        }
        cpc_.runFrame();
        ym_.frame(cpc_.psg());
        if (avi_.active()) {
            avi_.addFrame(cpc_.monitor().frame(), cpc_.audio().samples());
            // It ends by itself when its file is as large as one can be.
            if (!avi_.active()) {
                stopMixerForRecording();
                recordingAvi_ = false;
            }
        }
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
        if (displayClock_ && speedPercent_ == 100) {
            // In step with the screen: the next frame starts when the
            // screen says it has shown a picture. `deadline` is when this
            // frame started. While the screen keeps saying so, it is
            // waited for a little past the machine's own time; when it
            // stops, the machine's own time is kept exactly.
            const auto period = duration_cast<Clock::duration>(duration<double, std::micro>(Cpc::kFrameMicroseconds));
            const auto started = deadline, due = started + period;
            bool told = false;
            for (;;) {
                std::unique_lock lock(tickMutex_);
                const bool alive = lastTick_ != Clock::time_point() && Clock::now() - lastTick_ < 3 * period;
                const auto limit = alive ? std::max(due, lastTick_ + period) + std::chrono::milliseconds(4) : due;
                tick_.wait_until(lock, limit, [this] { return ticked_ || !running_ || !displayClock_; });
                told = ticked_;
                ticked_ = false;
                lock.unlock();
                // A tick that comes too soon is the last frame's: the
                // screen's next one is this frame's.
                if (!told || !running_ || !displayClock_ || Clock::now() >= started + period * 3 / 4)
                    break;
            }
            deadline = told ? Clock::now() : due;
            if (deadline < Clock::now() - std::chrono::milliseconds(100))
                deadline = Clock::now();
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
