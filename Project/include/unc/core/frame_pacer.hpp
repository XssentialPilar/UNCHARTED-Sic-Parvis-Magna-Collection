// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/core/frame_pacer.hpp
// Uncharted: Sic Parvis Magna Collection — Sub-Millisecond Hybrid Frame Pacer (QPC + HighRes WaitableTimer + _mm_pause)
#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
using HANDLE = void*;
#endif

namespace unc::core {

class alignas(64) FramePacer {
public:
    FramePacer() noexcept = default;
    ~FramePacer() noexcept { Shutdown(); }

    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    bool Initialize(double targetFps = 60.0) noexcept;
    void Shutdown() noexcept;

    void BeginFrame() noexcept;
    // Hybrid wait: High-Resolution Waitable Timer (> 1.5 ms remaining) + _mm_pause() spin-lock (last 0.35 ms)
    void WaitForTargetFrameTime() noexcept;

    void SetTargetFps(double targetFps) noexcept;
    [[nodiscard]] double GetLastFrameTimeMs() const noexcept { return m_lastFrameTimeMs; }
    [[nodiscard]] double GetLastJitterMs() const noexcept { return m_lastJitterMs; }
    [[nodiscard]] double GetTargetFrameTimeMs() const noexcept { return m_targetFrameTimeMs; }

private:
    HANDLE       m_waitableTimer{nullptr};
    std::int64_t m_qpcFrequency{0};
    std::int64_t m_frameStartTicks{0};
    std::int64_t m_targetFrameTicks{0};
    double       m_targetFrameTimeMs{16.6666667};
    double       m_lastFrameTimeMs{16.6666667};
    double       m_lastJitterMs{0.0};
};

} // namespace unc::core
