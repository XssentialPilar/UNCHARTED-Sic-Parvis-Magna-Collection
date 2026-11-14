// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <type_traits>
#include <utility>
#include "video_core/texture_cache/image.h"

#ifdef _WIN32
// No guest code is entered by this metadata-only test.
extern "C" int gow3_setjmp(void*) { std::abort(); }
#endif

int main() {
    using VideoCore::UniqueImage;
    static_assert(std::is_nothrow_move_constructible_v<UniqueImage>);
    // No image or allocation: test ownership metadata without creating a Vulkan device.
    const vk::Device device{reinterpret_cast<VkDevice>(uintptr_t{1})};
    UniqueImage source{device, nullptr};
    source.size_bytes = 65536;
    source.image_ci.extent = vk::Extent3D{128, 128, 1};
    UniqueImage moved{std::move(source)};
    assert(moved.device == device && moved.size_bytes == 65536);
    assert(moved.image_ci.extent.width == 128);
    assert(!source.image && source.size_bytes == 0);
    UniqueImage assigned;
    assigned.size_bytes = 4096;
    assigned = std::move(moved);
    assert(assigned.device == device && assigned.size_bytes == 65536);
    assert(moved.size_bytes == 0 && !moved.image);
    assigned = std::move(assigned);
    assert(assigned.size_bytes == 65536 && assigned.device == device);
}
