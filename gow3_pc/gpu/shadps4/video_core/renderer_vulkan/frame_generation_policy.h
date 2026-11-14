// SPDX-License-Identifier: GPL-2.0-or-later
// Frame Generation & Multi-Frame Generation (MFG) Policy
#pragma once

#include <chrono>

namespace Vulkan::FrameGenerationPolicy {

/// Frame generation modes: off, FSR 2X, DLSS 2X to 6X, dynamic MFG.
enum Mode : int {
    FgOff = 0,
    FgFsr = 1,
    FgDlss2X = 2,
    FgDlss3X = 3,
    FgDlss4X = 4,
    FgDlss5X = 5,
    FgDlss6X = 6,
    FgDynamic = 7
};

enum class Skip { None, Off, NoUpscale, Reset, FullScreenPass, Failed };

struct Input {
    bool enabled;      ///< frame generation on and supported
    bool upscaled;     ///< the upscaler made this frame (not a menu, movie or loading screen)
    bool reset;        ///< upscaler reset, scene cut, preset or menu change
    float ui_coverage; ///< share of the frame drawn after the upscale, 0..1
    float frame_ms;    ///< real frame time
    bool failed;       ///< the context or a dispatch failed before
};

Skip Decide(const Input& in);

/// Number of generated frames between real frames (0 for off, 1 for 2X, 2 for 3X ... 5 for 6X).
int GeneratedFrames(int mode);

class Pacing {
public:
    using Clock = std::chrono::steady_clock;
    /// Called once per new real frame, before presenting either member of its pair (2X).
    Clock::time_point RealDeadline(Clock::time_point ready);

    /// Multi-frame deadline for index in 1..totalGenerated (2X..6X).
    Clock::time_point MultiFrameDeadline(Clock::time_point ready, int frameIndex, int totalGenerated);

private:
    Clock::time_point previous{};
    Clock::duration interval{};
    bool has_previous = false;
};

} // namespace Vulkan::FrameGenerationPolicy
