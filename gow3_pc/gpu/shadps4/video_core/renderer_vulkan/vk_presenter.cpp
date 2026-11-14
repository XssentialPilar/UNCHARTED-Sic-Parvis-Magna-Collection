// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/debug.h"
#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "sdl_window.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderdoc.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "gow3_overlay.h"
#include "gow3_settings.h"
#include "gow3_graphics.h"
#include "gow3_thumbnail.h"
#include "video_core/renderer_vulkan/vk_temporal_upscaler.h"
#include "video_core/renderer_vulkan/vk_streamline.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "ffx_vk_portable.h"
#include "ffx_vk_framegeneration_presenter.h"
#include "video_core/renderer_vulkan/presentation_geometry.h"
#include "video_core/renderer_vulkan/frame_trace.h"
#include "video_core/renderer_vulkan/vk_warmup_inbox.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/amdgpu/liverpool.h" // gow3 21 Oct 2026: ServiceQueuedCommands
#include "video_core/texture_cache/image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <system_error>
#include <vector>
#include <vk_mem_alloc.h>
#ifdef MemoryBarrier
#undef MemoryBarrier // gow3: winnt.h macro (through fmt), clashes with vk::MemoryBarrier
#endif

namespace Vulkan {

namespace {
/// gow3 20 Oct 2026: presents on their own queue (Instance::SeparatePresentQueue) take that
/// queue's lock only; on the graphics queue the submit lock, as before.
std::mutex& PresentLock(const Instance& instance) {
    return instance.SeparatePresentQueue() ? Scheduler::present_mutex : Scheduler::submit_mutex;
}

FfxVkPortableImage DescribeFgImage(vk::Image image, vk::Format format, u32 width, u32 height,
                                   vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect,
                                   FfxVkPortableResourceState state) {
    FfxVkPortableImage out{};
    out.structSize = sizeof(out);
    out.image = image;
    out.format = static_cast<VkFormat>(format);
    out.extent = {width, height};
    out.mipCount = 1;
    out.arrayLayers = 1;
    out.usage = static_cast<VkImageUsageFlags>(usage);
    out.aspect = static_cast<VkImageAspectFlags>(aspect);
    out.state = state;
    return out;
}
} // namespace

bool CanBlitToSwapchain(const vk::PhysicalDevice physical_device, vk::Format format) {
    const vk::FormatProperties props{physical_device.getFormatProperties(format)};
    return static_cast<bool>(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);
}

[[nodiscard]] vk::ImageSubresourceLayers MakeImageSubresourceLayers() {
    return vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 dst_width,
                                          s32 dst_height, s32 offset_x, s32 offset_y) {
    return vk::ImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            std::array{
                vk::Offset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            std::array{
                vk::Offset3D{
                    .x = offset_x,
                    .y = offset_y,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = offset_x + dst_width,
                    .y = offset_y + dst_height,
                    .z = 1,
                },
            },
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlitStretch(s32 frame_width, s32 frame_height,
                                                 s32 swapchain_width, s32 swapchain_height) {
    return MakeImageBlit(frame_width, frame_height, swapchain_width, swapchain_height, 0, 0);
}

static vk::Rect2D FitImage(s32 frame_width, s32 frame_height, s32 swapchain_width,
                           s32 swapchain_height) {
    const auto rect = FitPresentation(frame_width, frame_height, swapchain_width, swapchain_height);
    return vk::Rect2D{{rect[0], rect[1]}, {u32(rect[2]), u32(rect[3])}};
}

[[nodiscard]] vk::ImageBlit MakeImageBlitFit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                             s32 swapchain_height) {
    const auto& dst_rect = FitImage(frame_width, frame_height, swapchain_width, swapchain_height);

    return MakeImageBlit(frame_width, frame_height, dst_rect.extent.width, dst_rect.extent.height,
                         dst_rect.offset.x, dst_rect.offset.y);
}

// gow3: screenshot capture and ImGui overlays removed; the port presents directly.

Presenter::Presenter(Frontend::WindowSDL& window_, AmdGpu::Liverpool* liverpool_)
    : window{window_}, liverpool{liverpool_},
      instance{window, EmulatorSettings.GetGpuId(), EmulatorSettings.IsVkValidationEnabled(),
               EmulatorSettings.IsVkCrashDiagnosticEnabled()},
      draw_scheduler{instance, true}, present_scheduler{instance}, flip_scheduler{instance},
      swapchain{instance, window}, runtime{instance, draw_scheduler},
      rasterizer{std::make_unique<Rasterizer>(instance, draw_scheduler, runtime, liverpool)},
      texture_cache{rasterizer->GetTextureCache()} {
    const u32 num_images = swapchain.GetImageCount();
    const vk::Device device = instance.GetDevice();

    // Create presentation frames.
    present_frames.resize(num_images);
    for (u32 i = 0; i < num_images; i++) {
        Frame& frame = present_frames[i];
        frame.id = i;
        auto fence = Check<"create present done fence">(
            device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
        frame.present_done = fence;
        free_queue.push(&frame);
    }

    fsr_pass.Create(device, instance.GetAllocator(), num_images);
    pp_pass.Create(device, swapchain.GetSurfaceFormat().format);
    Gow3Overlay::Init(instance, swapchain.GetSurfaceFormat().format, num_images);

    {
        FfxVkPortableDeviceCapabilities ffx_caps{};
        ffx_caps.structSize = sizeof(ffx_caps);
        if (ffxVkPortableQueryDeviceCapabilities(instance.GetPhysicalDevice(), &ffx_caps) ==
            FFX_VK_PORTABLE_OK) {
            fsr_fg_supported_ = (ffx_caps.fsr3FrameGenerationPrerequisites == VK_TRUE) &&
                                instance.IsStorageImageWriteWithoutFormatEnabled();
        }
        Gow3Settings::Get().fsr_fg_supported = fsr_fg_supported_;
        if (auto* sl = Streamline::Get(); sl && sl->IsInitialized()) {
            Gow3Settings::ConfigureStreamlineSupport(
                sl->IsDlssSupported(), sl->IsDlssGSupported(),
                sl->IsDlssGSupported() ? static_cast<int>(sl->MaxGeneratedFrames() + 1) : 1,
                sl->IsReflexSupported(), sl->IsVsyncSupportedWithDlssG(), fsr_fg_supported_);
        } else {
            Gow3Settings::ConfigureStreamlineSupport(false, false, 1, false, false, fsr_fg_supported_);
        }
        dlss_g_active_ = swapchain.IsDlssGActive();
        fsr_fg_active_ = swapchain.IsFsrFgActive();
        last_present_tp_ = std::chrono::steady_clock::now();
    }

    const bool binary_cache = EmulatorSettingsImpl::Flag("GOW3_PIPELINE_BINARIES", false) &&
                              EmulatorSettings.IsPipelineCacheEnabled();
    const bool capture_only = EmulatorSettingsImpl::Flag("GOW3_PIPELINE_BINARY_ISOLATED", false) &&
                              EmulatorSettingsImpl::Flag("GOW3_PIPELINE_CAPTURE_ONLY", false);
    // gow3 18 Oct 2026: the background warm-up also with pipeline binaries. On demand, each compute
    // shader was translated on the GPU thread the first time a level used it: Uncharted 3 hitched
    // 90-170 ms right after loading (35 compiles of 10-15 ms in 5 s); with the warm-up (2,440
    // pipelines in 16 s on background threads during the title screen) none were left in play.
    // GOW3_ON_DEMAND_PIPELINES=1 keeps the on-demand start.
    if (EmulatorSettingsImpl::Flag("GOW3_ON_DEMAND_PIPELINES", false) &&
        UseOnDemandPipelines(binary_cache, instance.GetPipelineBinaryGlobalKey().keySize != 0,
                            capture_only)) {
        rasterizer->GetPipelineCache().StartOnDemand();
        return;
    }

    // gow3: by default the cached pipelines load while the game runs (a corner note shows the
    // progress). Otherwise rebuilding them takes up to minutes before the first game frame:
    // show what is happening instead of a black window.
    const char* background_warmup = std::getenv("GOW3_BACKGROUND_WARMUP");
    if ((!background_warmup || *background_warmup != '0') &&
        rasterizer->GetPipelineCache().StartBackgroundWarmUp()) {
        return;
    }
    Gow3Overlay::SetLoading(true, 0, 0);
    PresentLoadingFrame();
    auto last = std::chrono::steady_clock::now();
    rasterizer->GetPipelineCache().WarmUp([&](u32 done, u32 total) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(250)) {
            return;
        }
        last = now;
        Gow3Overlay::SetLoading(true, done, total);
        PresentLoadingFrame();
    });
    Gow3Overlay::SetLoading(false, 0, 0);

}

Presenter::~Presenter() {

    draw_scheduler.Finish();
    present_scheduler.Finish();
    flip_scheduler.Finish();
    DestroyFsrFgResources();
    Check(draw_scheduler.CommandBuffer().reset());
    Check(present_scheduler.CommandBuffer().reset());
    Check(flip_scheduler.CommandBuffer().reset());

    const vk::Device device = instance.GetDevice();
    if (thumbnail.image) {
        vmaDestroyImage(instance.GetAllocator(), thumbnail.image, thumbnail.image_allocation);
    }
    if (thumbnail.buffer) {
        vmaDestroyBuffer(instance.GetAllocator(), thumbnail.buffer, thumbnail.buffer_allocation);
    }
    for (auto& frame : present_frames) {
        vmaDestroyImage(instance.GetAllocator(), frame.image, frame.allocation);
        device.destroyImageView(frame.image_view);
        device.destroyFence(frame.present_done);
    }
}

bool Presenter::IsVideoOutSurface(const AmdGpu::ColorBuffer& color_buffer) const {
    return std::ranges::find(vo_buffers_addr, color_buffer.Address()) != vo_buffers_addr.cend();
}

void Presenter::RecreateFrame(Frame* frame, u32 width, u32 height) {
    const vk::Device device = instance.GetDevice();
    if (frame->image_view) {
        device.destroyImageView(frame->image_view);
    }
    if (frame->image) {
        vmaDestroyImage(instance.GetAllocator(), frame->image, frame->allocation);
    }

    const vk::Format format = swapchain.GetSurfaceFormat().format;
    const vk::ImageCreateInfo image_info = {
        .flags = vk::ImageCreateFlagBits::eMutableFormat,
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
    };

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkImage unsafe_image{};
    VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);

    VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info, &alloc_info,
                                     &unsafe_image, &frame->allocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating texture with error {}",
                     vk::to_string(vk::Result{result}));
        UNREACHABLE();
    }
    frame->image = vk::Image{unsafe_image};
    SetObjectName(device, frame->image, "Frame image #{}", frame->id);

    const vk::ImageViewCreateInfo view_info = {
        .image = frame->image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    auto view = Check<"create frame image view">(device.createImageView(view_info));
    frame->image_view = view;
    frame->width = width;
    frame->height = height;

    frame->is_hdr = swapchain.GetHDR();
}

Frame* Presenter::PrepareLastFrame() {
    if (last_submit_frame == nullptr) {
        return nullptr;
    }

    Frame* frame = last_submit_frame;

    while (true) {
        vk::Result result = instance.GetDevice().waitForFences(frame->present_done, false,
                                                               std::numeric_limits<u64>::max());
        if (result == vk::Result::eSuccess) {
            break;
        }
        if (result == vk::Result::eTimeout) {
            continue;
        }
        ASSERT_MSG(result != vk::Result::eErrorDeviceLost,
                   "Device lost during waiting for a frame");
    }

    auto& scheduler = flip_scheduler;
    scheduler.EndRendering();
    const auto cmdbuf = scheduler.CommandBuffer();

    const auto frame_subresources = vk::ImageSubresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };

    const auto pre_barrier =
        vk::ImageMemoryBarrier2{.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
                                .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                                .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                .newLayout = vk::ImageLayout::eGeneral,
                                .image = frame->image,
                                .subresourceRange{frame_subresources}};

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    // Flush frame creation commands.
    frame->ready_semaphore = scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return frame;
}

static vk::Format GetFrameViewFormat(const Libraries::VideoOut::PixelFormat format) {
    switch (format) {
    case Libraries::VideoOut::PixelFormat::A8B8G8R8Srgb:
        return vk::Format::eR8G8B8A8Srgb;
    case Libraries::VideoOut::PixelFormat::A8R8G8B8Srgb:
        return vk::Format::eB8G8R8A8Srgb;
    case Libraries::VideoOut::PixelFormat::A2R10G10B10:
    case Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb:
    case Libraries::VideoOut::PixelFormat::A2R10G10B10Bt2020Pq:
        return vk::Format::eA2R10G10B10UnormPack32;
    default:
        break;
    }
    UNREACHABLE_MSG("Unknown format={}", static_cast<u32>(format));
    return {};
}

// gow3: raw copy of an image for GOW3_FRAME_DUMP_TRIGGER, written once the GPU is done.
static void DumpRaw(const Instance& instance, Scheduler& scheduler, vk::CommandBuffer cmdbuf,
                    vk::Image image, vk::ImageLayout layout, u32 w, u32 h, const char* name,
                    int index, u32 pixel_bytes = 4,
                    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
                    u32 block_width = 1, u32 block_height = 1, u32 layer = 0) {
    const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                       .size = VkDeviceSize((w + block_width - 1) / block_width) *
                                               ((h + block_height - 1) / block_height) * pixel_bytes,
                                       .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &buffer, &allocation,
                        &info) != VK_SUCCESS) {
        return;
    }
    cmdbuf.copyImageToBuffer(image, layout, buffer,
                             vk::BufferImageCopy{.imageSubresource = {aspect, 0, layer, 1},
                                                 .imageExtent = {w, h, 1}});
    const char* dir = std::getenv("GOW3_DUMP_DIR");
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s_%03d_%ux%u.raw", dir && *dir ? dir : "out/dump", name,
                  index, w, h);
    scheduler.DeferPriorityOperation([allocator = instance.GetAllocator(), buffer, allocation, info,
                                      size = buffer_ci.size, file = std::string{path}] {
        vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
        if (FILE* f = std::fopen(file.c_str(), "wb")) {
            std::fwrite(info.pMappedData, 1, size, f);
            std::fclose(f);
        }
        vmaDestroyBuffer(allocator, buffer, allocation);
    });
}

