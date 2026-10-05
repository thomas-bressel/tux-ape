#include "emulator.h"

#include <algorithm>
#include <chrono>
#include <cstring>

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
    return withMachine([&](Cpc& cpc) {
        std::string error;
        if (!tuxape::setupStockMachine(cpc, model, tuxape::defaultRomDir(), &error))
            return QString::fromStdString(error);
        return QString();
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
        cpc.keyboard().releaseAll();
    });
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
        std::memcpy(frame_.bits(), cpc_.monitor().frame(),
                    static_cast<size_t>(Monitor::kWidth) * Monitor::kHeight * sizeof(uint32_t));
    }
    // One notification is enough until the GUI has collected the picture.
    if (!framePending_.exchange(true))
        emit frameReady();
}

void Emulator::threadMain()
{
    using Clock = std::chrono::steady_clock;
    using std::chrono::duration;
    using std::chrono::duration_cast;

    auto deadline = Clock::now();
    auto statsStart = deadline;
    uint64_t statsMicroseconds = 0;
    uint64_t statsFrames = cpc_.monitor().frameNumber();

    while (running_) {
        if (paused_) {
            std::unique_lock lock(wakeMutex_);
            wake_.wait(lock, [this] { return !paused_ || !running_; });
            deadline = statsStart = Clock::now();
            statsMicroseconds = 0;
            continue;
        }

        machineMutex_.lock();
        autoType_.frame();
        cpc_.runFrame();
        publishFrame();
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

        deadline += duration_cast<Clock::duration>(
            duration<double, std::micro>(Cpc::kFrameMicroseconds * 100.0 / speedPercent_));
        // If the host cannot keep up, do not try to make up the lost time.
        if (deadline < now - std::chrono::milliseconds(100))
            deadline = now;
        std::this_thread::sleep_until(deadline);
    }
}
