// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/input/input_mapper.hpp
// Uncharted: Sic Parvis Magna Collection — Stateless Input Coordinate Normalization & Axis Independence
#pragma once

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
    [[nodiscard]] static constexpr NormalizedGameCoords NormalizeClientCoordinates(
        double xClient, double yClient, const ViewportBounds& viewport,
        std::uint32_t widthInternal, std::uint32_t heightInternal) noexcept {
        if (viewport.width <= 0.0 || viewport.height <= 0.0 || widthInternal == 0U ||
            heightInternal == 0U) {
            return NormalizedGameCoords{0.0, 0.0};
        }
        const double scaleX = static_cast<double>(widthInternal) / viewport.width;
        const double scaleY = static_cast<double>(heightInternal) / viewport.height;
        return NormalizedGameCoords{
            (xClient - viewport.x) * scaleX,
            (yClient - viewport.y) * scaleY,
        };
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