Frame* Presenter::PrepareFrame(const Libraries::VideoOut::BufferAttributeGroup& attribute,
                               VAddr cpu_address) {
    // gow3: GOW3_FRAME_DUMP_TRIGGER=<file>: when the file exists it is removed and this frame's
    // guest display buffer and presented image are written to GOW3_DUMP_DIR (default out/dump)
    // as raw 32-bit pixels (menus and movies included; the upscaler dumps scene frames only).
    static const char* frame_dump_trigger = std::getenv("GOW3_FRAME_DUMP_TRIGGER");
    static int frame_dump_index = 0;
    // gow3 20 Oct 2026 (idle CPU): the file is looked for at most every 250 ms. A remove() per
    // frame (a failed file open in the kernel) was 0.6% of the GPU thread with diagnostics on.
    static auto next_dump_check = std::chrono::steady_clock::time_point{};
    bool frame_dump = false;
    if (frame_dump_trigger) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_dump_check) {
            next_dump_check = now + std::chrono::milliseconds(250);
            frame_dump = std::remove(frame_dump_trigger) == 0;
        }
    }
    // gow3: scaled upscaler presets: the output-size display buffer drawn by the port.
    TemporalUpscaler::Display display{};
    const bool upscaled = rasterizer->GetUpscaler().DisplayOverride(cpu_address, display);
    VideoCore::ImageId image_id{};
    if (!upscaled) {
        auto desc = VideoCore::TextureCache::ImageDesc{attribute, cpu_address};
        image_id = texture_cache.FindImage(desc);
        texture_cache.UpdateImage(image_id);
    }

    Frame* frame = GetRenderFrame();

    const auto frame_subresources = vk::ImageSubresourceRange{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };

    const auto pre_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .image = frame->image,
        .subresourceRange{frame_subresources},
    };

    draw_scheduler.EndRendering();
    const auto cmdbuf = draw_scheduler.CommandBuffer();
    // Optional one-shot request: guest address and exact guest size per line, or
    // "size <width> <height>" for every cached image of that size, "max <width> <height>" for every
    // one up to that size, "range <lo> <hi>" (hex) for every one in those guest addresses (up to 256
    // in all).
    // Dump intermediate targets to locate where geometry/lighting disappears.
    if (frame_dump) {
        const char* dir = std::getenv("GOW3_DUMP_DIR");
        const auto request = std::filesystem::path(dir ? dir : "out/dump") / "guest-images.trigger";
        if (FILE* f = std::fopen(request.string().c_str(), "r")) {
            std::vector<std::pair<unsigned long long, unsigned long long>> requests;
            char line[256];
            constexpr size_t MaxRequests = 256;
            while (requests.size() < MaxRequests && std::fgets(line, sizeof(line), f)) {
                unsigned long long a, b;
                unsigned w, h;
                std::vector<std::pair<VAddr, u32>> images;
                if (std::sscanf(line, "size %u %u", &w, &h) == 2) {
                    images = texture_cache.DebugImagesOfSize(w, h);
                } else if (std::sscanf(line, "max %u %u", &w, &h) == 2) {
                    images = texture_cache.DebugImagesWhere(16, w, 16, h, 0, ~VAddr{0});
                } else if (std::sscanf(line, "range %llx %llx", &a, &b) == 2) {
                    images = texture_cache.DebugImagesWhere(1, ~0u, 1, ~0u, a, b);
                } else if (std::sscanf(line, "%llx %llx", &a, &b) == 2) {
                    requests.emplace_back(a, b);
                }
                for (const auto& [image_address, image_size] : images) {
                    if (requests.size() < MaxRequests) requests.emplace_back(image_address, image_size);
                }
            }
            for (const auto& [address, size] : requests) {
                const auto id = texture_cache.FindImageFromRange(address, size, false);
                if (!id) continue;
                auto& image = texture_cache.GetImage(id);
                const auto format = image.info.pixel_format;
                const bool depth16 = format == vk::Format::eD16Unorm;
                const bool depth = format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD32Sfloat ||
                                   depth16;
                const bool bc1 = format == vk::Format::eBc1RgbaUnormBlock ||
                                 format == vk::Format::eBc1RgbaSrgbBlock;
                const bool bc5 = format == vk::Format::eBc5UnormBlock ||
                                 format == vk::Format::eBc5SnormBlock;
                const bool bc16 = bc5 || format == vk::Format::eBc3UnormBlock ||
                                  format == vk::Format::eBc3SrgbBlock ||
                                  format == vk::Format::eBc2UnormBlock ||
                                  format == vk::Format::eBc2SrgbBlock ||
                                  format == vk::Format::eBc7UnormBlock ||
                                  format == vk::Format::eBc7SrgbBlock;
                bool compressed = bc1 || bc16;
                u32 bytes = format == vk::Format::eR8Unorm ? 1
                            : format == vk::Format::eR8G8Unorm || depth16 ? 2
                            : bc16 ? 16 : (bc1 || format == vk::Format::eR16G16B16A16Sfloat ? 8 : 4);
                if (image.info.num_samples != 1) continue;
                if (!depth && !compressed && image.info.num_bits != bytes * 8) {
                    // Any other format: its texel (or 4x4 block) size.
                    compressed = image.info.props.is_block;
                    bytes = image.info.num_bits / 8;
                    if (bytes == 0) continue;
                }
                runtime.Transit(&image, vk::ImageLayout::eTransferSrcOptimal,
                                vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
                runtime.FlushBarriers();
                const auto name = std::format("guest_{:x}_{}", address, vk::to_string(format));
                DumpRaw(instance, draw_scheduler, cmdbuf, image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                        image.info.size.width, image.info.size.height, name.c_str(), frame_dump_index,
                        bytes, depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor,
                        compressed ? 4 : 1, compressed ? 4 : 1);
                // Array images (shadow cascades): every further layer as <name>_L<n>.
                for (u32 layer = 1; layer < std::min<u32>(image.info.resources.layers, 8); ++layer) {
                    const auto layer_name = std::format("{}_L{}", name, layer);
                    DumpRaw(instance, draw_scheduler, cmdbuf, image.GetImage(),
                            vk::ImageLayout::eTransferSrcOptimal, image.info.size.width,
                            image.info.size.height, layer_name.c_str(), frame_dump_index, bytes,
                            depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor,
                            compressed ? 4 : 1, compressed ? 4 : 1, layer);
                }
                // Keep enough layout/state evidence to compare the cached image with a
                // separate read-only guest-memory snapshot. Do not refresh the image here:
                // that would hide the stale-cache condition this capture is investigating.
                const auto metadata_path = std::filesystem::path(dir ? dir : "out/dump") /
                    std::format("{}_{:03}.txt", name, frame_dump_index);
                if (FILE* metadata = std::fopen(metadata_path.string().c_str(), "w")) {
                    std::fprintf(metadata,
                        "address=%llx guest_size=%x flags=%x pitch=%u tile_mode=%u "
                        "array_mode=%u bank_swizzle=%u width=%u height=%u bits=%u\n",
                        static_cast<unsigned long long>(image.info.guest_address), image.info.guest_size,
                        static_cast<unsigned>(image.flags), image.info.pitch,
                        static_cast<unsigned>(image.info.tile_mode), static_cast<unsigned>(image.info.array_mode),
                        image.info.bank_swizzle, image.info.size.width, image.info.size.height, image.info.num_bits);
                    for (u32 mip = 0; mip < image.info.resources.levels; ++mip) {
                        const auto& layout = image.info.mips_layout[mip];
                        std::fprintf(metadata, "mip=%u offset=%x size=%x pitch=%u height=%u\n",
                                     mip, layout.offset, layout.size, layout.pitch, layout.height);
                    }
                    std::fclose(metadata);
                }
                if (format == vk::Format::eD32SfloatS8Uint) {
                    const auto stencil_name = name + "_stencil";
                    DumpRaw(instance, draw_scheduler, cmdbuf, image.GetImage(), vk::ImageLayout::eTransferSrcOptimal,
                            image.info.size.width, image.info.size.height, stencil_name.c_str(), frame_dump_index,
                            1, vk::ImageAspectFlagBits::eStencil);
                }
            }
            std::fclose(f);
            std::filesystem::remove(request);
        }
    }
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    VideoCore::ImageViewInfo view_info{};
    view_info.format = GetFrameViewFormat(attribute.attrib.pixel_format);
    // Exclude alpha from output frame to avoid blending with UI.
    view_info.mapping.a = vk::ComponentSwizzle::eOne;

    vk::ImageView image_view{};
    vk::Extent2D image_size{};
    if (upscaled) {
        const auto device = instance.GetDevice();
        image_view = Check(device.createImageView({
            .image = display.image,
            .viewType = vk::ImageViewType::e2D,
            .format = view_info.format,
            .components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eOne},
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        }));
        draw_scheduler.DeferOperation([device, image_view] { device.destroyImageView(image_view); });
        image_size = vk::Extent2D{display.width, display.height};
        const vk::ImageMemoryBarrier2 to_read{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = display.image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_read});
    } else {
        auto& image = texture_cache.GetImage(image_id);
        if (frame_dump) {
            runtime.Transit(&image, vk::ImageLayout::eTransferSrcOptimal,
                            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
            runtime.FlushBarriers();
            DumpRaw(instance, draw_scheduler, cmdbuf, image.GetImage(),
                    vk::ImageLayout::eTransferSrcOptimal, image.info.size.width,
                    image.info.size.height, "display", frame_dump_index);
        }
        image_view = *image.FindView(view_info).image_view;
        image_size = vk::Extent2D{image.info.size.width, image.info.size.height};
        // gow3 1 Nov 2026 (GitHub #236 #278 #325, AMD at 1080p/1440p): the sharpening pass
        // (RCAS, fsr_pass) reads this image in a compute shader when it is as large as the
        // window. The barrier made the game's last draws visible to fragment shaders only, so
        // on AMD the compute read raced them: menu elements flickered and tiles came out black.
        // Larger or smaller pictures (1620p, 900p) and RCAS off take the fragment-only path.
        runtime.Transit(&image, vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::PipelineStageFlagBits2::eFragmentShader |
                            vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderRead);
        runtime.FlushBarriers();
    }
    expected_ratio = static_cast<float>(image_size.width) / static_cast<float>(image_size.height);

    // gow3: FSR 1, RCAS and its strength come from the in-game menu, every frame.
    const auto& graphics = Gow3Settings::Get();
    fsr_settings.enable = graphics.fsr1;
    fsr_settings.use_rcas = graphics.rcas;
    fsr_settings.rcas_attenuation = Gow3Graphics::RcasAttenuation(graphics.rcas_strength);
    Gow3Settings::Get().image_width = int(image_size.width);
    Gow3Settings::Get().image_height = int(image_size.height);
    Gow3Settings::Get().window_width = int(frame->width);
    Gow3Settings::Get().window_height = int(frame->height);
    Gow3Settings::Get().rcas_applied =
        fsr_settings.use_rcas &&
        Gow3Graphics::FsrPassRuns(fsr_settings.enable, true, int(image_size.width), int(image_size.height),
                                  int(frame->width), int(frame->height));
    image_view = fsr_pass.Render(cmdbuf, image_view, image_size, {frame->width, frame->height},
                                 fsr_settings, frame->is_hdr);

    // Vulkan has no sRGB variant of the 10-bit format, so an A2R10G10B10Srgb buffer reaches
    // the post process pass still sRGB encoded and has to be decoded there instead.
    pp_settings.srgb_input =
        attribute.attrib.pixel_format == Libraries::VideoOut::PixelFormat::A2R10G10B10Srgb;
    pp_pass.Render(cmdbuf, image_view, image_size, *frame, pp_settings);
    if (frame_dump) {
        // The presented image as the swapchain blit reads it (General after the pass).
        const vk::MemoryBarrier2 done{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                                      .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                                      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                      .dstAccessMask = vk::AccessFlagBits2::eTransferRead};
        cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &done});
        DumpRaw(instance, draw_scheduler, cmdbuf, frame->image, vk::ImageLayout::eGeneral,
                frame->width, frame->height, "output", frame_dump_index);
        ++frame_dump_index;
    }

    // Flush frame creation commands.
    frame->fg = rasterizer ? rasterizer->GetUpscaler().LatestFgFrame()
                           : TemporalUpscaler::FgFrameData{};
    frame->sl_frame_index =
        frame->fg.sl_frame_index ? frame->fg.sl_frame_index
                                 : (rasterizer ? rasterizer->GetUpscaler().CurrentSlFrame() : 0);
    frame->ready_semaphore = draw_scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = draw_scheduler.CurrentTick();
    SubmitInfo info{};
    draw_scheduler.Flush(info);
    if (auto* sl = Streamline::Get(); sl && sl->IsInitialized() && frame->sl_frame_index != 0) {
        sl->SetPclMarker(SlPclMarker::RenderSubmitEnd, frame->sl_frame_index);
    }

    // gow3: the GPU command thread runs at most GOW3_FRAMES_AHEAD (default 1) guest frames
    // ahead of the GPU: it waits here for the frame that many flips back. When the GPU is the
    // bottleneck it finishes frames at an even rate; without this bound the command thread ran
    // ahead and then blocked wherever a resource ran out, so flips (and the guest's frame
    // timing) came in bursts: 12.5/25 ms alternation at 80 FPS. 0 turns it off.
    // The in-game menu changes it live (Gow3Settings::frames_queued; GOW3_FRAMES_AHEAD at start).
    const u32 frames_ahead = u32(std::max(0, Gow3Settings::Get().frames_queued.load()));
    if (!frames_ahead) {
        recent_frame_ticks.clear();
    } else {
        recent_frame_ticks.push_back(frame->ready_tick);
        while (recent_frame_ticks.size() > frames_ahead) {
            const u64 tick = recent_frame_ticks.front();
            recent_frame_ticks.pop_front();
            if (recent_frame_ticks.size() == frames_ahead) {
                // gow3 18 Oct 2026: while the command thread waits here for the GPU (up to a
                // frame), readbacks whose GPU work is done land meanwhile, and with them the
                // fences the guest waits for (ray-cast results). A plain wait held them until the
                // whole frame was done: Uncharted 3 on the rooftops made two or three 5-7 ms
                // frames, then one of 25-35 ms waiting for its ray casts (75 FPS, 1% lows 22 FPS
                // at the 120 limit). GOW3_POLL_WHILE_WAITING=0 waits plainly (A/B).
                static const bool poll_while_waiting = [] {
                    const char* env = std::getenv("GOW3_POLL_WHILE_WAITING");
                    return !(env && env[0] == '0');
                }();
                // gow3 20 Oct 2026 (idle CPU): the 0.25 ms polls woke the command thread up to
                // ~4,000 times a second while the GPU was the bottleneck. Readbacks land in tick
                // order once the GPU has passed their tick, so after a poll the oldest pending
                // one can land no sooner than the GPU's next finished submission: the thread
                // now sleeps until that submission (or the frame's tick) is done, lands what is
                // finished and repeats; with no readback pending it waits plainly (below). Each
                // readback still lands as soon as its submission is done (sooner than the next
                // 0.25 ms poll). Readbacks left after a poll although done (the test knobs
                // GOW3_TEST_READBACK_HOLD/LAG hold them) and a GPU that finishes nothing for
                // 1 s go back to the 0.25 ms polls. GOW3_POLL_ON_COMPLETION=0: the 0.25 ms polls.
                static const bool poll_on_completion = [] {
                    const char* env = std::getenv("GOW3_POLL_ON_COMPLETION");
                    const bool on = !(env && env[0] == '0') &&
                                    !std::getenv("GOW3_TEST_READBACK_HOLD") &&
                                    !std::getenv("GOW3_TEST_READBACK_LAG");
                    std::printf("Presenter: frames-ahead wait lands readbacks %s\n",
                                on ? "as each GPU submission finishes (GOW3_POLL_ON_COMPLETION=0: "
                                     "every 0.25 ms)"
                                   : "every 0.25 ms");
                    return on;
                }();
                // gow3 21 Oct 2026 (GOW3_WAIT_SERVICES_FAULTS=1, the CPU worker): guest fault
                // requests queued while this wait runs are served here (Uncharted 2: read faults
                // waited ~10 ms each for the command thread, 20-38% of a guest thread): the wait
                // goes in slices of at most 1 ms, re-checking the frame's tick after each slice
                // and each served request (one may submit work), and lands readbacks as each
                // submission finishes, as below. Only on the GPU command thread with the draw
                // recording thread idle (Liverpool::ServiceQueuedCommands checks).
                if (poll_while_waiting && rasterizer && liverpool &&
                    AmdGpu::Liverpool::WaitServicesFaults()) {
                    auto* semaphore = draw_scheduler.GetWorkSemaphore();
                    auto& buffer_cache = rasterizer->GetBufferCache();
                    Gow3Stats::WaitTimer timer{Gow3Stats::tick_wait_ns};
                    buffer_cache.PollReadbacksNow();
                    while (!semaphore->IsFree(tick)) {
                        liverpool->ServiceQueuedCommands();
                        if (semaphore->IsFree(tick)) {
                            break;
                        }
                        const u64 newest = buffer_cache.NewestPendingReadbackTick();
                        const u64 known = semaphore->KnownGpuTick();
                        const u64 target = newest > known ? std::min(tick, known + 1) : tick;
                        if (semaphore->WaitFor(target, 1'000'000)) {
                            buffer_cache.PollReadbacksNow();
                        }
                    }
                    liverpool->ServiceQueuedCommands();
                    buffer_cache.PollReadbacksNow();
                } else if (poll_while_waiting && rasterizer) {
                    auto* semaphore = draw_scheduler.GetWorkSemaphore();
                    auto& buffer_cache = rasterizer->GetBufferCache();
                    Gow3Stats::WaitTimer timer{Gow3Stats::tick_wait_ns};
                    if (poll_on_completion) {
                        bool on_completion = true;
                        buffer_cache.PollReadbacksNow();
                        while (!semaphore->IsFree(tick)) {
                            if (on_completion) {
                                // The newest pending readback's tick (0: none pending, also
                                // counting readbacks without data pieces, e.g. fence-only); the
                                // poll refreshed the GPU tick when any was pending.
                                const u64 newest = buffer_cache.NewestPendingReadbackTick();
                                if (newest == 0) {
                                    break; // nothing to land: the plain wait below
                                }
                                const u64 known = semaphore->KnownGpuTick();
                                if (newest > known) {
                                    if (semaphore->WaitFor(std::min(tick, known + 1),
                                                           1'000'000'000)) {
                                        buffer_cache.PollReadbacksNow();
                                        continue;
                                    }
                                }
                                on_completion = false; // the 0.25 ms polls, as before
                            }
                            if (semaphore->WaitFor(tick, 250'000)) {
                                break;
                            }
                            buffer_cache.PollReadbacksNow();
                        }
                    } else {
                        while (!semaphore->IsFree(tick)) {
                            buffer_cache.PollReadbacksNow();
                            if (semaphore->WaitFor(tick, 250'000)) {
                                break;
                            }
                        }
                    }
                    buffer_cache.PollReadbacksNow();
                }
                draw_scheduler.Wait(tick);
            }
        }
    }
    return frame;
}

Frame* Presenter::PrepareBlankFrame(bool present_thread) {
    // Request a free presentation frame.
    Frame* frame = GetRenderFrame();

    auto& scheduler = present_thread ? present_scheduler : draw_scheduler;
    scheduler.EndRendering();

    const auto cmdbuf = scheduler.CommandBuffer();

    constexpr vk::ImageSubresourceRange simple_subresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = 1,
    };
    const auto pre_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .image = frame->image,
        .subresourceRange = simple_subresource,
    };

    const auto post_barrier = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = frame->image,
        .subresourceRange = simple_subresource,
    };

    const vk::RenderingAttachmentInfo attachment = {
        .imageView = frame->image_view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    const vk::RenderingInfo rendering_info = {
        .renderArea =
            {
                .extent = {frame->width, frame->height},
            },
        .layerCount = 1,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &attachment,
    };

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &pre_barrier,
    });

    cmdbuf.beginRendering(rendering_info);
    cmdbuf.endRendering();

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &post_barrier,
    });

    // Flush frame creation commands.
    frame->ready_semaphore = scheduler.GetWorkSemaphore()->Handle();
    frame->ready_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    return frame;
}

