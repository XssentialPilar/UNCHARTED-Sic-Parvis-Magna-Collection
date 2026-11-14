// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/frame_generation_policy.h"

namespace Vulkan::FrameGenerationPolicy {

// Above this share of changed pixels a pass after the HUD rewrote the frame (e.g. a full-screen
// post-effect): interpolating it would smear the HUD into the scene.
constexpr float kMaxUiCoverage = 0.30f;

Skip Decide(const Input& in) {
    if (!in.enabled) return Skip::Off;
    if (in.failed) return Skip::Failed;
    if (!in.upscaled) return Skip::NoUpscale;
    if (in.reset) return Skip::Reset;
    if (in.ui_coverage > kMaxUiCoverage) return Skip::FullScreenPass;
    return Skip::None;
}

int GeneratedFrames(int mode) {
    switch (mode) {
    case FgFsr:
    case FgDlss2X:
        return 1; // 2X presentation (1 generated + 1 real)
    case FgDlss3X:
        return 2; // 3X presentation (2 generated + 1 real)
    case FgDlss4X:
        return 3; // 4X presentation (3 generated + 1 real)
    case FgDlss5X:
        return 4; // 5X presentation (4 generated + 1 real)
    case FgDlss6X:
        return 5; // 6X presentation (5 generated + 1 real)
    case FgDynamic:
        return 1; // baseline dynamic 2X
    default:
        return 0; // Off or invalid
    }
}

Pacing::Clock::time_point Pacing::RealDeadline(Clock::time_point ready) {
    // Menus, pauses and clock discontinuities must not carry a long wait into gameplay.
    if (!has_previous || ready <= previous || ready - previous > std::chrono::milliseconds(250)) {
        interval = Clock::duration::zero();
        has_previous = true;
    } else {
        const auto sample = ready - previous;
        if (interval == Clock::duration::zero() || sample < interval) {
            interval = sample;
        } else {
            interval += (sample - interval) / 10;
        }
    }
    previous = ready;
    return ready + interval / 2;
}

Pacing::Clock::time_point Pacing::MultiFrameDeadline(Clock::time_point ready, int frameIndex, int totalGenerated) {
    if (totalGenerated <= 1) {
        return RealDeadline(ready);
    }
    if (ready > previous) {
        if (!has_previous || ready - previous > std::chrono::milliseconds(250)) {
            interval = Clock::duration::zero();
            has_previous = true;
        } else {
            const auto sample = ready - previous;
            if (interval == Clock::duration::zero() || sample < interval) {
                interval = sample;
            } else {
                interval += (sample - interval) / 10;
            }
        }
        previous = ready;
    }
    return ready + (interval * frameIndex) / (totalGenerated + 1);
}

} // namespace Vulkan::FrameGenerationPolicy
