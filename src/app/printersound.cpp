#include "printersound.h"

#include <algorithm>
#include <bit>
#include <cmath>

using tuxape::Dmp2000;

namespace {

// The steppers' steps: the carriage's 1/60 inch, the paper's 1/216.
constexpr double kCarriageSteps = 60.0;
constexpr double kPaperSteps = 216.0;
// How long a strike's rattle lasts, and the beeper.
constexpr double kBurst = 0.0003;
constexpr uint64_t kBeep = 250000;
// Older than this, what the printer did is not heard any more.
constexpr uint64_t kStale = 200000;

}  // namespace

void PrinterSound::Ring::tune(float hertz, float width, int rate)
{
    const float turn = 2.0f * 3.14159265f * hertz / static_cast<float>(rate);
    const float r = std::exp(-3.14159265f * width / static_cast<float>(rate));
    a1 = 2.0f * r * std::cos(turn);
    a2 = r * r;
    // Struck once, it rings at about 1 whatever its pitch.
    gain = std::sin(turn);
    y1 = y2 = 0.0f;
}

float PrinterSound::random()
{
    noise_ ^= noise_ << 13;
    noise_ ^= noise_ >> 17;
    noise_ ^= noise_ << 5;
    return static_cast<float>(static_cast<int32_t>(noise_)) / 2147483648.0f;
}

void PrinterSound::reset()
{
    strikes_.clear();
    motions_.clear();
    beeps_.clear();
    rate_ = 0;  // the rings tuned again, and still
    burst_ = 0;
    beepUntil_ = 0;
    ringing_ = 0;
}

void PrinterSound::add(const std::vector<Dmp2000::Event>& events, uint64_t now)
{
    const auto fresh = [now](uint64_t at) { return at + kStale >= now; };
    for (const Dmp2000::Event& e : events) {
        switch (e.kind) {
        case Dmp2000::Event::Kind::Strike:
            if (fresh(e.microsecond)) {
                // The more needles, the louder.
                const float force = 0.35f + 0.65f * static_cast<float>(std::popcount(e.strike.pins)) / 9.0f;
                const Strike s{e.microsecond, force};
                // They come in time, but for a line's second pass.
                strikes_.insert(std::upper_bound(strikes_.begin(), strikes_.end(), s,
                                                 [](const Strike& a, const Strike& b) { return a.at < b.at; }),
                                s);
            }
            break;
        case Dmp2000::Event::Kind::Motion:
            if (fresh(e.motion.end))
                motions_.push_back(e.motion);
            break;
        case Dmp2000::Event::Kind::Beep:
            if (fresh(e.microsecond))
                beeps_.push_back(e.microsecond);
            break;
        default:
            break;
        }
    }
}

void PrinterSound::mix(std::vector<int16_t>& stereo, int rate, uint64_t now)
{
    const size_t frames = stereo.size() / 2;
    // The stretch: the machine's time since the last one.
    const uint64_t from = std::max(heard_, now > kStale ? now - kStale : 0);
    heard_ = now;
    if (rate <= 0 || frames == 0 || now <= from)
        return;
    if (strikes_.empty() && motions_.empty() && beeps_.empty() && ringing_ <= 0 && now >= beepUntil_)
        return;
    if (rate != rate_) {
        rate_ = rate;
        head_.tune(2200.0f, 400.0f, rate);    // the head's needles and their guide
        body_.tune(4800.0f, 900.0f, rate);    // the rattle of its plastic
        platen_.tune(650.0f, 120.0f, rate);   // the platen, knocked
        carriage_.tune(420.0f, 60.0f, rate);  // the carriage's motor and belt
        paper_.tune(260.0f, 50.0f, rate);     // the paper's motor
        lowCut_ = std::exp(-2.0f * 3.14159265f * 40.0f / static_cast<float>(rate));
        lastIn_ = lastOut_ = 0.0f;
    }
    const double step = static_cast<double>(now - from) / static_cast<double>(frames);  // microseconds a sample
    const int burst = std::max(1, static_cast<int>(kBurst * rate));
    double t = static_cast<double>(from);
    for (size_t i = 0; i < frames; ++i) {
        const double before = t;
        t += step;
        float needles = 0.0f, knock = 0.0f, carriage = 0.0f, paper = 0.0f;
        while (!strikes_.empty() && static_cast<double>(strikes_.front().at) <= t) {
            needles += strikes_.front().force;
            burst_ = burst;
            burstForce_ = strikes_.front().force;
            strikes_.pop_front();
        }
        if (burst_ > 0) {
            needles += random() * 0.4f * burstForce_;
            --burst_;
        }
        knock += 0.4f * needles;
        for (auto m = motions_.begin(); m != motions_.end();) {
            const double start = static_cast<double>(m->start), end = static_cast<double>(m->end);
            const bool across = m->kind == Dmp2000::Motion::Kind::Carriage;
            // A knock where a stepper starts and where it stops.
            if ((start > before && start <= t) || (end > before && end <= t))
                (across ? knock : paper) += across ? 0.7f : 0.5f;
            if (start <= t && t < end) {
                const double inchesPerSample = std::fabs(m->to - m->from) / (end - start) * step;
                double& where = across ? carriageStep_ : paperStep_;
                where += inchesPerSample * (across ? kCarriageSteps : kPaperSteps);
                if (where >= 1.0) {
                    where -= std::floor(where);
                    (across ? carriage : paper) += across ? 0.5f + 0.1f * random() : 0.6f + 0.15f * random();
                }
            }
            if (end < before)
                m = motions_.erase(m);
            else
                ++m;
        }
        while (!beeps_.empty() && static_cast<double>(beeps_.front()) <= t) {
            beepUntil_ = beeps_.front() + kBeep;
            beeps_.pop_front();
        }
        if (needles != 0.0f || knock != 0.0f || carriage != 0.0f || paper != 0.0f)
            ringing_ = rate / 10;
        float sound = head_.step(needles) * 5000.0f + body_.step(needles) * 2200.0f + platen_.step(knock) * 3200.0f +
                      carriage_.step(carriage) * 1600.0f + paper_.step(paper) * 2400.0f;
        if (t < static_cast<double>(beepUntil_)) {
            // The beeper: a buzzer's 2 kHz, nearly square.
            beepTurn_ += 2000.0 * step / 1e6;
            beepTurn_ -= std::floor(beepTurn_);
            const double a = beepTurn_ * 6.283185307;
            sound += static_cast<float>(std::sin(a) + 0.3 * std::sin(3 * a)) * 1800.0f;
        }
        if (ringing_ > 0)
            --ringing_;
        // The steppers' knocks all push one way: no more than a sound of
        // them heard (a high-pass at some 40 Hz), and not too loud beside
        // the machine's.
        const float heardNow = sound - lastIn_ + lowCut_ * lastOut_;
        lastIn_ = sound;
        lastOut_ = heardNow;
        sound = heardNow * 0.7f;
        // Beside the machine, a little to its right.
        const int left = static_cast<int>(sound * 0.85f), right = static_cast<int>(sound);
        stereo[2 * i] = static_cast<int16_t>(std::clamp(stereo[2 * i] + left, -32768, 32767));
        stereo[2 * i + 1] = static_cast<int16_t>(std::clamp(stereo[2 * i + 1] + right, -32768, 32767));
    }
}
