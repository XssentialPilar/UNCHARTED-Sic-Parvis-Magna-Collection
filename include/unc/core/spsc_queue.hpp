// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/core/spsc_queue.hpp
// Uncharted: Sic Parvis Magna Collection — Lock-Free Cache-Aligned SPSC Queue for Time-Sliced Streaming
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace unc::core {

template <typename T, std::size_t Capacity>
class alignas(64) SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "SpscQueue Capacity must be a power of two >= 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "SpscQueue elements must be trivially copyable for lock-free DMA/streaming");

public:
    constexpr SpscQueue() noexcept = default;

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    [[nodiscard]] bool TryPush(const T& item) noexcept {
        const std::size_t write = m_writeIndex.load(std::memory_order_relaxed);
        const std::size_t next = (write + 1U) & kMask;
        if (next == m_readIndex.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        m_storage[write] = item;
        m_writeIndex.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPop(T& outItem) noexcept {
        const std::size_t read = m_readIndex.load(std::memory_order_relaxed);
        if (read == m_writeIndex.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        outItem = m_storage[read];
        m_readIndex.store((read + 1U) & kMask, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t SizeApprox() const noexcept {
        const std::size_t write = m_writeIndex.load(std::memory_order_acquire);
        const std::size_t read = m_readIndex.load(std::memory_order_acquire);
        return (write - read) & kMask;
    }

    [[nodiscard]] bool IsEmpty() const noexcept {
        return m_readIndex.load(std::memory_order_acquire) ==
               m_writeIndex.load(std::memory_order_acquire);
    }

    void Clear() noexcept {
        m_readIndex.store(0U, std::memory_order_release);
        m_writeIndex.store(0U, std::memory_order_release);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1U;

    alignas(64) std::atomic<std::size_t> m_writeIndex{0U};
    alignas(64) std::atomic<std::size_t> m_readIndex{0U};
    alignas(64) std::array<T, Capacity>  m_storage{};
};

} // namespace unc::core
