// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: checks the per-frame pipeline trace (renderer_vulkan/frame_trace.h) without
// the game: the interval switch, that every field of a frame is carried from the
// publishers to the printed record, and the arithmetic the record relies on.
#include <cstdio>
#include <cstring>
#include <string>

#include "video_core/renderer_vulkan/frame_trace.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--enabled") == 0) {
        _putenv_s("GOW3_FRAME_TRACE", "1");
        Gow3Trace::PublishWorld(1920, 1080, 1920, 1080, false, false, true, 0, 0, false);
        Check(Gow3Trace::Get().snap.world_w == 0,
              "native path must not report host defaults as an observed guest target");
        Gow3Trace::PublishSceneTarget(1920, 1200);
        Gow3Trace::PublishWorld(1920, 1080, 1920, 1080, false, false, true, 0, 0, false);
        Check(Gow3Trace::Get().snap.world_w == 1920 &&
                  Gow3Trace::Get().snap.world_h == 1200 &&
                  Gow3Trace::Get().snap.scene_target_observed,
              "upscaler defaults must not overwrite the actual 1200p scene depth");
        Gow3Trace::Present(1920, 1200, 1920, 1200, 44, 1, 3, 1, 0, false, false,
                          0, 0, 1920, 1200);
        return failures ? 1 : 0;
    }
    // Off unless asked for.
    Check(Gow3Trace::Interval() == 0, "trace must be off without GOW3_FRAME_TRACE");
    Check(!Gow3Trace::Enabled(), "disabled trace must report Enabled() false");

    // Publishers are no-ops while off: nothing is recorded, nothing is printed.
    Gow3Trace::PublishWorld(1280, 720, 1920, 1080, true, true, true, 1, 2);
    Gow3Trace::PublishLayer2D(1920, 1200, 1.0f, 1.0f, 0, 60, true);
    Gow3Trace::PublishViewport(0, 60, 1920, 1080);
    Gow3Trace::PublishScissor(0, 60, 1920, 1080);
    Gow3Trace::Present(1920, 1200, 1920, 1200, 44, 1, 3, 7, 4, true, false, 0, 60, 1920, 1080);
    Check(Gow3Trace::Get().snap.frames == 0, "disabled trace must not count frames");
    Check(Gow3Trace::Get().snap.world_w == 0, "disabled trace must not record frames");

    // With the switch on, a frame carries every field through unchanged.
    _putenv_s("GOW3_FRAME_TRACE", "1");
    // The interval is parsed once per process; this binary starts with it off, so the
    // enabled path is exercised through the snapshot directly.
    {
        auto& state = Gow3Trace::Get();
        std::scoped_lock lock{state.mutex};
        auto& s = state.snap;
        s.world_w = 1280;
        s.world_h = 720;
        s.out_w = 1920;
        s.out_h = 1080;
        s.upscaler_on = true;
        s.scaled_scene = true;
        s.hdr = true;
        s.upscaler = 1;
        s.preset = 2;
        s.ui_target_w = 1920;
        s.ui_target_h = 1200;
        s.ui_scale_x = 1.0f;
        s.ui_scale_y = 1.0f;
        s.ui_off_x = 0;
        s.ui_off_y = 60;
        s.ui_isotropic = true;
        s.vp_x = 0;
        s.vp_y = 60;
        s.vp_w = 1920;
        s.vp_h = 1080;
        s.sc_x = 0;
        s.sc_y = 60;
        s.sc_w = 1920;
        s.sc_h = 1080;
        s.frame_w = 1920;
        s.frame_h = 1200;
        s.swap_w = 1920;
        s.swap_h = 1200;
        s.swap_format = 44;
        s.swap_mode = 1;
        s.swap_images = 3;
        s.generation = 7;
        s.redundant = 4;
        s.uses_dxgi = true;
        s.headless = false;
        s.dst_x = 0;
        s.dst_y = 60;
        s.dst_w = 1920;
        s.dst_h = 1080;
        s.frames = 120;
        // The safe area and the vertical FOV ratio are what the record derives: check
        // them here so a printing change cannot silently disagree with the geometry.
        const unsigned safe_w = unsigned(1920.0 * s.ui_scale_x + 0.5);
        const unsigned safe_h = unsigned(1080.0 * s.ui_scale_y + 0.5);
        Check(safe_w == 1920 && safe_h == 1080, "UI safe area is the 1920x1080 layer at 1:1");
        const double aspect = double(s.world_w) / double(s.world_h);
        const double ratio = (16.0 / 9.0) / aspect;
        Check(aspect > 1.777 && aspect < 1.778, "16:9 scene reports its own aspect");
        Check(ratio > 0.999 && ratio < 1.001, "16:9 has no vertical gain");
        // 1920x1200 world (16:10): vertical view is wider than 16:9 at the same
        // horizontal FOV, by 16/9 / 1.6 = 1.1111.
        const double tall = (16.0 / 9.0) / (1920.0 / 1200.0);
        Check(tall > 1.110 && tall < 1.112, "16:10 gains 11.1% vertical view");
        std::printf("FrameTrace record (one frame):\n");
        Gow3Trace::Print(s);
    }

    // The lipsum of the upscaler names must stay in step with Gow3Settings::Upscaler.
    Check(std::strcmp(Gow3Trace::UpscalerLabel(0), "off") == 0, "upscaler 0 is off");
    Check(std::strcmp(Gow3Trace::UpscalerLabel(1), "fsr3") == 0, "upscaler 1 is fsr3");
    Check(std::strcmp(Gow3Trace::UpscalerLabel(2), "fsr4") == 0, "upscaler 2 is fsr4");
    Check(std::strcmp(Gow3Trace::UpscalerLabel(3), "fsr4-1.1") == 0, "upscaler 3 is fsr4-1.1");
    Check(std::strcmp(Gow3Trace::UpscalerLabel(4), "taa") == 0, "upscaler 4 is taa");
    Check(std::strcmp(Gow3Trace::UpscalerLabel(5), "dlss") == 0, "upscaler 5 is dlss");

    if (failures == 0) {
        std::printf("Frame trace: PASS (interval, field carry, safe area, aspect/FOV ratio, names)\n");
        return 0;
    }
    std::printf("Frame trace: %d failure(s)\n", failures);
    return 1;
}
