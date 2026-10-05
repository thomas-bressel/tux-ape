#include "emulator.h"

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
    if (!audio_)
        return;
    std::vector<int16_t>& samples = cpc_.audio().samples();
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
    {
        std::lock_guard lock(wakeMutex_);
        paused_ = paused;
    }
    wake_.notify_all();
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
        if (joystickEnabled_)
            applyJoystick(joystick_.poll());
        autoType_.frame();
        cpc_.runFrame();
        const int every = displayEvery_;
        if (++unshown >= every) {
            publishFrame();
            unshown = 0;
        }
        playSound();
        const uint64_t frames = cpc_.monitor().frameNumber();
        machineMutex_.unlock();
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
