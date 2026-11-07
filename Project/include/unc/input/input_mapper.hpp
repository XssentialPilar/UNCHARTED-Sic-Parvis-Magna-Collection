// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/input/input_mapper.hpp
// Uncharted: Sic Parvis Magna Collection — Stateless Input Coordinate Normalization & Axis Independence
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace unc::input {

struct ViewportBounds {
    double x{0.0};
    double y{0.0};
    double width{1280.0};
    double height{720.0};
};

struct NormalizedGameCoords {
    double xGame{0.0};
    double yGame{0.0};
};

struct UiAxisScrollState {
    double horizontalCarouselOffset{0.0};
    double verticalInventoryScroll{0.0};
};

class alignas(64) InputMapper {
public:
    // Section 5.2: Pure stateless transformation
    // X_game = (X_client - Viewport_X) * (Width_internal / Viewport_width)
    // Y_game = (Y_client - Viewport_Y) * (Height_internal / Viewport_height)
    // Proportional Aspect Ratio Scaling & Viewport calculation
    // Preserves the engine's base 16:9 aspect ratio (1.7777778) without stretching or geometric distortion.
    // On 16:10 (e.g. 1920x1200) or Ultrawide (21:9), centers the viewport symmetrically with uniform scale.
    [[nodiscard]] static constexpr ViewportBounds CalculateAspectCorrectViewport(
        double clientWidth, double clientHeight,
        double targetAspect = 16.0 / 9.0, double maxCropFraction = 0.0) noexcept {
        if (clientWidth <= 0.0 || clientHeight <= 0.0) {
            return ViewportBounds{0.0, 0.0, 1920.0, 1080.0};
        }
        const double clientAspect = clientWidth / clientHeight;

        if (std::abs(clientAspect - targetAspect) < 0.001) {
            return ViewportBounds{0.0, 0.0, clientWidth, clientHeight};
        }

        double vpWidth = clientWidth;
        double vpHeight = clientHeight;

        if (clientAspect > targetAspect) {
            // Wider than 16:9 (e.g. 21:9 Ultrawide): Pillarbox / symmetric margins
            vpWidth = clientHeight * targetAspect;
            if (maxCropFraction > 0.0) {
                const double excess = (clientWidth - vpWidth) * maxCropFraction;
                vpWidth = std::min(clientWidth, vpWidth + excess);
            }
        } else {
            // Taller than 16:9 (e.g. 16:10 like 1920x1200): Letterbox / symmetric top-bottom margins
            vpHeight = clientWidth / targetAspect;
            if (maxCropFraction > 0.0) {
                const double excess = (clientHeight - vpHeight) * maxCropFraction;
                vpHeight = std::min(clientHeight, vpHeight + excess);
            }
        }

        const double offsetX = (clientWidth - vpWidth) * 0.5;
        const double offsetY = (clientHeight - vpHeight) * 0.5;
        return ViewportBounds{offsetX, offsetY, vpWidth, vpHeight};
    }

    // Section 5.2: Pure stateless transformation with viewport compensation & boundary clamping
    // X_game = clamp((X_client - Viewport_X) * (Width_internal / Viewport_width), 0, Width_internal)
    // Y_game = clamp((Y_client - Viewport_Y) * (Height_internal / Viewport_height), 0, Height_internal)
    [[nodiscard]] static constexpr NormalizedGameCoords NormalizeClientCoordinates(
        double xClient, double yClient, const ViewportBounds& viewport,
        std::uint32_t widthInternal, std::uint32_t heightInternal) noexcept {
        if (viewport.width <= 0.0 || viewport.height <= 0.0 || widthInternal == 0U ||
            heightInternal == 0U) {
            return NormalizedGameCoords{0.0, 0.0};
        }
        const double scaleX = static_cast<double>(widthInternal) / viewport.width;
        const double scaleY = static_cast<double>(heightInternal) / viewport.height;
        const double rawX = (xClient - viewport.x) * scaleX;
        const double rawY = (yClient - viewport.y) * scaleY;
        const double clampedX = std::clamp(rawX, 0.0, static_cast<double>(widthInternal));
        const double clampedY = std::clamp(rawY, 0.0, static_cast<double>(heightInternal));
        return NormalizedGameCoords{clampedX, clampedY};
    }

    // Strict axis independence: horizontal carousel mutations NEVER read or write vertical scroll state
    static constexpr void ApplyHorizontalCarouselDelta(UiAxisScrollState& state,
                                                       double deltaX) noexcept {
        state.horizontalCarouselOffset += deltaX;
    }

    // Strict axis independence: vertical inventory mutations NEVER read or write horizontal carousel state
    static constexpr void ApplyVerticalInventoryDelta(UiAxisScrollState& state,
                                                      double deltaY) noexcept {
        state.verticalInventoryScroll += deltaY;
    }
};

} // namespace unc::input
