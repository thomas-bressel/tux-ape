#pragma once

#include <cstdio>

// Minimal assertion helpers for the test programs: a failed check is
// reported and counted, and the test carries on.

inline int g_failures = 0;

#define CHECK_EQ(got, want)                                                                         \
    do {                                                                                            \
        const long long checkGot = (got), checkWant = (want);                                       \
        if (checkGot != checkWant) {                                                                \
            std::printf("%s:%d: %s = %lld (0x%llx), want %lld (0x%llx)\n", __FILE__, __LINE__, #got, \
                        checkGot, checkGot, checkWant, checkWant);                                  \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (0)

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);               \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (0)

// Prints a summary and returns the process exit code.
inline int checkSummary(const char* what)
{
    if (g_failures) {
        std::printf("%s: %d check(s) failed\n", what, g_failures);
        return 1;
    }
    std::printf("%s: all checks passed\n", what);
    return 0;
}
