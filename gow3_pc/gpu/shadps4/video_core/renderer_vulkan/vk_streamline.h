// SPDX-FileCopyrightText: Copyright 2026 gow3 contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Streamline SDK v2.14.16 native Vulkan integration (DLSS Super Resolution / DLAA with
// Transformer Presets J/K/L/M, DLSS-G Frame Generation / MFG, NVIDIA Reflex + PCL).
// Loaded dynamically from sl.interposer.dll next to the executable with Authenticode verification.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace sl {
struct FrameToken;
}

namespace Vulkan {

enum class SlDlssMode : u32 {
    Off = 0,
    Dlaa = 1,
    Quality = 2,
    Balanced = 3,
    Performance = 4,
    UltraPerformance = 5,
    UltraQuality = 6,
};

enum class SlDlssPreset : u32 {
    Auto = 0,
    PresetJ = 1,
    PresetK = 2,
    PresetL = 3,
    PresetM = 4,
};

enum class SlDlssGMode : u32 {
    Off = 0,
    On = 1,
    Auto = 2,
    Dynamic = 3,
};

enum class SlReflexMode : u32 {
    Off = 0,
    On = 1,
    Boost = 2,
};

enum class SlPclMarker : u32 {
    SimulationStart = 0,
    SimulationEnd = 1,
    RenderSubmitStart = 2,
    RenderSubmitEnd = 3,
    PresentStart = 4,
    PresentEnd = 5,
    InputSample = 6,
};

class Streamline {
public:
    struct ImageResource {
        vk::Image image{};
        vk::DeviceMemory memory{};
        vk::ImageView view{};
        vk::Format format{vk::Format::eUndefined};
        vk::ImageLayout layout{vk::ImageLayout::eGeneral};
        u32 width{0};
        u32 height{0};
        u32 mip_levels{1};
        u32 array_layers{1};
        u32 usage{0};
    };

    struct FrameConstants {
        float camera_view_to_clip[16]{};
        float clip_to_camera_view[16]{};
        float clip_to_prev_clip[16]{};
        float prev_clip_to_clip[16]{};
        float jitter_x{0.0f};
        float jitter_y{0.0f};
        float mvec_scale_x{1.0f};
        float mvec_scale_y{1.0f};
        float camera_pos[3]{0.0f, 0.0f, 0.0f};
        float camera_up[3]{0.0f, 1.0f, 0.0f};
        float camera_right[3]{1.0f, 0.0f, 0.0f};
        float camera_fwd[3]{0.0f, 0.0f, 1.0f};
        float camera_near{0.1f};
        float camera_far{1000.0f};
        float camera_fov{0.785398f};
        float camera_aspect{16.0f / 9.0f};
        float motion_vector_resolution_width{0.0f};
        float motion_vector_resolution_height{0.0f};
        bool camera_valid{false};
        bool reset{false};
        bool depth_inverted{false};
        bool motion_vectors_jittered{false};
    };

    struct CameraInput {
        float cur_inv_view[12]{};   // 3x4 view-to-world rows
        float cur_proj[4]{};        // x scale, y scale, z scale, z offset
        float prev_inv_view[12]{};  // 3x4 previous view-to-world rows
        float prev_proj[4]{};       // previous proj
        float jitter_x{0.0f};
        float jitter_y{0.0f};
        u32 render_width{1};
        u32 render_height{1};
        float camera_near{0.1f};
        float camera_fov{0.785398f};
        bool valid{false};
        bool prev_valid{false};
        bool reset{false};
    };

    struct DlssEvalDesc {
        ImageResource color_in{};
        ImageResource depth{};
        ImageResource motion_vectors{};
        ImageResource color_out{};
        ImageResource exposure{};
        u32 render_width{0};
        u32 render_height{0};
        u32 output_width{0};
        u32 output_height{0};
        SlDlssMode mode{SlDlssMode::Off};
        SlDlssPreset preset{SlDlssPreset::Auto};
        float sharpness{0.0f};
        bool hdr{false};
        bool auto_exposure{true};
    };

