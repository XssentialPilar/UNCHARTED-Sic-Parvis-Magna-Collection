// SPDX-License-Identifier: GPL-2.0-or-later
// native/core/frame_pacer.cpp
// Uncharted: Sic Parvis Magna Collection — Sub-Millisecond Hybrid Frame Pacer (QPC + HighRes WaitableTimer + _mm_pause)

#include "unc/core/frame_pacer.hpp"

#include <cmath>

#if defined(_WIN32) || defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace unc::core {

bool FramePacer::Initialize(double targetFps) noexcept {
    Shutdown();
#ifdef _WIN32
    LARGE_INTEGER freq{};
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) {
        return false;
    }
    m_qpcFrequency = freq.QuadPart;
    m_waitableTimer = CreateWaitableTimerExW(nullptr, nullptr,
                                             CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                             TIMER_ALL_ACCESS);
    if (m_waitableTimer == nullptr) {
        m_waitableTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    m_frameStartTicks = now.QuadPart;
#else
    m_qpcFrequency = 1000000000LL;
    m_frameStartTicks = 0;
#endif
    SetTargetFps(targetFps);
    return true;
}

void FramePacer::Shutdown() noexcept {
#ifdef _WIN32
    if (m_waitableTimer != nullptr) {
        CloseHandle(m_waitableTimer);
        m_waitableTimer = nullptr;
    }
#endif
}

void FramePacer::SetTargetFps(double targetFps) noexcept {
    const double clampedFps = (targetFps < 15.0) ? 60.0 : ((targetFps > 240.0) ? 240.0 : targetFps);
    m_targetFrameTimeMs = 1000.0 / clampedFps;
    if (m_qpcFrequency > 0) {
        m_targetFrameTicks =
            static_cast<std::int64_t>((static_cast<double>(m_qpcFrequency) / clampedFps) + 0.5);
    }
}

void FramePacer::BeginFrame() noexcept {
#ifdef _WIN32
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    m_frameStartTicks = now.QuadPart;
#endif
}

void FramePacer::WaitForTargetFrameTime() noexcept {
#ifdef _WIN32
    if (m_qpcFrequency <= 0 || m_targetFrameTicks <= 0) {
        return;
    }
    const std::int64_t targetTick = m_frameStartTicks + m_targetFrameTicks;
    const std::int64_t timerThresholdTicks = (m_qpcFrequency * 15LL) / 10000LL; // 1.50 ms
    const std::int64_t spinReserveTicks = (m_qpcFrequency * 35LL) / 100000LL;   // 0.35 ms

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const std::int64_t remainingTicks = targetTick - now.QuadPart;

    // Step 3: If > 1.5 ms remains, wait using the High-Resolution Kernel Waitable Timer
    if (remainingTicks > timerThresholdTicks && m_waitableTimer != nullptr) {
        const std::int64_t sleepTicks = remainingTicks - spinReserveTicks;
        if (sleepTicks > 0) {
            LARGE_INTEGER dueTime{};
            dueTime.QuadPart = -static_cast<LONGLONG>((sleepTicks * 10000000LL) / m_qpcFrequency);
            if (dueTime.QuadPart < 0 &&
                SetWaitableTimer(m_waitableTimer, &dueTime, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(m_waitableTimer, 15U);
            }
        }
    }

    // Step 4: Spin-wait for the final <= 0.35 ms using _mm_pause() to hit the exact presentation tick
    for (;;) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= targetTick) {
            break;
        }
#if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#endif
    }

    const double elapsedMs =
        (static_cast<double>(now.QuadPart - m_frameStartTicks) * 1000.0) /
        static_cast<double>(m_qpcFrequency);
    m_lastJitterMs = std::abs(elapsedMs - m_lastFrameTimeMs);
    m_lastFrameTimeMs = elapsedMs;
    m_frameStartTicks = now.QuadPart;
#else
    m_lastJitterMs = 0.0;
    m_lastFrameTimeMs = m_targetFrameTimeMs;
#endif
}

} // namespace unc::core
