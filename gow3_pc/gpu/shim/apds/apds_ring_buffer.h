// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - Non-Blocking Ring Buffer
#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace APDS {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 8192)
        : m_capacity(capacity), m_buffer(capacity) {}

    bool Push(const T& item) {
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return false; // Non-blocking drop under heavy contention
        }
        m_buffer[m_head] = item;
        m_head = (m_head + 1) % m_capacity;
        if (m_count < m_capacity) {
            ++m_count;
        } else {
            m_tail = (m_tail + 1) % m_capacity; // overwrite oldest
        }
        return true;
    }

    bool PushMove(T&& item) {
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return false;
        }
        m_buffer[m_head] = std::move(item);
        m_head = (m_head + 1) % m_capacity;
        if (m_count < m_capacity) {
            ++m_count;
        } else {
            m_tail = (m_tail + 1) % m_capacity;
        }
        return true;
    }

    bool Pop(T& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0) return false;
        out = std::move(m_buffer[m_tail]);
        m_tail = (m_tail + 1) % m_capacity;
        --m_count;
        return true;
    }

    size_t Drain(std::vector<T>& out, size_t max_items = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t count = m_count.load();
        if (count == 0) return 0;

        const size_t to_read = (max_items > 0 && max_items < count) ? max_items : count;
        out.reserve(out.size() + to_read);

        for (size_t i = 0; i < to_read; ++i) {
            out.push_back(std::move(m_buffer[m_tail]));
            m_tail = (m_tail + 1) % m_capacity;
            --m_count;
        }
        return to_read;
    }

    size_t Size() const noexcept {
        return m_count.load(std::memory_order_relaxed);
    }

    bool Empty() const noexcept {
        return m_count.load(std::memory_order_relaxed) == 0;
    }

private:
    const size_t m_capacity;
    std::vector<T> m_buffer;
    size_t m_head{0};
    size_t m_tail{0};
    std::atomic<size_t> m_count{0};
    mutable std::mutex m_mutex;
};

} // namespace APDS