void Presenter::PresentLoadingFrame() {
    if (window.GetWidth() == 0 || window.GetHeight() == 0) {
        return;
    }
    swapchain.UpdateVsync();
    if (window.GetWidth() != swapchain.GetWidth() || window.GetHeight() != swapchain.GetHeight()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
    }
    if (!swapchain.AcquireNextImage()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
        if (!swapchain.AcquireNextImage()) return;
    }
    const vk::Image image = swapchain.Image();
    const vk::Extent2D extent = swapchain.GetExtent();
    const auto cmdbuf = present_scheduler.CommandBuffer();
    const vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
    const auto barrier = [&](vk::ImageLayout from, vk::ImageLayout to, vk::AccessFlags src,
                             vk::AccessFlags dst) {
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                               vk::PipelineStageFlagBits::eAllCommands, {}, {}, {},
                               vk::ImageMemoryBarrier{
                                   .srcAccessMask = src,
                                   .dstAccessMask = dst,
                                   .oldLayout = from,
                                   .newLayout = to,
                                   .srcQueueFamilyIndex = swapchain.UsesDxgi()
                                       ? (from == vk::ImageLayout::eUndefined
                                              ? VK_QUEUE_FAMILY_EXTERNAL
                                              : instance.GetGraphicsQueueFamilyIndex())
                                       : VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = swapchain.UsesDxgi()
                                       ? (to == swapchain.FinalLayout()
                                              ? VK_QUEUE_FAMILY_EXTERNAL
                                              : instance.GetGraphicsQueueFamilyIndex())
                                       : VK_QUEUE_FAMILY_IGNORED,
                                   .image = image,
                                   .subresourceRange = range,
                               });
    };
    barrier(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, {},
            vk::AccessFlagBits::eTransferWrite);
    cmdbuf.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal,
                           vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}},
                           range);
    barrier(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eColorAttachmentWrite);
    Gow3Overlay::Render(cmdbuf, swapchain.ImageView(), extent);
    barrier(vk::ImageLayout::eColorAttachmentOptimal, swapchain.FinalLayout(),
            vk::AccessFlagBits::eColorAttachmentWrite, {});
    SubmitInfo info{};
    if (!swapchain.UsesOffscreenPresent()) {
        info.AddWait(swapchain.GetImageAcquiredSemaphore());
        info.AddSignal(swapchain.GetPresentReadySemaphore());
    }
    present_scheduler.Flush(info);
    if (swapchain.UsesOffscreenPresent()) present_scheduler.Finish();
    bool presented;
    {
        std::scoped_lock present_lock{PresentLock(instance)};
        presented = swapchain.Present();
    }
    if (!presented && window.GetWidth() != 0 && window.GetHeight() != 0) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
    }
    // A few frames a second during start-up: waiting keeps semaphores and the swapchain simple.
    present_scheduler.Finish();
}

// gow3 3 Nov 2026: the save list's pictures (gow3_thumbnail.h). The game frame (in
// TransferSrcOptimal after its blit to the swapchain) is scaled into a small RGBA8 image, which is
// copied to a mapped buffer; ReadThumbnail takes it once that frame's present has finished. The
// overlay is drawn later, straight into the swapchain image, so it is never in the picture.
void Presenter::CaptureThumbnail(vk::CommandBuffer cmdbuf, const Frame* frame) {
    using Gow3Thumbnail::Height;
    using Gow3Thumbnail::Width;
    const vk::Device device = instance.GetDevice();
    const vk::Format frame_format = swapchain.GetSurfaceFormat().format;
    // An sRGB frame into an sRGB picture: the bytes stay as shown (a UNORM one would darken it).
    const vk::Format format = frame_format == vk::Format::eB8G8R8A8Srgb ||
                                      frame_format == vk::Format::eR8G8B8A8Srgb
                                  ? vk::Format::eR8G8B8A8Srgb
                                  : vk::Format::eR8G8B8A8Unorm;
    if (!thumbnail.image) {
        const vk::FormatProperties props = instance.GetPhysicalDevice().getFormatProperties(format);
        if (!(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst)) {
            thumbnail.failed = true;
            LOG_WARNING(Render_Vulkan, "Save pictures: {} cannot be a blit target", vk::to_string(format));
            return;
        }
        const VkImageCreateInfo image_info = static_cast<VkImageCreateInfo>(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {Width, Height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
        });
        const VmaAllocationCreateInfo image_alloc{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VkImage image{};
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = VkDeviceSize(Width) * Height * 4,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        };
        const VmaAllocationCreateInfo buffer_alloc{
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VkBuffer buffer{};
        VmaAllocationInfo buffer_out{};
        if (vmaCreateImage(instance.GetAllocator(), &image_info, &image_alloc, &image,
                           &thumbnail.image_allocation, nullptr) != VK_SUCCESS) {
            thumbnail.failed = true;
            return;
        }
        thumbnail.image = vk::Image{image};
        if (vmaCreateBuffer(instance.GetAllocator(), &buffer_info, &buffer_alloc, &buffer,
                            &thumbnail.buffer_allocation, &buffer_out) != VK_SUCCESS ||
            !buffer_out.pMappedData) {
            if (buffer) {
                vmaDestroyBuffer(instance.GetAllocator(), buffer, thumbnail.buffer_allocation);
            }
            thumbnail.failed = true;
            return;
        }
        thumbnail.buffer = vk::Buffer{buffer};
        thumbnail.mapped = buffer_out.pMappedData;
        SetObjectName(device, thumbnail.image, "Save picture");
    }
    const vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    const vk::ImageMemoryBarrier to_dst{
        .srcAccessMask = vk::AccessFlagBits::eNone,
        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = thumbnail.image,
        .subresourceRange = range,
    };
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                           {}, {}, {}, to_dst);
    cmdbuf.blitImage(frame->image, vk::ImageLayout::eTransferSrcOptimal, thumbnail.image,
                     vk::ImageLayout::eTransferDstOptimal,
                     MakeImageBlitStretch(frame->width, frame->height, Width, Height), vk::Filter::eLinear);
    const vk::ImageMemoryBarrier to_src{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = thumbnail.image,
        .subresourceRange = range,
    };
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                           {}, {}, {}, to_src);
    const vk::BufferImageCopy copy{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = MakeImageSubresourceLayers(),
        .imageOffset = {0, 0, 0},
        .imageExtent = {Width, Height, 1},
    };
    cmdbuf.copyImageToBuffer(thumbnail.image, vk::ImageLayout::eTransferSrcOptimal, thumbnail.buffer, copy);
    const vk::BufferMemoryBarrier to_host{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eHostRead,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = thumbnail.buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, {},
                           to_host, {});
    thumbnail.pending = frame->present_done;
}

