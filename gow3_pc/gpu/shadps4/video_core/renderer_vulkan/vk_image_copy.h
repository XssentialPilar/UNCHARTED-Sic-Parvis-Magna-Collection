// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include <vulkan/vulkan_format_traits.hpp>

namespace Vulkan {

constexpr bool HasDepthAspect(vk::Format format) {
    switch (format) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
        return true;
    default:
        return false;
    }
}

// VUID-vkCmdCopyImage-srcImage-01548: two depth/stencil formats must be identical,
// even when the copied depth channels have the same size (D32 and D32S8).
constexpr bool DepthCopyRequiresBuffer(vk::Format src, vk::Format dst) {
    return src != dst && HasDepthAspect(src) && HasDepthAspect(dst);
}

// VK_KHR_maintenance8 permits specific single-component color formats, not every
// format with the same byte count. In particular RGBA8 <-> D32[S8] is not permitted.
constexpr bool DepthColorFormatsCompatible(vk::Format depth, vk::Format color) {
    switch (depth) {
    case vk::Format::eD32Sfloat:
    case vk::Format::eD32SfloatS8Uint:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD24UnormS8Uint:
        return color == vk::Format::eR32Sfloat || color == vk::Format::eR32Sint ||
               color == vk::Format::eR32Uint;
    case vk::Format::eD16Unorm:
    case vk::Format::eD16UnormS8Uint:
        return color == vk::Format::eR16Sfloat || color == vk::Format::eR16Unorm ||
               color == vk::Format::eR16Snorm || color == vk::Format::eR16Uint ||
               color == vk::Format::eR16Sint;
    default:
        return false;
    }
}

inline u64 BufferImageCopySize(const vk::BufferImageCopy& copy, vk::Format format) {
    const auto block = vk::blockExtent(format);
    u64 bytes = vk::blockSize(format);
    if (copy.imageSubresource.aspectMask == vk::ImageAspectFlagBits::eStencil) bytes = 1;
    if (copy.imageSubresource.aspectMask == vk::ImageAspectFlagBits::eDepth) {
        bytes = format == vk::Format::eD16Unorm || format == vk::Format::eD16UnormS8Uint ? 2 : 4;
    }
    const auto ceil_blocks = [](u64 value, u64 size) { return (value + size - 1) / size; };
    const u64 row = copy.bufferRowLength ? copy.bufferRowLength : copy.imageExtent.width;
    const u64 height = copy.bufferImageHeight ? copy.bufferImageHeight : copy.imageExtent.height;
    const u64 pitch = ceil_blocks(row, block[0]) * bytes;
    const u64 slice = ceil_blocks(height, block[1]) * pitch;
    const u64 rows = ceil_blocks(copy.imageExtent.height, block[1]);
    const u64 slices = u64(copy.imageExtent.depth) * copy.imageSubresource.layerCount;
    if (rows == 0 || slices == 0 || copy.imageExtent.width == 0) return 0;
    return (slices - 1) * slice + (rows - 1) * pitch + ceil_blocks(copy.imageExtent.width, block[0]) * bytes;
}

} // namespace Vulkan
