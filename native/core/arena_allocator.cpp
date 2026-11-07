// SPDX-License-Identifier: GPL-2.0-or-later
// native/core/arena_allocator.cpp
// Uncharted: Sic Parvis Magna Collection — O(1) Cache-Aligned Linear Arena Allocator

#include "unc/core/arena_allocator.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace unc::core {

LinearArenaAllocator::LinearArenaAllocator(std::size_t capacityBytes) noexcept
    : m_capacity(capacityBytes), m_offset(0), m_resetCount(0) {
    if (capacityBytes == 0U) {
        return;
    }
#ifdef _WIN32
    m_buffer = static_cast<std::uint8_t*>(_aligned_malloc(capacityBytes, 64U));
#else
    const std::size_t alignedCap = (capacityBytes + 63U) & ~std::size_t(63U);
    m_buffer = static_cast<std::uint8_t*>(std::aligned_alloc(64U, alignedCap));
#endif
}

LinearArenaAllocator::~LinearArenaAllocator() noexcept {
    if (m_buffer != nullptr) {
#ifdef _WIN32
        _aligned_free(m_buffer);
#else
        std::free(m_buffer);
#endif
        m_buffer = nullptr;
    }
    m_capacity = 0;
    m_offset = 0;
}

void* LinearArenaAllocator::Allocate(std::size_t sizeBytes, std::size_t alignment) noexcept {
    if (m_buffer == nullptr || sizeBytes == 0U) {
        return nullptr;
    }
    const std::size_t safeAlign = (alignment == 0U) ? 16U : alignment;
    const std::size_t mask = safeAlign - 1U;
    const std::size_t alignedOffset = (m_offset + mask) & ~mask;
    if (alignedOffset > m_capacity || sizeBytes > (m_capacity - alignedOffset)) {
        return nullptr; // Arena full: zero fallback to OS heap in hot-path
    }
    void* ptr = m_buffer + alignedOffset;
    m_offset = alignedOffset + sizeBytes;
    return ptr;
}

} // namespace unc::core
