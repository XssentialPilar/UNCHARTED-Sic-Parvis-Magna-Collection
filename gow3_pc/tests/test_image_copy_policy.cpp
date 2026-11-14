// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_image_copy.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace Vulkan;
    assert(DepthCopyRequiresBuffer(vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat));
    assert(DepthCopyRequiresBuffer(vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint));
    assert(DepthCopyRequiresBuffer(vk::Format::eD24UnormS8Uint, vk::Format::eX8D24UnormPack32));
    assert(DepthCopyRequiresBuffer(vk::Format::eD16UnormS8Uint, vk::Format::eD16Unorm));
    assert(!DepthCopyRequiresBuffer(vk::Format::eD32Sfloat, vk::Format::eD32Sfloat));
    assert(!DepthCopyRequiresBuffer(vk::Format::eR32Sfloat, vk::Format::eD32Sfloat));
    // Equal byte sizes do not establish maintenance8 depth/color compatibility.
    assert(!DepthColorFormatsCompatible(vk::Format::eD32SfloatS8Uint, vk::Format::eR8G8B8A8Unorm));
    assert(!DepthColorFormatsCompatible(vk::Format::eD32Sfloat, vk::Format::eB8G8R8A8Unorm));
    for (auto depth : {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
                       vk::Format::eD24UnormS8Uint, vk::Format::eX8D24UnormPack32}) {
        assert(DepthColorFormatsCompatible(depth, vk::Format::eR32Uint));
        assert(DepthColorFormatsCompatible(depth, vk::Format::eR32Sint));
        assert(DepthColorFormatsCompatible(depth, vk::Format::eR32Sfloat));
        assert(!DepthColorFormatsCompatible(depth, vk::Format::eR16Unorm));
    }
    assert(DepthColorFormatsCompatible(vk::Format::eD16UnormS8Uint, vk::Format::eR16Sfloat));
    assert(!DepthColorFormatsCompatible(vk::Format::eD16Unorm, vk::Format::eR32Sfloat));

    vk::BufferImageCopy copy{
        .imageSubresource{.aspectMask = vk::ImageAspectFlagBits::eDepth, .layerCount = 1},
        .imageExtent = {1920, 1200, 1}};
    assert(BufferImageCopySize(copy, vk::Format::eD32SfloatS8Uint) == 1920ULL * 1200 * 4);
    assert(BufferImageCopySize(copy, vk::Format::eD16UnormS8Uint) == 1920ULL * 1200 * 2);
    copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eStencil;
    assert(BufferImageCopySize(copy, vk::Format::eD32SfloatS8Uint) == 1920ULL * 1200);
    copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    // BC mips smaller than one compression block still occupy a full block.
    copy.imageExtent = vk::Extent3D{2, 2, 1};
    assert(BufferImageCopySize(copy, vk::Format::eBc1RgbaUnormBlock) == 8);
    copy.imageExtent = vk::Extent3D{5, 5, 1};
    assert(BufferImageCopySize(copy, vk::Format::eBc1RgbaUnormBlock) == 32);
    // The last row/slice includes only copied texels, not all padding after them.
    copy.bufferRowLength = 8;
    copy.bufferImageHeight = 8;
    copy.imageExtent = vk::Extent3D{3, 2, 1};
    copy.imageSubresource.layerCount = 2;
    assert(BufferImageCopySize(copy, vk::Format::eR8G8B8A8Unorm) == 256 + 32 + 12);
    copy.bufferRowLength = copy.bufferImageHeight = 0;
    copy.imageExtent = vk::Extent3D{16384, 16384, 1};
    copy.imageSubresource.layerCount = 8;
    assert(BufferImageCopySize(copy, vk::Format::eR32Uint) == (8ULL << 30));
    std::puts("Image copy format, aspect, compressed mip and 64-bit size checks passed");
}