void Presenter::ReadThumbnail() {
    if (!thumbnail.pending ||
        instance.GetDevice().getFenceStatus(thumbnail.pending) != vk::Result::eSuccess) {
        return;
    }
    thumbnail.pending = vk::Fence{};
    vmaInvalidateAllocation(instance.GetAllocator(), thumbnail.buffer_allocation, 0, VK_WHOLE_SIZE);
    Gow3Thumbnail::Store(static_cast<const unsigned char*>(thumbnail.mapped));
}

void Presenter::Present(Frame* frame, bool is_reusing_frame, bool is_game_frame) {
    // gow3: a finished save picture copy is read before any present_done fence is reset.
    ReadThumbnail();
    if (thumbnail.pending == frame->present_done) {
        thumbnail.pending = vk::Fence{}; // its fence is reset below: that copy is dropped
    }
    // Free the frame for reuse
    const auto free_frame = [&] {
        if (!is_reusing_frame) {
            last_submit_frame = frame;
            std::scoped_lock fl{free_mutex};
            free_queue.push(frame);
            free_cv.notify_one();
        }
    };

    // gow3: a minimized window has no pixels on Windows (0x0); keep the swapchain and skip
    // No FG active on normal path (unless Streamline DLSS-G / FSR3-FG handled below).
    Gow3Settings::Get().fg_active_now.store(false, std::memory_order_relaxed);
    Gow3Settings::Get().active_fg_multiplier.store(1, std::memory_order_relaxed);

    if (window.GetWidth() == 0 || window.GetHeight() == 0) {
        free_frame();
        return;
    }

    auto& settings = Gow3Settings::Get();
    const int fg_setting = settings.frame_generation.load(std::memory_order_relaxed);
    const int reflex_setting = settings.reflex_mode.load(std::memory_order_relaxed);
    const int fps_cap = Gow3Graphics::PresentLimit(settings.fps_limit.load(std::memory_order_relaxed));
    auto* sl = Streamline::Get();
    const bool sl_ready = sl && sl->IsInitialized();

    const bool want_dlss_g =
        !swapchain.IsHeadless() && (fg_setting >= 2) && sl_ready && sl->IsDlssGSupported();
    const bool want_fsr_fg =
        !swapchain.IsHeadless() && !want_dlss_g && (fg_setting == 1) && fsr_fg_supported_;

    const bool was_fsr_fg = fsr_fg_active_;
    const bool was_dlss_g = dlss_g_active_;
    swapchain.SetFrameGenActive(want_dlss_g, want_fsr_fg);
    dlss_g_active_ = swapchain.IsDlssGActive();
    fsr_fg_active_ = swapchain.IsFsrFgActive();
    if (was_fsr_fg && !fsr_fg_active_) {
        fsr_fg_has_prev_ = false;
    }
    if ((was_dlss_g != dlss_g_active_ || was_fsr_fg != fsr_fg_active_) &&
        ffxVkFrameGenerationTransitionNeedsQuiescence(was_dlss_g || was_fsr_fg,
                                                      dlss_g_active_ || fsr_fg_active_)) {
        (void)Scheduler::WaitDeviceIdle(instance);
    }

    // Update NVIDIA Reflex / frame cap on Streamline when supported (DLSS-G mandates active Reflex).
    const int eff_reflex = (want_dlss_g && reflex_setting == 0) ? 1 : reflex_setting;
    const u32 eff_reflex_cap =
        (want_dlss_g && fg_setting != 7) ? 0u : (fps_cap > 0 ? (1000000u / static_cast<u32>(fps_cap)) : 0u);
    if (sl_ready && sl->IsReflexSupported()) {
        static int s_last_reflex = -1;
        static u32 s_last_limit_us = UINT32_MAX;
        if (s_last_reflex != eff_reflex || s_last_limit_us != eff_reflex_cap) {
            const SlReflexMode rm = eff_reflex == 2 ? SlReflexMode::Boost
                                  : eff_reflex == 1 ? SlReflexMode::On
                                                    : SlReflexMode::Off;
            if (sl->UpdateReflex(rm, eff_reflex_cap)) {
                s_last_reflex = eff_reflex;
                s_last_limit_us = eff_reflex_cap;
            }
        }
    }

    // Recreate the swapchain if the window was resized or VSync changed.
    swapchain.UpdateVsync();
    if (window.GetWidth() != swapchain.GetWidth() || window.GetHeight() != swapchain.GetHeight()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
    }

    const auto now_tp = std::chrono::steady_clock::now();
    float present_dt_ms = std::chrono::duration<float, std::milli>(now_tp - last_present_tp_).count();
    last_present_tp_ = now_tp;
    if (present_dt_ms < 0.5f || present_dt_ms > 200.0f) {
        present_dt_ms = 16.6f;
    }

    const bool can_interp =
        is_game_frame && !is_reusing_frame && !frame->fg.in_menu_or_paused &&
        frame->fg.has_motion_depth && !Gow3Overlay::BlocksFrameGen();

    const vk::ImageSubresourceRange color_range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };

    const auto record_blit_to_swapchain = [&](vk::CommandBuffer cb, vk::Image src_image,
                                              u32 src_w, u32 src_h, bool src_is_frame_image,
                                              vk::Image dst_swapchain_image,
                                              vk::ImageView dst_swapchain_view,
                                              const vk::Extent2D& extent) {
        if (EmulatorSettings.IsVkHostMarkersEnabled()) {
            cb.beginDebugUtilsLabelEXT(vk::DebugUtilsLabelEXT{.pLabelName = "Present"});
        }
        const std::array pre_barriers{
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = swapchain.UsesDxgi() ? VK_QUEUE_FAMILY_EXTERNAL
                                                           : VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = swapchain.UsesDxgi()
                                          ? instance.GetGraphicsQueueFamilyIndex()
                                          : VK_QUEUE_FAMILY_IGNORED,
                .image = dst_swapchain_image,
                .subresourceRange = color_range,
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                                 vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange = color_range,
            },
        };
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                           vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                           {}, {}, pre_barriers);
        const vk::ImageBlit blit = MakeImageBlitFit(s32(src_w), s32(src_h), extent.width, extent.height);
        // Skip full-screen clear when the blit already covers the entire swapchain surface.
        static const bool skip_clear = [] {
            const char* env = std::getenv("GOW3_PRESENT_SKIP_CLEAR");
            return !(env && env[0] == '0');
        }();
        const bool covers_all = blit.dstOffsets[0].x == 0 && blit.dstOffsets[0].y == 0 &&
                                u32(blit.dstOffsets[1].x) == extent.width &&
                                u32(blit.dstOffsets[1].y) == extent.height;
        if (!skip_clear || !covers_all) {
            const vk::ClearColorValue black{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
            cb.clearColorImage(dst_swapchain_image, vk::ImageLayout::eTransferDstOptimal, black,
                               color_range);
            const vk::MemoryBarrier clear_done{
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            };
            cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, clear_done, {}, {});
        }
        cb.blitImage(src_image, vk::ImageLayout::eTransferSrcOptimal, dst_swapchain_image,
                     vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);
        if (src_is_frame_image) {
            Gow3Trace::Present(src_w, src_h, extent.width, extent.height,
                               int(swapchain.GetSurfaceFormat().format),
                               int(swapchain.GetPresentMode()), int(swapchain.GetImageCount()),
                               swapchain.Generation(), swapchain.RedundantRecreations(),
                               swapchain.UsesDxgi(), swapchain.IsHeadless(), blit.dstOffsets[0].x,
                               blit.dstOffsets[0].y,
                               u32(blit.dstOffsets[1].x - blit.dstOffsets[0].x),
                               u32(blit.dstOffsets[1].y - blit.dstOffsets[0].y));
            if (is_game_frame && !frame->is_hdr && !thumbnail.failed && !thumbnail.pending &&
                Gow3Thumbnail::Wanted()) {
                CaptureThumbnail(cb, frame);
            }
        }
        const bool overlay = Gow3Overlay::Visible();
        const std::array post_barriers{
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = overlay ? vk::AccessFlagBits::eColorAttachmentRead |
                                               vk::AccessFlagBits::eColorAttachmentWrite
                                         : vk::AccessFlagBits::eNone,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = overlay ? vk::ImageLayout::eColorAttachmentOptimal
                                     : swapchain.FinalLayout(),
                .srcQueueFamilyIndex = swapchain.UsesDxgi() && !overlay
                                          ? instance.GetGraphicsQueueFamilyIndex()
                                          : VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = swapchain.UsesDxgi() && !overlay
                                          ? VK_QUEUE_FAMILY_EXTERNAL : VK_QUEUE_FAMILY_IGNORED,
                .image = dst_swapchain_image,
                .subresourceRange = color_range,
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange = color_range,
            },
        };
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                           vk::PipelineStageFlagBits::eAllCommands,
                           vk::DependencyFlagBits::eByRegion, {}, {}, post_barriers);
        if (overlay) {
            Gow3Overlay::Render(cb, dst_swapchain_view, extent);
            const vk::ImageMemoryBarrier to_present{
                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eNone,
                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .newLayout = swapchain.FinalLayout(),
                .srcQueueFamilyIndex = swapchain.UsesDxgi()
                                          ? instance.GetGraphicsQueueFamilyIndex()
                                          : VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = swapchain.UsesDxgi()
                                          ? VK_QUEUE_FAMILY_EXTERNAL : VK_QUEUE_FAMILY_IGNORED,
                .image = dst_swapchain_image,
                .subresourceRange = color_range,
            };
            cb.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                               vk::PipelineStageFlagBits::eBottomOfPipe,
                               vk::DependencyFlagBits::eByRegion, {}, {}, to_present);
        }
        if (EmulatorSettings.IsVkHostMarkersEnabled()) {
            cb.endDebugUtilsLabelEXT();
        }
    };

    // -------------------------------------------------------------------------
    // Path A: Native AMD FSR 3 Frame Generation (Optical Flow + Interpolation)
    // -------------------------------------------------------------------------
    if (fsr_fg_active_ && !swapchain.UsesOffscreenPresent()) {
        const u32 dw = frame->width;
        const u32 dh = frame->height;
        const u32 rw = std::min(frame->fg.render_width ? frame->fg.render_width : dw, dw);
        const u32 rh = std::min(frame->fg.render_height ? frame->fg.render_height : dh, dh);
        const vk::Format frame_fmt = swapchain.GetSurfaceFormat().format;
        const bool fg_ready =
            can_interp && EnsureFsrFgContext(rw, rh, dw, dh, frame_fmt);

        if (!fg_ready) {
            fsr_fg_has_prev_ = false;
        }

        // Fall through cleanly if semaphores aren't available.
        if (fsr_fg_acquire_sems_.size() >= 2 &&
            fsr_fg_render_sems_.size() == swapchain.GetImageCount()) {
            auto acquire_cb = [](void* ud, VkSemaphore sem, u32* out_idx) -> VkResult {
                return static_cast<Swapchain*>(ud)->AcquireNextImageRaw(sem, out_idx);
            };
            FfxVkFrameGenerationAcquiredPair pair{};
            VkResult acq = ffxVkFrameGenerationAcquirePair(
                acquire_cb, &swapchain, static_cast<VkSemaphore>(fsr_fg_acquire_sems_[0]),
                static_cast<VkSemaphore>(fsr_fg_acquire_sems_[1]), swapchain.GetImageCount(), &pair);

            if (!pair.generatedImageAcquired) {
                swapchain.Recreate(window.GetWidth(), window.GetHeight());
                acq = ffxVkFrameGenerationAcquirePair(
                    acquire_cb, &swapchain, static_cast<VkSemaphore>(fsr_fg_acquire_sems_[0]),
                    static_cast<VkSemaphore>(fsr_fg_acquire_sems_[1]), swapchain.GetImageCount(),
                    &pair);
                if (!pair.generatedImageAcquired) {
                    LOG_WARNING(Render_Vulkan, "FSR3-FG: skipping frame on failed acquire ({})",
                                int(acq));
                    free_frame();
                    return;
                }
            }

            const auto reset_result = instance.GetDevice().resetFences(frame->present_done);
            ASSERT_MSG(reset_result == vk::Result::eSuccess,
                       "Unexpected error resetting present done fence: {}",
                       vk::to_string(reset_result));

            const vk::Extent2D extent = swapchain.GetExtent();
            SetExpectedGameSize(s32(extent.width), s32(extent.height));

            bool interp_dispatched = false;
            bool fg_reset = frame->fg.reset || !fsr_fg_has_prev_;

            if (pair.paired && fg_ready && fsr_fg_context_ && fsr_fg_gen_image_) {
                auto& prep_sched = present_scheduler;
                const auto prep_cmd = prep_sched.CommandBuffer();

                const vk::ImageUsageFlags frame_usage =
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
                const FfxVkPortableImage current_color = DescribeFgImage(
                    frame->image, frame_fmt, dw, dh, frame_usage, vk::ImageAspectFlagBits::eColor,
                    FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
                const bool use_hudless =
                    frame->fg.has_hudless && frame->fg.hudless_image != vk::Image{} &&
                    frame->fg.hudless_format == frame_fmt && frame->fg.output_width == dw &&
                    frame->fg.output_height == dh;
                const FfxVkPortableImage hudless_color =
                    use_hudless
                        ? DescribeFgImage(frame->fg.hudless_image, frame_fmt, dw, dh,
                                          vk::ImageUsageFlagBits::eSampled |
                                              vk::ImageUsageFlagBits::eTransferSrc |
                                              vk::ImageUsageFlagBits::eStorage,
                                          vk::ImageAspectFlagBits::eColor,
                                          FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS)
                        : FfxVkPortableImage{.structSize = sizeof(FfxVkPortableImage)};
                const FfxVkPortableImage& interp_src = use_hudless ? hudless_color : current_color;

                const FfxVkPortableImage depth_img = DescribeFgImage(
                    frame->fg.depth_image, frame->fg.depth_format, rw, rh,
                    vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eDepth,
                    FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
                const FfxVkPortableImage mvec_img = DescribeFgImage(
                    frame->fg.mvec_image, frame->fg.mvec_format, rw, rh,
                    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
                    vk::ImageAspectFlagBits::eColor,
                    FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
                const FfxVkPortableImage output_img = DescribeFgImage(
                    fsr_fg_gen_image_, fsr_fg_format_, dw, dh,
                    vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
                    vk::ImageAspectFlagBits::eColor,
                    FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);

                // Transition generated frame target into eGeneral before FSR3-FG writes it.
                const vk::ImageMemoryBarrier2 gen_init{
                    .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                    .srcAccessMask = vk::AccessFlagBits2::eNone,
                    .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                    .dstAccessMask = vk::AccessFlagBits2::eShaderWrite |
                                     vk::AccessFlagBits2::eShaderRead,
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .image = fsr_fg_gen_image_,
                    .subresourceRange = color_range,
                };
                prep_cmd.pipelineBarrier2(
                    {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &gen_init});

                const auto& c = frame->fg.constants;
                const float cam_near = std::max(0.01f, c.camera_near);
                const float cam_far = std::max(cam_near + 1.0f, c.camera_far);
                const float cam_fov = std::clamp(c.camera_fov, 0.1f, 3.0f);
                const FfxVkPortableTransferFunction tf =
                    frame->is_hdr ? FFX_VK_PORTABLE_TRANSFER_FUNCTION_PQ
                                  : FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
                const float min_lum = frame->is_hdr ? 0.001f : 0.0001f;
                const float max_lum = frame->is_hdr ? 1000.0f : 100.0f;

                const u64 fid = ++fsr_fg_frame_id_;
                FfxVkPortableFrameGenerationPrepareInfo prep{};
                prep.structSize = sizeof(prep);
                prep.commandBuffer = prep_cmd;
                prep.depth = depth_img;
                prep.motionVectors = mvec_img;
                prep.renderSize = {rw, rh};
                prep.jitterOffset = {c.jitter_x, c.jitter_y};
                prep.motionVectorScale = {c.mvec_scale_x, c.mvec_scale_y};
                prep.frameTimeMilliseconds = present_dt_ms;
                prep.cameraNear = cam_near;
                prep.cameraFar = cam_far;
                prep.cameraVerticalFovRadians = cam_fov;
                prep.viewSpaceToMeters = 1.0f;
                prep.minLuminance = min_lum;
                prep.maxLuminance = max_lum;
                prep.transferFunction = tf;
                prep.cameraPosition = {c.camera_pos[0], c.camera_pos[1], c.camera_pos[2]};
                prep.cameraUp = {c.camera_up[0], c.camera_up[1], c.camera_up[2]};
                prep.cameraRight = {c.camera_right[0], c.camera_right[1], c.camera_right[2]};
                prep.cameraForward = {c.camera_fwd[0], c.camera_fwd[1], c.camera_fwd[2]};
                prep.reset = fg_reset ? VK_TRUE : VK_FALSE;
                prep.frameId = fid;

                FfxVkPortableFrameGenerationDispatchInfo disp{};
                disp.structSize = sizeof(disp);
                disp.commandBuffer = prep_cmd;
                disp.currentColor = current_color;
                disp.hudlessColor = hudless_color;
                disp.distortionField.structSize = sizeof(disp.distortionField);
                disp.output = output_img;
                disp.displaySize = {dw, dh};
                disp.interpolationRect = {0, 0, dw, dh};
                disp.frameTimeMilliseconds = present_dt_ms;
                disp.cameraNear = cam_near;
                disp.cameraFar = cam_far;
                disp.cameraVerticalFovRadians = cam_fov;
                disp.viewSpaceToMeters = 1.0f;
                disp.minLuminance = min_lum;
                disp.maxLuminance = max_lum;
                disp.transferFunction = tf;
                disp.reset = fg_reset ? VK_TRUE : VK_FALSE;
                disp.frameId = fid;

                if (ffxVkPortableFrameGenerationContextPrepare(fsr_fg_context_, &prep,
                                                             &interp_src) == FFX_VK_PORTABLE_OK &&
                    ffxVkPortableFrameGenerationContextRecordDispatch(fsr_fg_context_, &disp) ==
                        FFX_VK_PORTABLE_OK) {
                    const vk::ImageMemoryBarrier2 gen_ready{
                        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite |
                                         vk::AccessFlagBits2::eMemoryWrite,
                        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
                        .oldLayout = vk::ImageLayout::eGeneral,
                        .newLayout = vk::ImageLayout::eGeneral,
                        .image = fsr_fg_gen_image_,
                        .subresourceRange = color_range,
                    };
                    prep_cmd.pipelineBarrier2(
                        {.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &gen_ready});
                    interp_dispatched = true;
                    fsr_fg_has_prev_ = true;
                }
            }

            FfxVkFrameGenerationPresentPlan plan{};
            if (!ffxVkFrameGenerationBuildPresentPlan(&pair, interp_dispatched, fg_reset, &plan)) {
                // Stranded/duplicate acquire pair: reset swapchain safely.
                (void)Scheduler::WaitDeviceIdle(instance);
                SubmitInfo panic_info{};
                panic_info.AddSignal(frame->present_done);
                present_scheduler.Flush(panic_info);
                swapchain.Recreate(window.GetWidth(), window.GetHeight());
                fsr_fg_has_prev_ = false;
                free_frame();
                return;
            }

            bool any_presented = true;
            for (u32 s = 0; s < plan.slotCount; ++s) {
                const auto& slot = plan.slots[s];
                const bool use_gen = slot.useInterpolatedScene && interp_dispatched;
                const vk::Image src_img = use_gen ? fsr_fg_gen_image_ : frame->image;
                const vk::Image dst_img = swapchain.ImageAt(slot.imageIndex);
                const vk::ImageView dst_view = swapchain.ImageViewAt(slot.imageIndex);
                const vk::Semaphore render_sem = fsr_fg_render_sems_[slot.imageIndex];
                const auto cb = present_scheduler.CommandBuffer();

                record_blit_to_swapchain(cb, src_img, dw, dh, !use_gen && s == plan.slotCount - 1,
                                         dst_img, dst_view, extent);

                SubmitInfo sinfo{};
                if (slot.imageAvailableSemaphore != VK_NULL_HANDLE) {
                    sinfo.AddWait(vk::Semaphore(slot.imageAvailableSemaphore));
                }
                if (s == 0) {
                    sinfo.AddWait(frame->ready_semaphore, frame->ready_tick);
                }
                sinfo.AddSignal(render_sem);
                if (s == plan.slotCount - 1) {
                    sinfo.AddSignal(frame->present_done);
                }
                present_scheduler.Flush(sinfo);

                std::scoped_lock present_lock{PresentLock(instance)};
                if (!swapchain.PresentImage(slot.imageIndex, render_sem, s == plan.slotCount - 1)) {
                    any_presented = false;
                }
            }

            const bool fg_live = interp_dispatched && !fg_reset && plan.slotCount == 2;
            settings.fg_active_now.store(fg_live, std::memory_order_relaxed);
            settings.active_fg_multiplier.store(fg_live ? 2 : 1, std::memory_order_relaxed);

            if (!any_presented && window.GetWidth() != 0 && window.GetHeight() != 0) {
                swapchain.Recreate(window.GetWidth(), window.GetHeight());
            }
            free_frame();
            if (!is_reusing_frame && is_game_frame) {
                DebugState.IncFlipFrameNum();
            }
            return;
        }
    }

    // -------------------------------------------------------------------------
    // Path B: Normal Swapchain + Streamline DLSS-G & Reflex PCL Telemetry
    // -------------------------------------------------------------------------
    if (!swapchain.AcquireNextImage()) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
        if (!swapchain.AcquireNextImage()) {
            LOG_WARNING(Render_Vulkan, "Skipping frame!");
            free_frame();
            return;
        }
    }

    const auto reset_result = instance.GetDevice().resetFences(frame->present_done);
    ASSERT_MSG(reset_result == vk::Result::eSuccess,
               "Unexpected error resetting present done fence: {}", vk::to_string(reset_result));

    const vk::Image swapchain_image = swapchain.Image();
    auto& scheduler = present_scheduler;
    const auto cmdbuf = scheduler.CommandBuffer();
    const vk::Extent2D extent = swapchain.GetExtent();
    SetExpectedGameSize(s32(extent.width), s32(extent.height));

    record_blit_to_swapchain(cmdbuf, frame->image, frame->width, frame->height, true,
                             swapchain_image, swapchain.ImageView(), extent);

    // Ensure valid Streamline frame token for Reflex PCL & DLSS-G tagging.
    // Do not reuse a prior rendered frame index on repeated/held flips.
    u32 sl_idx = is_reusing_frame ? 0 : frame->sl_frame_index;
    if (sl_ready && sl_idx == 0) {
        sl_idx = rasterizer ? rasterizer->GetUpscaler().BeginNextSlFrame() : ++telemetry_tick_;
        const SlReflexMode rm = eff_reflex == 2 ? SlReflexMode::Boost
                              : eff_reflex == 1 ? SlReflexMode::On
                                                : SlReflexMode::Off;
        sl->BeginFrame(sl_idx, rm, eff_reflex_cap);
        sl->SetConstants(frame->fg.constants, sl_idx);
    }

    // Configure & tag Streamline DLSS-G when enabled on native Vulkan WSI.
    int dlss_g_mult = 1;
    if (dlss_g_active_ && sl_ready) {
        const u32 target_gen = fg_setting == 2 ? 1u
                             : fg_setting == 3 ? 2u
                             : fg_setting == 4 ? 3u
                             : fg_setting == 5 ? 4u
                             : fg_setting == 6 ? 5u
                             : fg_setting >= 7 ? std::max(1u, sl->MaxGeneratedFrames())
                                               : 1u;
        const u32 gen_clamped = std::clamp(target_gen, 1u, std::max(1u, sl->MaxGeneratedFrames()));
        const SlDlssGMode gmode = !can_interp   ? SlDlssGMode::Off
                                : fg_setting == 7 ? (sl->IsDynamicMfgSupported()
                                                         ? SlDlssGMode::Dynamic
                                                         : SlDlssGMode::On)
                                                  : SlDlssGMode::On;
        dlss_g_mult = (gmode != SlDlssGMode::Off) ? static_cast<int>(gen_clamped + 1) : 1;

        const bool use_hudless =
            frame->fg.has_hudless && frame->fg.hudless_image &&
            frame->fg.hudless_format != vk::Format::eR16G16B16A16Sfloat &&
            frame->fg.output_width == extent.width && frame->fg.output_height == extent.height;

        Streamline::DlssGDesc gdesc{};
        gdesc.mode = gmode;
        gdesc.num_frames_to_generate = gen_clamped;
        gdesc.dynamic_target_fps = fps_cap > 0 ? static_cast<u32>(fps_cap) : 0u;
        gdesc.render_width = frame->fg.render_width ? frame->fg.render_width : extent.width;
        gdesc.render_height = frame->fg.render_height ? frame->fg.render_height : extent.height;
        gdesc.output_width = extent.width;
        gdesc.output_height = extent.height;
        gdesc.color_format = swapchain.GetSurfaceFormat().format;
        gdesc.hudless_format = use_hudless ? frame->fg.hudless_format : vk::Format::eUndefined;
        gdesc.depth_format = frame->fg.depth_format;
        gdesc.mvec_format = frame->fg.mvec_format;
        gdesc.ui_recomposition = use_hudless;
        sl->UpdateDlssG(gdesc);

        if (gmode != SlDlssGMode::Off && frame->fg.has_motion_depth) {
            if (frame->fg.depth_image) {
                Streamline::ImageResource depth_res{
                    frame->fg.depth_image,
                    VK_NULL_HANDLE,
                    frame->fg.depth_view,
                    frame->fg.depth_format,
                    vk::ImageLayout::eGeneral,
                    frame->fg.render_width,
                    frame->fg.render_height,
                    1,
                    1,
                    static_cast<u32>(vk::ImageUsageFlagBits::eSampled |
                                     vk::ImageUsageFlagBits::eTransferDst),
                };
                sl->TagImage(cmdbuf, 0 /* kBufferTypeDepth */, depth_res, true,
                             frame->fg.render_width, frame->fg.render_height, sl_idx);
            }
            if (frame->fg.mvec_image) {
                Streamline::ImageResource mvec_res{
                    frame->fg.mvec_image,
                    VK_NULL_HANDLE,
                    frame->fg.mvec_view,
                    frame->fg.mvec_format,
                    vk::ImageLayout::eGeneral,
                    frame->fg.render_width,
                    frame->fg.render_height,
                    1,
                    1,
                    static_cast<u32>(vk::ImageUsageFlagBits::eSampled |
                                     vk::ImageUsageFlagBits::eStorage),
                };
                sl->TagImage(cmdbuf, 1 /* kBufferTypeMotionVectors */, mvec_res, true,
                             frame->fg.render_width, frame->fg.render_height, sl_idx);
            }
            if (use_hudless) {
                Streamline::ImageResource hudless_res{
                    frame->fg.hudless_image,
                    VK_NULL_HANDLE,
                    frame->fg.hudless_view,
                    frame->fg.hudless_format,
                    vk::ImageLayout::eGeneral,
                    extent.width,
                    extent.height,
                    1,
                    1,
                    static_cast<u32>(vk::ImageUsageFlagBits::eSampled |
                                     vk::ImageUsageFlagBits::eStorage),
                };
                sl->TagImage(cmdbuf, 2 /* kBufferTypeHUDLessColor */, hudless_res, true,
                             extent.width, extent.height, sl_idx);
            }
        }
    }

    SubmitInfo info{};
    if (!swapchain.UsesOffscreenPresent()) info.AddWait(swapchain.GetImageAcquiredSemaphore());
    info.AddWait(frame->ready_semaphore, frame->ready_tick);
    if (!swapchain.UsesOffscreenPresent()) info.AddSignal(swapchain.GetPresentReadySemaphore());
    vk::Semaphore gpu_sync_sem{};
    u64 gpu_sync_val = 0;
    const bool gpu_sync_armed = swapchain.UsesOffscreenPresent() &&
                                swapchain.PrepareGpuPresentSync(gpu_sync_sem, gpu_sync_val);
    if (gpu_sync_armed) {
        info.AddSignal(gpu_sync_sem, gpu_sync_val);
    }
    info.AddSignal(frame->present_done);
    scheduler.Flush(info);
    // When offscreen/DXGI lacks a shared GPU timeline fence (e.g. Win7 or DX9), wait ONLY on
    // this present submission's fence instead of draining the entire device queue.
    if (swapchain.UsesOffscreenPresent() && !gpu_sync_armed) {
        (void)instance.GetDevice().waitForFences(frame->present_done, VK_TRUE, 2'000'000'000ULL);
    }

    if (sl_ready && sl_idx != 0) {
        sl->SetPclMarker(SlPclMarker::PresentStart, sl_idx);
    }

    // Present to swapchain.
    bool presented;
    {
        std::scoped_lock present_lock{PresentLock(instance)};
        presented = swapchain.Present();
    }

    if (sl_ready && sl_idx != 0) {
        sl->SetPclMarker(SlPclMarker::PresentEnd, sl_idx);
        const bool update_tel = dlss_g_active_ || (++telemetry_tick_ % 15u) == 0u;
        if (update_tel) {
            const auto tel = sl->QueryTelemetry();
            const int active_mult =
                (dlss_g_active_ && dlss_g_mult > 1 && tel.frames_actually_presented > 1)
                    ? static_cast<int>(tel.frames_actually_presented)
                    : dlss_g_mult;
            const bool fg_live =
                dlss_g_active_ && can_interp && (active_mult > 1) && (tel.dlss_g_status == 0u);
            Gow3Settings::UpdateStreamlineTelemetry(
                tel.latency_report_available ? tel.pc_latency_ms : 0.0f, tel.sim_latency_ms,
                tel.render_latency_ms, tel.gpu_latency_ms, fg_live, fg_live ? active_mult : 1);
        }
    }

    if (!presented && window.GetWidth() != 0 && window.GetHeight() != 0) {
        swapchain.Recreate(window.GetWidth(), window.GetHeight());
    }

    free_frame();
    if (!is_reusing_frame && is_game_frame) {
        DebugState.IncFlipFrameNum();
    }
}

void Presenter::DestroyFsrFgResources() {
    const vk::Device device = instance.GetDevice();
    if (fsr_fg_context_ || fsr_fg_gen_image_ || !fsr_fg_acquire_sems_.empty() ||
        !fsr_fg_render_sems_.empty()) {
        (void)Scheduler::WaitDeviceIdle(instance);
    }
    if (fsr_fg_context_) {
        ffxVkPortableFrameGenerationContextDestroy(fsr_fg_context_);
        fsr_fg_context_ = nullptr;
    }
    if (fsr_fg_gen_view_) {
        device.destroyImageView(fsr_fg_gen_view_);
        fsr_fg_gen_view_ = vk::ImageView{};
    }
    if (fsr_fg_gen_image_) {
        vmaDestroyImage(instance.GetAllocator(), fsr_fg_gen_image_, fsr_fg_gen_alloc_);
        fsr_fg_gen_image_ = vk::Image{};
        fsr_fg_gen_alloc_ = nullptr;
    }
    for (vk::Semaphore sem : fsr_fg_acquire_sems_) {
        if (sem) device.destroySemaphore(sem);
    }
    fsr_fg_acquire_sems_.clear();
    for (vk::Semaphore sem : fsr_fg_render_sems_) {
        if (sem) device.destroySemaphore(sem);
    }
    fsr_fg_render_sems_.clear();
    fsr_fg_render_w_ = 0;
    fsr_fg_render_h_ = 0;
    fsr_fg_display_w_ = 0;
    fsr_fg_display_h_ = 0;
    fsr_fg_format_ = vk::Format::eUndefined;
    fsr_fg_has_prev_ = false;
    fsr_fg_frame_id_ = 0;
}

bool Presenter::EnsureFsrFgContext(u32 rw, u32 rh, u32 dw, u32 dh, vk::Format format) {
    if (!fsr_fg_supported_ || rw == 0 || rh == 0 || dw == 0 || dh == 0 ||
        format == vk::Format::eUndefined) {
        return false;
    }
    const u32 img_count = std::max<u32>(1u, swapchain.GetImageCount());
    const vk::FormatProperties out_props = instance.GetPhysicalDevice().getFormatProperties(format);
    const vk::Format out_fmt =
        (out_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage)
            ? format
            : vk::Format::eR8G8B8A8Unorm;

    if (fsr_fg_context_ && fsr_fg_render_w_ == rw && fsr_fg_render_h_ == rh &&
        fsr_fg_display_w_ == dw && fsr_fg_display_h_ == dh && fsr_fg_format_ == out_fmt &&
        fsr_fg_render_sems_.size() == img_count) {
        return true;
    }

    DestroyFsrFgResources();

    const vk::Device device = instance.GetDevice();
    FfxVkPortableDeviceInfo device_info{};
    device_info.structSize = sizeof(device_info);
    device_info.instance = instance.GetInstance();
    device_info.physicalDevice = instance.GetPhysicalDevice();
    device_info.device = device;
    device_info.getDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    device_info.queue = instance.GetGraphicsQueue();
    device_info.queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex();
    device_info.shaderFloat16Enabled = instance.IsShaderFloat16Enabled();
    device_info.subgroupSizeControlEnabled = instance.IsSubgroupSizeControlEnabled();
    device_info.synchronization2Enabled = VK_TRUE;
    device_info.shaderStorageImageWriteWithoutFormatEnabled = VK_TRUE;

    FfxVkPortableFrameGenerationCreateInfo create_info{};
    create_info.structSize = sizeof(create_info);
    create_info.flags = swapchain.GetHDR() ? FFX_VK_PORTABLE_CONTEXT_HDR_COLOR_INPUT : 0;
    create_info.maxRenderSize = {std::min(rw, dw), std::min(rh, dh)};
    create_info.displaySize = {dw, dh};
    create_info.interpolationSourceFormat = static_cast<VkFormat>(format);
    create_info.outputFormat = static_cast<VkFormat>(out_fmt);

    if (ffxVkPortableFrameGenerationContextCreate(&device_info, &create_info, &fsr_fg_context_) !=
        FFX_VK_PORTABLE_OK) {
        fsr_fg_context_ = nullptr;
        LOG_WARNING(Render_Vulkan, "FSR3 Frame Generation context creation failed");
        return false;
    }

    const VkImageCreateInfo img_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(out_fmt),
        .extent = {dw, dh, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo alloc_ci{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkImage gen_img = VK_NULL_HANDLE;
    if (vmaCreateImage(instance.GetAllocator(), &img_ci, &alloc_ci, &gen_img, &fsr_fg_gen_alloc_,
                       nullptr) != VK_SUCCESS) {
        DestroyFsrFgResources();
        return false;
    }
    fsr_fg_gen_image_ = vk::Image(gen_img);

    const auto [view_res, gen_view] = device.createImageView({
        .image = fsr_fg_gen_image_,
        .viewType = vk::ImageViewType::e2D,
        .format = out_fmt,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    });
    if (view_res != vk::Result::eSuccess) {
        DestroyFsrFgResources();
        return false;
    }
    fsr_fg_gen_view_ = gen_view;

    fsr_fg_acquire_sems_.resize(2);
    for (vk::Semaphore& sem : fsr_fg_acquire_sems_) {
        auto [sr, s] = device.createSemaphore({});
        if (sr != vk::Result::eSuccess) {
            DestroyFsrFgResources();
            return false;
        }
        sem = s;
    }
    fsr_fg_render_sems_.resize(img_count);
    for (vk::Semaphore& sem : fsr_fg_render_sems_) {
        auto [sr, s] = device.createSemaphore({});
        if (sr != vk::Result::eSuccess) {
            DestroyFsrFgResources();
            return false;
        }
        sem = s;
    }

    fsr_fg_render_w_ = create_info.maxRenderSize.width;
    fsr_fg_render_h_ = create_info.maxRenderSize.height;
    fsr_fg_display_w_ = dw;
    fsr_fg_display_h_ = dh;
    fsr_fg_format_ = out_fmt;
    fsr_fg_has_prev_ = false;
    fsr_fg_frame_id_ = 0;
    return true;
}

Frame* Presenter::GetRenderFrame() {
    // Wait for free presentation frames
    Frame* frame;
    // gow3 20 Oct 2026: the time this waits for presentation (counted; Stall lines).
    Gow3Stats::WaitTimer wait_timer{Gow3Stats::frame_wait_ns};
    const auto wait_start = std::chrono::steady_clock::now();
    {
        std::unique_lock lock{free_mutex};
        free_cv.wait(lock, [this] { return !free_queue.empty(); });
        LOG_DEBUG(Render_Vulkan, "Got render frame, remaining {}", free_queue.size() - 1);

        // Take the frame from the queue
        frame = free_queue.front();
        free_queue.pop();
    }

    const vk::Device device = instance.GetDevice();
    vk::Result result{};

    const auto wait = [&]() {
        result = device.waitForFences(frame->present_done, false, std::numeric_limits<u64>::max());
        return result;
    };

    // Wait for the presentation to be finished so all frame resources are free
    while (wait() != vk::Result::eSuccess) {
        ASSERT_MSG(result != vk::Result::eErrorDeviceLost,
                   "Device lost during waiting for a frame");
        // Retry if the waiting times out
        if (result == vk::Result::eTimeout) {
            continue;
        }
    }
    // gow3 1 Nov 2026: a frame that is still being shown or queued for the display (VSync) is
    // what this waits for; see VideoOutDriver::PresentThread (display queue drain).
    if (std::chrono::steady_clock::now() - wait_start > std::chrono::milliseconds(1)) {
        long_frame_waits.fetch_add(1, std::memory_order_relaxed);
    }

    if (frame->width != expected_frame_width || frame->height != expected_frame_height ||
        frame->is_hdr != swapchain.GetHDR()) {
        RecreateFrame(frame, expected_frame_width, expected_frame_height);
    }

    return frame;
}

void Presenter::SetExpectedGameSize(s32 width, s32 height) {
    if (width <= 0 || height <= 0) {
        return; // no surface (minimized window): keep the last frame size
    }
    const float ratio = (float)width / (float)height;

    expected_frame_height = height;
    expected_frame_width = width;
    if (ratio > expected_ratio) {
        expected_frame_width = static_cast<s32>(height * expected_ratio);
    } else {
        expected_frame_height = static_cast<s32>(width / expected_ratio);
    }
}

} // namespace Vulkan