    struct DlssGDesc {
        SlDlssGMode mode{SlDlssGMode::Off};
        u32 num_frames_to_generate{1}; // 1 = 2X, 2 = 3X, 3 = 4X
        u32 dynamic_target_fps{0};
        u32 render_width{0};
        u32 render_height{0};
        u32 output_width{0};
        u32 output_height{0};
        vk::Format color_format{vk::Format::eUndefined};
        vk::Format hudless_format{vk::Format::eUndefined};
        vk::Format depth_format{vk::Format::eUndefined};
        vk::Format mvec_format{vk::Format::eUndefined};
        bool ui_recomposition{true};
    };

    struct ReflexTelemetry {
        bool supported{false};
        bool latency_report_available{false};
        bool low_latency_active{false};
        float pc_latency_ms{0.0f};
        float render_latency_ms{0.0f};
        float gpu_latency_ms{0.0f};
        float sim_latency_ms{0.0f};
        u32 max_generated_frames{0};
        u32 frames_actually_presented{0};
        bool dlss_g_vsync_supported{false};
        bool dlss_g_dynamic_mfg_supported{false};
        u64 dlss_vram_bytes{0};
        u64 dlss_g_vram_bytes{0};
        u32 dlss_g_status{0};
    };

    static Streamline* Get();
    ~Streamline();

    /// Loads sl.interposer.dll next to the executable, verifies NVIDIA signature, calls slInit().
    bool InitBeforeInstance();

    /// Returns vkGetInstanceProcAddr exported by sl.interposer.dll so Vulkan calls are interposed.
    [[nodiscard]] PFN_vkGetInstanceProcAddr GetVkInstanceProcAddr() const noexcept;

    /// Appends Vulkan instance extensions required by loaded Streamline features.
    void AppendInstanceExtensions(std::vector<const char*>& enabled) const;

    /// Queries adapter feature support & populates required device extensions + VK 1.2/1.3 features.
    void PreparePhysicalDevice(vk::Instance instance, vk::PhysicalDevice physical,
                               std::vector<const char*>& enabled_device_exts,
                               vk::PhysicalDeviceVulkan12Features& vk12,
                               vk::PhysicalDeviceVulkan13Features& vk13);

    /// Resolves feature API functions and initial adapter state after VkDevice creation.
    void OnDeviceCreated(vk::Instance instance, vk::PhysicalDevice physical, vk::Device device);

    /// Calls slShutdown() prior to VkDevice / VkInstance destruction (keeps interposer mapped until UnloadInterposer).
    void Shutdown();

    /// Unloads sl.interposer.dll after VkDevice / VkInstance have been destroyed cleanly.
    void UnloadInterposer();

    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }
    [[nodiscard]] bool IsInterposerHooked() const noexcept { return sl_vk_get_instance_proc_ != nullptr; }
    [[nodiscard]] bool IsDlssSupported() const noexcept { return dlss_supported_; }
    [[nodiscard]] bool IsDlssGSupported() const noexcept { return dlss_g_supported_; }
    [[nodiscard]] bool IsReflexSupported() const noexcept { return reflex_supported_; }
    [[nodiscard]] bool IsPclSupported() const noexcept { return pcl_supported_; }
    [[nodiscard]] bool IsNisSupported() const noexcept { return nis_supported_; }
    [[nodiscard]] bool IsVsyncSupportedWithDlssG() const noexcept { return dlss_g_vsync_supported_; }
    [[nodiscard]] bool IsDynamicMfgSupported() const noexcept { return dlss_g_dynamic_mfg_supported_; }
    [[nodiscard]] u32 MaxGeneratedFrames() const noexcept { return dlss_g_max_frames_; }
    [[nodiscard]] const std::string& StatusSummary() const noexcept { return status_summary_; }

