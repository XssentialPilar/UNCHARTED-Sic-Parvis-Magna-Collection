// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace Vulkan {
// x, y, width, height. Integer products avoid floating-point aspect drift at large sizes.
inline std::array<int32_t, 4> FitPresentation(int32_t source_width, int32_t source_height,
                                            int32_t width, int32_t height) {
    if (source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0) return {};
    int32_t fitted_width = width, fitted_height = height;
    if (int64_t(source_width) * height > int64_t(width) * source_height)
        fitted_height = int32_t(int64_t(width) * source_height / source_width);
    else
        fitted_width = int32_t(int64_t(height) * source_width / source_height);
    return {(width - fitted_width) / 2, (height - fitted_height) / 2, fitted_width, fitted_height};
}
} // namespace Vulkan
