// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Explorer's Desk Theme
#ifndef SPM_UI_THEME_HPP
#define SPM_UI_THEME_HPP

#include "imgui.h"

namespace spm::ui {

// Palette constants: Uncharted relic / antique explorer style
struct Palette {
    static constexpr ImVec4 GoldPrimary     = ImVec4(0.831f, 0.686f, 0.216f, 1.0f); // #D4AF37
    static constexpr ImVec4 GoldHover       = ImVec4(0.933f, 0.784f, 0.314f, 1.0f);
    static constexpr ImVec4 GoldActive      = ImVec4(0.702f, 0.580f, 0.180f, 1.0f);
    static constexpr ImVec4 GoldBorder      = ImVec4(0.550f, 0.450f, 0.180f, 0.85f);
    static constexpr ImVec4 SlateBg         = ImVec4(0.063f, 0.071f, 0.082f, 1.0f); // Deep dark slate #101215
    static constexpr ImVec4 PanelBg         = ImVec4(0.098f, 0.110f, 0.125f, 0.94f);
    static constexpr ImVec4 CardBg          = ImVec4(0.125f, 0.141f, 0.161f, 0.90f);
    static constexpr ImVec4 CardBorder      = ImVec4(0.251f, 0.231f, 0.188f, 0.60f);
    static constexpr ImVec4 ParchmentText   = ImVec4(0.922f, 0.898f, 0.839f, 1.0f); // #EBE5D6
    static constexpr ImVec4 TextMuted       = ImVec4(0.600f, 0.580f, 0.541f, 1.0f);
    static constexpr ImVec4 SuccessGreen    = ImVec4(0.298f, 0.733f, 0.388f, 1.0f);
    static constexpr ImVec4 WarningOrange   = ImVec4(0.902f, 0.600f, 0.133f, 1.0f);
    static constexpr ImVec4 ErrorRed        = ImVec4(0.863f, 0.247f, 0.247f, 1.0f);
};

void ApplyExplorersDeskTheme(float dpiScale = 1.0f);

} // namespace spm::ui

#endif // SPM_UI_THEME_HPP
