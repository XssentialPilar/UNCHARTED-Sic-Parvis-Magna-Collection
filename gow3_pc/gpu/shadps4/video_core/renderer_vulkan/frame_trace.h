// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: per-frame pipeline trace.
//
// The v0.6.1 specification asks for the complete path of a frame: internal logical
// size, initial render target, world (scene) resolution, aspect and FOV policy,
// post-processing, upscaler resolution, viewport and scissor, the 2D layer, the UI
// safe area, the swapchain, and the final presentation. Every value here is measured
// in the running frame; nothing is derived from a preset table or assumed.
//
// Off by default. GOW3_FRAME_TRACE=<n> prints one block every <n> presented frames
// (GOW3_FRAME_TRACE=1 prints all of them, GOW3_FRAME_TRACE=0 or unset: nothing).
// The cost while off is one relaxed atomic load per publish.

#pragma once

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace Gow3Trace {

enum class UpscalerName : int {
    Off = 0,
    Fsr3 = 1,
    Fsr4 = 2,
    Fsr411 = 3,
    Taa = 4,
    Dlss = 5,
};

inline const char* UpscalerLabel(int upscaler) {
    switch (upscaler) {
    case 0: return "off";
    case 1: return "fsr3";
    case 2: return "fsr4";
    case 3: return "fsr4-1.1";
    case 4: return "taa";
    case 5: return "dlss";
    default: return "unknown";
    }
}

struct Snapshot {
    // World: the scene the renderer draws before any upscale.
    unsigned world_w = 0, world_h = 0;
    bool scene_target_observed = false;
    bool upscaler_on = false;
    bool scaled_scene = false;
    bool hdr = true;
    int upscaler = 0;
    int preset = -1;
    // The upscaler's output image (the display size inside the runtime).
    unsigned out_w = 0, out_h = 0;
    // The image the 2D layer is composited into, and its uniform placement.
    unsigned ui_target_w = 0, ui_target_h = 0;
    float ui_scale_x = 1.0f, ui_scale_y = 1.0f;
    int ui_off_x = 0, ui_off_y = 0;
    bool ui_isotropic = false;
    // The last draw's viewport (pixels of its target) and scissor.
    bool have_viewport = false;
    float vp_x = 0, vp_y = 0, vp_w = 0, vp_h = 0;
    long sc_x = 0, sc_y = 0;
    unsigned sc_w = 0, sc_h = 0;
    // Presentation.
    unsigned frame_w = 0, frame_h = 0;
    unsigned swap_w = 0, swap_h = 0;
    int swap_format = 0, swap_mode = 0, swap_images = 0;
    unsigned long long generation = 0, redundant = 0;
    bool uses_dxgi = false, headless = false;
    int dst_x = 0, dst_y = 0;
    unsigned dst_w = 0, dst_h = 0;
    unsigned long long frames = 0;
};

inline int Interval() {
    static const int interval = [] {
        const char* env = std::getenv("GOW3_FRAME_TRACE");
        if (!env || env[0] == '\0' || env[0] == '0') {
            return 0;
        }
        const int n = std::atoi(env);
        return n > 0 ? n : 1;
    }();
    return interval;
}

inline bool Enabled() {
    return Interval() > 0;
}

struct State {
    std::mutex mutex;
    Snapshot snap;
};

inline State& Get() {
    static State state;
    return state;
}

/// The scene the renderer draws: the world resolution and the upscaler it feeds.
inline void PublishWorld(unsigned world_w, unsigned world_h, unsigned out_w, unsigned out_h,
                         bool upscaler_on, bool scaled, bool hdr, int upscaler, int preset,
                         bool host_scene_size = true) {
    if (!Enabled()) {
        return;
    }
    auto& state = Get();
    std::scoped_lock lock{state.mutex};
    auto& s = state.snap;
    // The native Uncharted path bypasses scene_targets: its defaults are not evidence
    // of the guest's actual resolution. That path publishes the observed scene depth.
    if (host_scene_size) {
        s.world_w = world_w;
        s.world_h = world_h;
        s.scene_target_observed = false;
    }
    s.out_w = out_w;
    s.out_h = out_h;
    s.upscaler_on = upscaler_on;
    s.scaled_scene = scaled;
    s.hdr = hdr;
    s.upscaler = upscaler;
    s.preset = preset;
}

inline void PublishSceneTarget(unsigned width, unsigned height) {
    if (!Enabled()) {
        return;
    }
    auto& state = Get();
    std::scoped_lock lock{state.mutex};
    state.snap.world_w = width;
    state.snap.world_h = height;
    state.snap.scene_target_observed = true;
}

/// The 2D layer: its target, the uniform scale applied to the 1920x1080 UI space and
/// the centred offset that keeps it undistorted (the UI safe area starts here).
inline void PublishLayer2D(unsigned target_w, unsigned target_h, float scale_x, float scale_y,
                           int off_x, int off_y, bool isotropic) {
    if (!Enabled()) {
        return;
    }
    auto& state = Get();
    std::scoped_lock lock{state.mutex};
    auto& s = state.snap;
    s.ui_target_w = target_w;
    s.ui_target_h = target_h;
    s.ui_scale_x = scale_x;
    s.ui_scale_y = scale_y;
    s.ui_off_x = off_x;
    s.ui_off_y = off_y;
    s.ui_isotropic = isotropic;
}

inline void PublishViewport(float x, float y, float w, float h) {
    if (!Enabled()) {
        return;
    }
    auto& state = Get();
    std::scoped_lock lock{state.mutex};
    auto& s = state.snap;
    s.vp_x = x;
    s.vp_y = y;
    s.vp_w = w;
    s.vp_h = h;
    s.have_viewport = true;
}

inline void PublishScissor(long x, long y, unsigned w, unsigned h) {
    if (!Enabled()) {
        return;
    }
    auto& state = Get();
    std::scoped_lock lock{state.mutex};
    auto& s = state.snap;
    s.sc_x = x;
    s.sc_y = y;
    s.sc_w = w;
    s.sc_h = h;
}

inline void Print(const Snapshot& s) {
    const double aspect_world =
        s.world_h ? double(s.world_w) / double(s.world_h) : 0.0;
    const double aspect_swap = s.swap_h ? double(s.swap_w) / double(s.swap_h) : 0.0;
    // This is a geometric ratio under a fixed horizontal-FOV assumption, not a
    // measured camera angle or proof that the game's projection patch was applied.
    const double vfov_ratio = aspect_world > 0.0 ? (16.0 / 9.0) / aspect_world : 0.0;
    // The 2D layer's own space is 1920x1080; the safe area is that space scaled by the
    // uniform factor and moved by the centred offset inside the real target.
    const unsigned safe_w = unsigned(1920.0 * s.ui_scale_x + 0.5);
    const unsigned safe_h = unsigned(1080.0 * s.ui_scale_y + 0.5);
    std::printf(
        "FrameTrace: n=%llu | logical=1920x1080 | initial_rt=%ux%u | world=%ux%u | "
        "world_source=%s aspect_world=%.4f aspect_present=%.4f "
        "fixed_hfov_vertical_ratio=%.4f fov=unmeasured | "
        "post=%s | upscaler=%s%s out=%ux%u preset=%d | "
        "layer2d=%ux%u scale=%.4f,%.4f offset=%d,%d isotropic=%d | "
        "ui_safe=%d,%d %ux%u | viewport=%.0f,%.0f %.0fx%.0f | scissor=%ld,%ld %ux%u | "
        "swapchain=%ux%u fmt=%d mode=%d images=%d gen=%llu redundant=%llu dxgi=%d headless=%d | "
        "present=%d,%d %ux%u bars=%d,%d\n",
        s.frames, s.frame_w, s.frame_h, s.world_w, s.world_h,
        s.scene_target_observed ? "scene-depth" : s.world_w ? "host-raster" : "unobserved",
        aspect_world, aspect_swap,
        vfov_ratio, s.hdr ? "hdr+post" : "sdr+post", UpscalerLabel(s.upscaler),
        s.upscaler_on ? "" : "(inactive)", s.out_w, s.out_h, s.preset, s.ui_target_w,
        s.ui_target_h, double(s.ui_scale_x), double(s.ui_scale_y), s.ui_off_x, s.ui_off_y,
        s.ui_isotropic ? 1 : 0, s.ui_off_x, s.ui_off_y, safe_w, safe_h, double(s.vp_x),
        double(s.vp_y), double(s.vp_w), double(s.vp_h), s.sc_x, s.sc_y, s.sc_w, s.sc_h, s.swap_w,
        s.swap_h, s.swap_format, s.swap_mode, s.swap_images, s.generation, s.redundant,
        s.uses_dxgi ? 1 : 0, s.headless ? 1 : 0, s.dst_x, s.dst_y, s.dst_w, s.dst_h,
        s.dst_x, s.dst_y);
    std::fflush(stdout);
}

/// Closes a presented frame: fills the presentation fields and prints when due.
inline void Present(unsigned frame_w, unsigned frame_h, unsigned swap_w, unsigned swap_h,
                    int swap_format, int swap_mode, int swap_images, unsigned long long generation,
                    unsigned long long redundant, bool uses_dxgi, bool headless, int dst_x,
                    int dst_y, unsigned dst_w, unsigned dst_h) {
    if (!Enabled()) {
        return;
    }
    auto& state = Get();
    std::scoped_lock lock{state.mutex};
    auto& s = state.snap;
    s.frame_w = frame_w;
    s.frame_h = frame_h;
    s.swap_w = swap_w;
    s.swap_h = swap_h;
    s.swap_format = swap_format;
    s.swap_mode = swap_mode;
    s.swap_images = swap_images;
    s.generation = generation;
    s.redundant = redundant;
    s.uses_dxgi = uses_dxgi;
    s.headless = headless;
    s.dst_x = dst_x;
    s.dst_y = dst_y;
    s.dst_w = dst_w;
    s.dst_h = dst_h;
    ++s.frames;
    const int interval = Interval();
    if (interval > 0 && s.frames % static_cast<unsigned long long>(interval) == 0) {
        Print(s);
    }
}

} // namespace Gow3Trace