    /// Allocates or updates the Streamline frame token for `frame_index` and runs slReflexSleep if enabled.
    void BeginFrame(u32 frame_index, SlReflexMode reflex_mode, u32 frame_limit_us);

    /// Emits a PCL marker for `frame_index` (defaults to most recent frame if UINT32_MAX).
    void SetPclMarker(SlPclMarker marker, u32 frame_index = UINT32_MAX);

    /// Supplies per-frame common camera, projection, jitter, and motion vector constants.
    bool SetConstants(const FrameConstants& constants, u32 frame_index = UINT32_MAX, u32 viewport_id = 0);

    /// Builds exact row-major camera matrices using sl_matrix_helpers and pushes constants.
    static FrameConstants BuildConstants(const CameraInput& cam);
    bool SetCameraConstants(const CameraInput& cam, u32 frame_index = UINT32_MAX, u32 viewport_id = 0);

    /// Tags an image resource with Streamline.
    /// buffer_type corresponds to sl::kBufferType* (0=Depth, 1=MV, 2=HUDLess, 3=ScalingIn, 4=ScalingOut, 23=UIColorAndAlpha).
    bool TagImage(vk::CommandBuffer cmd, u32 buffer_type, const ImageResource& img,
                  bool valid_until_present = true, u32 extent_width = 0, u32 extent_height = 0,
                  u32 frame_index = UINT32_MAX, u32 viewport_id = 0);

    /// Runs Streamline DLSS Super Resolution / DLAA onto `desc.color_out`.
    bool EvaluateDlss(vk::CommandBuffer cmd, const DlssEvalDesc& desc,
                      u32 frame_index = UINT32_MAX, u32 viewport_id = 0);

    /// Configures DLSS-G Frame Generation for the active viewport.
    bool UpdateDlssG(const DlssGDesc& desc, u32 viewport_id = 0);

    /// Updates Reflex mode and optional frame limiter (`frame_limit_us == 0` disables limit).
    bool UpdateReflex(SlReflexMode mode, u32 frame_limit_us);

    /// Queries real Streamline Reflex & DLSS-G telemetry (PC latency, MFG state, VRAM estimate).
    ReflexTelemetry QueryTelemetry(u32 viewport_id = 0);

    /// Queries NVIDIA optimal internal render resolution for the requested quality mode.
    bool QueryOptimalDlssSize(u32 output_width, u32 output_height, SlDlssMode mode,
                              u32& out_render_width, u32& out_render_height,
                              float& out_sharpness);

    /// Frees feature GPU resources (call cleanly on swapchain resize / disable).
    void FreeFeatureResources(u32 viewport_id = 0);

    /// Converts gow3_settings integer DLSS preset ID to SlDlssPreset.
    static SlDlssPreset PresetFromInt(int preset_id) noexcept;

    /// Converts Preset::* (0..4) into SlDlssMode.
    static SlDlssMode ModeFromScalePreset(int scale_preset) noexcept;

private:
    Streamline() = default;
    sl::FrameToken* GetOrCreateTokenLocked(u32 frame_index);
    void RefreshDlssGStateLocked(u32 viewport_id);

    struct Impl;
    Impl* impl_{nullptr};
    mutable std::mutex mutex_{};
    void* interposer_dll_{nullptr};
    PFN_vkGetInstanceProcAddr sl_vk_get_instance_proc_{nullptr};

    bool init_attempted_{false};
    bool initialized_{false};
    bool device_initialized_{false};

    bool dlss_supported_{false};
    bool dlss_g_supported_{false};
    bool reflex_supported_{false};
    bool pcl_supported_{false};
    bool nis_supported_{false};

    bool dlss_g_vsync_supported_{false};
    bool dlss_g_dynamic_mfg_supported_{false};
    u32 dlss_g_max_frames_{0};
    u32 last_frame_index_{0};

    std::string status_summary_{};
    std::unordered_map<u32, sl::FrameToken*> frame_tokens_{};
};

} // namespace Vulkan
