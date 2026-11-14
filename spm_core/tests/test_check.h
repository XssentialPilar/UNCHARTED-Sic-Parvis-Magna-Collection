// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore test helper: assertion checks that stay active in Release builds.
//
// <cassert> expands to nothing when NDEBUG is defined, which silently turned
// every Release test into a no-op. CHECK records a failure and keeps going;
// REQUIRE stops immediately when later checks would depend on the invariant.
#ifndef SPM_TEST_CHECK_H
#define SPM_TEST_CHECK_H

#include <cstdio>
#include <cstdlib>

namespace spm_test {

inline int& Failures() {
    static int failures = 0;
    return failures;
}

inline int Finish(const char* suite) {
    if (Failures() == 0) {
        std::printf("[PASS] %s\n", suite);
        return 0;
    }
    std::fprintf(stderr, "[FAIL] %s: %d check(s) failed\n", suite, Failures());
    return 1;
}

} // namespace spm_test

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition,        \
                         __FILE__, __LINE__);                                     \
            ++spm_test::Failures();                                               \
        }                                                                         \
    } while (false)

#define REQUIRE(condition)                                                        \
    do {                                                                          \
        if (!(condition)) {                                                       \
            std::fprintf(stderr, "REQUIRE failed: %s (%s:%d)\n", #condition,      \
                         __FILE__, __LINE__);                                     \
            std::exit(1);                                                         \
        }                                                                         \
    } while (false)

#endif // SPM_TEST_CHECK_H
