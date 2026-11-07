// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — SDL3 Input & Relative Mouse Capture Bridge
#pragma once

#include <cstdint>

namespace unc::vendor::sdl3 {

enum class SdlGamepadBackend : uint8_t {
    Auto = 0,
    XInput = 1,
    RawInput = 2,
    HidApi = 3,
};

struct SdlMouseCaptureConfig {
    bool require_window_focus{true};
    bool require_relative_confirmation{true};
    double default_yaw_sensitivity{1.0};
    double default_pitch_sensitivity{1.0};
    bool invert_y{false};
};

} // namespace unc::vendor::sdl3
