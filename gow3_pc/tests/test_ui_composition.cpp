// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "gpu/shadps4/video_core/renderer_vulkan/ui_composition.h"
#include "gpu/shadps4/video_core/renderer_vulkan/presentation_geometry.h"

int main() {
    using Vulkan::FitPresentation;
    assert((FitPresentation(1920, 1200, 1920, 1200) == std::array<int32_t, 4>{0, 0, 1920, 1200}));
    assert((FitPresentation(1920, 1080, 1920, 1200) == std::array<int32_t, 4>{0, 60, 1920, 1080}));
    assert((FitPresentation(1920, 1200, 1920, 1080) == std::array<int32_t, 4>{96, 0, 1728, 1080}));
    assert((FitPresentation(1920, 1200, 1632, 918) == std::array<int32_t, 4>{82, 0, 1468, 918}));
    assert((FitPresentation(0, 1080, 1920, 1200) == std::array<int32_t, 4>{}));
    using namespace Vulkan::UiComposition;
    // Title/loading menus have no depth/camera. UI must still go to the native target.
    assert(Choose(true, true, false, true) == Background::Copy);
    assert(Choose(true, true, false, false) == Background::Copy);
    // In-game HUD: first reconstruct the scene, then draw the native UI over it.
    assert(Choose(true, true, true, true) == Background::Temporal);
    // Runtime FSR disable/failure must not change UI resolution.
    assert(Choose(true, true, true, false) == Background::Copy);
    assert(Choose(true, false, true, true) == Background::None);
    assert(Choose(false, true, true, true) == Background::None);
    assert(NativeViewport(1920, -1080));
    assert(!NativeViewport(960, -540));
    assert(MovieShader(0x34e8a281) && MovieShader(0x24042a9b));
    assert(!MovieShader(0x0b0acf50)); // fullscreen post/tonemap also has a native viewport
    assert(!MovieShader(0x6c62a79f)); // shadow geometry
    const auto display = Scale(960, 540, 1920, 1080, false);
    assert(display[0] == 2 && display[1] == 2);
    const auto ui = Scale(960, 540, 1920, 1080, true);
    assert(ui[0] == 1 && ui[1] == 1); // don't double an already native UI viewport
    const auto larger = Scale(960, 540, 3840, 2160, true);
    assert(larger[0] == 2 && larger[1] == 2);
    // Native UI coordinates are a 1920x1080 space: the HUD must keep its geometry when the
    // output has another aspect, with centred bars instead of an anisotropic stretch.
    const auto same = FitLayout(1920, 1080, 1920, 1080, true);
    assert(same.scale_x == 1 && same.scale_y == 1 && same.offset_x == 0 && same.offset_y == 0);
    const auto uhd = FitLayout(1920, 1080, 3840, 2160, true);
    assert(uhd.scale_x == 2 && uhd.scale_y == 2 && uhd.offset_x == 0 && uhd.offset_y == 0);
    const auto tall = FitLayout(1920, 1080, 1920, 1200, true);
    assert(tall.scale_x == 1 && tall.scale_y == 1 && tall.offset_x == 0 && tall.offset_y == 60);
    const auto wide = FitLayout(1920, 1080, 2560, 1080, true);
    assert(wide.scale_x == 1 && wide.scale_y == 1 && wide.offset_x == 320 && wide.offset_y == 0);
    const auto fourthree = FitLayout(1920, 1080, 1600, 1200, true);
    assert(fourthree.scale_x == fourthree.scale_y);
    assert(std::abs(fourthree.offset_x) < 0.01f);
    assert(std::abs(fourthree.offset_y - (1200.0f - 1080.0f * fourthree.scale_y) * 0.5f) <
           0.01f);
    // A guest image already at another size is still mapped to the whole target (upscaler
    // composition); only the native-coordinate path changes.
    const auto guest = FitLayout(1920, 1080, 1920, 1200, false);
    assert(guest.scale_x == 1 && guest.scale_y > 1.11f && guest.scale_y < 1.12f);
    assert(guest.offset_x == 0 && guest.offset_y == 0);
    const auto empty = FitLayout(1920, 1080, 0, 0, true);
    assert(empty.scale_x == 1 && empty.scale_y == 1);
    // Exercise the shader conversion AND the actual fixed viewport, rather than only
    // asserting a helper's return value. The attachment size is not the virtual viewport.
    const auto screen_pixel = [](float pixel, float scale, float inset, uint32_t extent,
                                 float virtual_viewport) {
        const float window = pixel * scale + ClipSpaceOffset(inset, extent);
        const float ndc = window / (virtual_viewport * 0.5f) - 1.0f;
        return (ndc + 1.0f) * virtual_viewport * 0.5f;
    };
    for (const float viewport : {4096.0f, 16384.0f}) {
        assert(std::abs(screen_pixel(0, tall.scale_y, tall.offset_y, 1200, viewport) - 60) <
               0.01f);
        assert(std::abs(screen_pixel(1080, tall.scale_y, tall.offset_y, 1200, viewport) - 1140) <
               0.01f);
        assert(std::abs(screen_pixel(0, wide.scale_x, wide.offset_x, 2560, viewport) - 320) <
               0.01f);
        assert(std::abs(screen_pixel(1920, wide.scale_x, wide.offset_x, 2560, viewport) - 2240) <
               0.01f);
    }
    assert(ClipSpaceOffset(60, 0) == 0);
    std::puts("UI composition: PASS (menu without camera, HUD, FSR off, viewport scaling)");
}
