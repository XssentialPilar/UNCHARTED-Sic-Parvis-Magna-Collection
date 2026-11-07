// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/core/arena_allocator.hpp
// Uncharted: Sic Parvis Magna Collection — O(1) Cache-Aligned Linear Arena Allocator (Zero Hot-Path Heap)
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>

namespace unc::core {

class alignas(64) LinearArenaAllocator {
public:
    explicit LinearArenaAllocator(std::size_t capacityBytes) noexcept;
    ~LinearArenaAllocator() noexcept;

    LinearArenaAllocator(const LinearArenaAllocator&) = delete;
    LinearArenaAllocator& operator=(const LinearArenaAllocator&) = delete;

    [[nodiscard]] void* Allocate(std::size_t sizeBytes, std::size_t alignment = 16) noexcept;

    template <typename T, typename... Args>
    [[nodiscard]] T* Emplace(Args&&... args) noexcept {
        void* mem = Allocate(sizeof(T), alignof(T));
        return mem ? new (mem) T(static_cast<Args&&>(args)...) : nullptr;
    }

    // Invoked at the end of each Present(): O(1) cost, zero OS calls
    void Reset() noexcept { m_offset = 0; }

    [[nodiscard]] std::size_t GetUsedBytes() const noexcept { return m_offset; }
    [[nodiscard]] std::size_t GetCapacityBytes() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t GetResetCount() const noexcept { return m_resetCount; }

    void ResetAndCount() noexcept {
        m_offset = 0;
        ++m_resetCount;
    }

private:
    std::uint8_t* m_buffer{nullptr};
    std::size_t   m_capacity{0};
    std::size_t   m_offset{0};
    std::size_t   m_resetCount{0};
};

} // namespace unc::core
