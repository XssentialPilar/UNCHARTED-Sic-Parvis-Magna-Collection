// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — DirectX 12 Agility SDK Configuration Header
#pragma once

#include <cstdint>

namespace unc::vendor::d3d12 {

inline constexpr uint32_t kAgilitySdkVersion = 614U;
inline constexpr const char* kAgilitySdkPath = ".\\D3D12\\";

struct AgilitySdkConfig {
    uint32_t sdk_version{kAgilitySdkVersion};
    const char* sdk_path{kAgilitySdkPath};
    bool allow_feature_level_11_0_fallback{true};
};

} // namespace unc::vendor::d3d12
