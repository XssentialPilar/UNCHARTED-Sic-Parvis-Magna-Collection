// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Explorer's Desk Theme
#include "ui_theme.hpp"

namespace spm::ui {

void ApplyExplorersDeskTheme(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();

    // Scale sizing by DPI factor
    style.WindowPadding     = ImVec2(16.0f * dpiScale, 16.0f * dpiScale);
    style.FramePadding      = ImVec2(10.0f * dpiScale, 6.0f * dpiScale);
    style.ItemSpacing       = ImVec2(12.0f * dpiScale, 10.0f * dpiScale);
    style.ItemInnerSpacing  = ImVec2(8.0f * dpiScale, 6.0f * dpiScale);
    style.IndentSpacing     = 20.0f * dpiScale;
    style.ScrollbarSize     = 14.0f * dpiScale;
    style.GrabMinSize       = 14.0f * dpiScale;

    // Corner roundings
    style.WindowRounding    = 4.0f * dpiScale;
    style.ChildRounding     = 4.0f * dpiScale;
    style.FrameRounding     = 3.0f * dpiScale;
    style.PopupRounding     = 4.0f * dpiScale;
    style.ScrollbarRounding = 6.0f * dpiScale;
    style.GrabRounding      = 3.0f * dpiScale;
    style.TabRounding       = 4.0f * dpiScale;

    // Borders
    style.WindowBorderSize  = 1.0f * dpiScale;
    style.ChildBorderSize   = 1.0f * dpiScale;
    style.PopupBorderSize   = 1.0f * dpiScale;
    style.FrameBorderSize   = 1.0f * dpiScale;
    style.TabBorderSize     = 1.0f * dpiScale;

    // Palette Colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = Palette::ParchmentText;
    colors[ImGuiCol_TextDisabled]          = Palette::TextMuted;
    colors[ImGuiCol_WindowBg]              = Palette::SlateBg;
    colors[ImGuiCol_ChildBg]               = Palette::PanelBg;
    colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.09f, 0.10f, 0.98f);
    colors[ImGuiCol_Border]                = Palette::CardBorder;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_FrameBg]               = Palette::CardBg;
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.20f, 0.23f, 0.90f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.22f, 0.24f, 0.28f, 0.95f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.05f, 0.06f, 0.07f, 1.0f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.07f, 0.08f, 0.09f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.05f, 0.06f, 0.07f, 0.8f);
    colors[ImGuiCol_MenuBarBg]             = Palette::PanelBg;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.05f, 0.06f, 0.6f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.28f, 0.24f, 0.8f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.50f, 0.44f, 0.30f, 0.9f);
    colors[ImGuiCol_ScrollbarGrabActive]   = Palette::GoldPrimary;
    colors[ImGuiCol_CheckMark]             = Palette::GoldPrimary;
    colors[ImGuiCol_SliderGrab]            = Palette::GoldPrimary;
    colors[ImGuiCol_SliderGrabActive]      = Palette::GoldActive;
    colors[ImGuiCol_Button]                = ImVec4(0.16f, 0.17f, 0.20f, 0.90f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.25f, 0.24f, 0.20f, 0.95f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.35f, 0.32f, 0.20f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.20f, 0.19f, 0.16f, 0.80f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.30f, 0.27f, 0.18f, 0.90f);
    colors[ImGuiCol_HeaderActive]          = Palette::GoldPrimary;
    colors[ImGuiCol_Separator]             = Palette::CardBorder;
    colors[ImGuiCol_SeparatorHovered]      = Palette::GoldBorder;
    colors[ImGuiCol_SeparatorActive]       = Palette::GoldPrimary;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.20f, 0.20f, 0.20f, 0.5f);
    colors[ImGuiCol_ResizeGripHovered]     = Palette::GoldBorder;
    colors[ImGuiCol_ResizeGripActive]      = Palette::GoldPrimary;
    colors[ImGuiCol_Tab]                   = ImVec4(0.11f, 0.12f, 0.14f, 0.90f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.22f, 0.21f, 0.18f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.25f, 0.22f, 0.15f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.08f, 0.09f, 0.10f, 0.90f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_PlotLines]             = Palette::GoldPrimary;
    colors[ImGuiCol_PlotLinesHovered]      = Palette::GoldHover;
    colors[ImGuiCol_PlotHistogram]         = Palette::GoldPrimary;
    colors[ImGuiCol_PlotHistogramHovered]  = Palette::GoldHover;
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.14f, 0.15f, 0.17f, 1.0f);
    colors[ImGuiCol_TableBorderStrong]     = Palette::CardBorder;
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.19f, 0.21f, 0.6f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
    colors[ImGuiCol_NavHighlight]          = Palette::GoldPrimary;
}

} // namespace spm::ui
