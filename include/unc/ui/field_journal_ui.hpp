// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/ui/field_journal_ui.hpp
// Uncharted: Sic Parvis Magna Collection — Decoupled 1:1 Orthographic UI Projection & Aspect Ratio Anchors
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "unc/input/input_mapper.hpp"

namespace unc::ui {

enum class UiAnchor : std::uint8_t {
    TopLeft = 0,
    TopCenter = 1,
    TopRight = 2,
    CenterLeft = 3,
    Center = 4,
    CenterRight = 5,
    BottomLeft = 6,
    BottomCenter = 7,
    BottomRight = 8,
};

struct UiRect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

struct OrthoMatrix4x4 {
    std::array<float, 16> m{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

class alignas(64) FieldJournalUI {
public:
    static constexpr float kVirtualCanvasWidth = 1920.0f;
    static constexpr float kVirtualCanvasHeight = 1080.0f;
    static constexpr float kBaseAspectRatio = 16.0f / 9.0f; // 1.7777778

    FieldJournalUI() noexcept = default;

    // Builds a decoupled 1:1 orthographic projection matrix for UI elements
    // so brass frames, cards, and buttons preserve exact 1:1 geometric proportion
    // without stretching across 16:9, 16:10, or Ultrawide viewports.
    [[nodiscard]] static constexpr OrthoMatrix4x4 BuildOrthoProjection(
        const input::ViewportBounds& vp) noexcept {
        const float left = 0.0f;
        const float right = static_cast<float>(vp.width > 0.0 ? vp.width : kVirtualCanvasWidth);
        const float bottom = static_cast<float>(vp.height > 0.0 ? vp.height : kVirtualCanvasHeight);
        const float top = 0.0f;
        const float nearZ = 0.0f;
        const float farZ = 1.0f;

        const float rml = right - left;
        const float bmt = bottom - top;
        const float fmn = farZ - nearZ;

        OrthoMatrix4x4 out{};
        out.m[0] = 2.0f / (rml > 0.0f ? rml : 1.0f);
        out.m[5] = -2.0f / (bmt > 0.0f ? bmt : 1.0f); // Inverted Y for screen-space
        out.m[10] = 1.0f / (fmn > 0.0f ? fmn : 1.0f);
        out.m[12] = -(right + left) / (rml > 0.0f ? rml : 1.0f);
        out.m[13] = (bottom + top) / (bmt > 0.0f ? bmt : 1.0f);
        out.m[14] = -nearZ / (fmn > 0.0f ? fmn : 1.0f);
        out.m[15] = 1.0f;
        return out;
    }

    // Resolves anchored element screen-space rect preserving 1:1 aspect ratio inside viewport
    [[nodiscard]] static constexpr UiRect ResolveAnchoredRect(
        UiAnchor anchor, float baseWidth, float baseHeight,
        const input::ViewportBounds& vp, float scaleFactor = 1.0f) noexcept {
        const float uniformScale = static_cast<float>(
            std::min(vp.width / kVirtualCanvasWidth, vp.height / kVirtualCanvasHeight)) * scaleFactor;
        const float scaledW = baseWidth * uniformScale;
        const float scaledH = baseHeight * uniformScale;

        const float vpX = static_cast<float>(vp.x);
        const float vpY = static_cast<float>(vp.y);
        const float vpW = static_cast<float>(vp.width);
        const float vpH = static_cast<float>(vp.height);

        float outX = vpX;
        float outY = vpY;

        switch (anchor) {
        case UiAnchor::TopLeft:
            outX = vpX;
            outY = vpY;
            break;
        case UiAnchor::TopCenter:
            outX = vpX + (vpW - scaledW) * 0.5f;
            outY = vpY;
            break;
        case UiAnchor::TopRight:
            outX = vpX + vpW - scaledW;
            outY = vpY;
            break;
        case UiAnchor::CenterLeft:
            outX = vpX;
            outY = vpY + (vpH - scaledH) * 0.5f;
            break;
        case UiAnchor::Center:
            outX = vpX + (vpW - scaledW) * 0.5f;
            outY = vpY + (vpH - scaledH) * 0.5f;
            break;
        case UiAnchor::CenterRight:
            outX = vpX + vpW - scaledW;
            outY = vpY + (vpH - scaledH) * 0.5f;
            break;
        case UiAnchor::BottomLeft:
            outX = vpX;
            outY = vpY + vpH - scaledH;
            break;
        case UiAnchor::BottomCenter:
            outX = vpX + (vpW - scaledW) * 0.5f;
            outY = vpY + vpH - scaledH;
            break;
        case UiAnchor::BottomRight:
            outX = vpX + vpW - scaledW;
            outY = vpY + vpH - scaledH;
            break;
        }

        return UiRect{outX, outY, scaledW, scaledH};
    }
};

} // namespace unc::ui
