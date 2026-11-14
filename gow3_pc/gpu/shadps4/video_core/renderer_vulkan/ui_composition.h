// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Vulkan::UiComposition {

enum class Background { None, Copy, Temporal };

// UI composition must not require scene depth/camera or an active FSR context.
constexpr Background Choose(bool scaled, bool ui_draw, bool scene_ready, bool fsr_active) {
    if (!scaled || !ui_draw) {
        return Background::None;
    }
    return scene_ready && fsr_active ? Background::Temporal : Background::Copy;
}

inline bool NativeViewport(float width, float height) {
    return std::abs(std::abs(width) - 1920.0f) < 0.5f &&
           std::abs(std::abs(height) - 1080.0f) < 0.5f;
}

// Scaleform draws, including the first stencil/movie pass. A native-size viewport
// alone also matches every fullscreen post pass when guest targets stay at 1080p.
constexpr bool MovieShader(uint64_t hash) {
    return hash == 0x34e8a281 || hash == 0x81d336ce || hash == 0x09957251 ||
           hash == 0x24042a9b || hash == 0xa400228b;
}

inline std::array<float, 2> Scale(uint32_t guest_width, uint32_t guest_height,
                                uint32_t output_width, uint32_t output_height,
                                bool native_coordinates) {
    return {float(output_width) / float(native_coordinates ? 1920 : guest_width),
            float(output_height) / float(native_coordinates ? 1080 : guest_height)};
}

// Placement of a guest coordinate space in the output-size image: one scale for both axes and
// an offset in target pixels. The UI is never stretched by the ratio between the two aspects.
struct Layout {
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

// Native UI coordinates name a 1920x1080 space even when the output is taller (16:10, 4:3) or
// wider (ultrawide) than 16:9. Fitting that space anisotropically, as Scale() does, stretched
// the HUD by output_height/1080 (11.1% at 1920x1200). Fit it uniformly instead and centre the
// leftover, so the HUD keeps its 16:9 geometry and only gains bars. Guest images already at
// the output size are still mapped to the whole target: that path is the upscaler's own
// composition and its source and destination share the aspect.
inline Layout FitLayout(uint32_t guest_width, uint32_t guest_height, uint32_t output_width,
                        uint32_t output_height, bool native_coordinates) {
    if (output_width == 0 || output_height == 0) {
        return {};
    }
    if (!native_coordinates) {
        return {float(output_width) / float(std::max(guest_width, 1u)),
                float(output_height) / float(std::max(guest_height, 1u)), 0.0f, 0.0f};
    }
    constexpr float space_width = 1920.0f;
    constexpr float space_height = 1080.0f;
    const float scale = std::min(float(output_width) / space_width,
                                 float(output_height) / space_height);
    return {scale, scale,
            (float(output_width) - space_width * scale) * 0.5f,
            (float(output_height) - space_height * scale) * 0.5f};
}

// ConvertPositionToClipSpace divides window coordinates by half the *virtual* viewport,
// and UpdateViewportScissorState binds that same viewport (up to 16384 pixels). The
// viewport transform cancels that division, so the push-constant offset is already in
// pixels. Dividing by the attachment extent again moved a 60px inset to 819.2px at 1200p.
inline float ClipSpaceOffset(float offset_pixels, uint32_t target_extent) {
    return target_extent ? offset_pixels : 0.0f;
}

} // namespace Vulkan::UiComposition
