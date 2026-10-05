// Keeps the emulation light. TuxAPE is meant to run comfortably on an old
// machine (the target is an Intel Core i5 with 2 GB of RAM), so the core
// must stay many times faster than a real CPC on one processor core, and
// small in memory. This test measures both and fails when either drifts
// too far; `tuxape-headless --bench 60` gives the same speed figure by
// hand.
//
// On the laptop TuxAPE is developed on (Ryzen 7 7735U) the core ran 40
// times faster than a CPC in October 2026. The floor below is far under
// that: it is there to catch a change that makes the emulation several
// times heavier, not to time the host. Unoptimised builds are only
// measured, not judged.
//
// Needs the ROM images; exits with code 77 ("skipped") without them.

#include <cstdio>
#include <ctime>

#ifdef __linux__
#include <sys/resource.h>
#endif

#include "check.h"
#include "core/cpc.h"
#include "core/setup.h"

namespace {

using namespace tuxape;

constexpr double kSlowestAllowed = 10.0;  // times the speed of a real CPC, on one core

// Emulated seconds per second of processor time, with the sound made as the
// application has it made.
double speed(Cpc& cpc, int frames)
{
    cpc.audio().setSampleRate(44100);
    const uint64_t start = cpc.microseconds();
    const std::clock_t cpuStart = std::clock();
    for (int frame = 0; frame < frames; ++frame) {
        cpc.runFrame();
        cpc.audio().samples().clear();
    }
    const double cpu = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
    return static_cast<double>(cpc.microseconds() - start) / 1e6 / (cpu > 0 ? cpu : 1e-9);
}

}  // namespace

int main()
{
    Cpc cpc;
    if (!setupStockMachine(cpc, CpcModel::Cpc6128, defaultRomDir(), nullptr)) {
        std::printf("ROM images not found; skipping\n");
        return 77;
    }
    // The machine starts, then sits at its prompt: the video is drawn all
    // the time, which is where the work is.
    speed(cpc, 100);
    double best = 0;
    for (int run = 0; run < 3; ++run) {
        const double measured = speed(cpc, 500);
        best = measured > best ? measured : best;
    }
    std::printf("core speed: %.1f times a real CPC on one core (%.1f %% of a core at normal speed)\n", best,
                100.0 / best);
#ifdef NDEBUG
    if (best < kSlowestAllowed) {
        std::printf("that is under the floor of %.0f times: the emulation has become too heavy\n", kSlowestAllowed);
        ++g_failures;
    }
#else
    std::printf("(unoptimised build: measured, not judged)\n");
#endif

#ifdef __linux__
    // Memory: everything this process has needed to run a CPC6128 (its RAM,
    // its ROMs, its picture, and the C++ library). A few megabytes; the
    // ceiling is there so that nothing large slips in unnoticed.
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::printf("memory used by the whole test: %.1f MB\n", static_cast<double>(usage.ru_maxrss) / 1024.0);
    CHECK(usage.ru_maxrss < 32 * 1024);  // in KB
#endif

    return checkSummary("core_speed");
}
