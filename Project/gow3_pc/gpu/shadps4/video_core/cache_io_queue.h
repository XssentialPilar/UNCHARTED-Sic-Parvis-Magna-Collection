// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cassert>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <queue>

namespace Storage {

// A single FIFO and predicate protected by the same mutex. Close rejects new work, wakes the
// consumer, and lets it drain accepted writes before the database or archive is closed.
class CacheIoQueue {
public:
    using Task = std::packaged_task<void()>;

    void Open() {
        std::scoped_lock lock{mutex};
        assert(!accepting && tasks.empty());
        accepting = true;
    }

    bool Push(Task task) {
        {
            std::scoped_lock lock{mutex};
            if (!accepting) {
                return false;
            }
            tasks.push(std::move(task));
        }
        cv.notify_one();
        return true;
    }

    std::optional<Task> Pop() {
        std::unique_lock lock{mutex};
        cv.wait(lock, [&] { return !accepting || !tasks.empty(); });
        if (tasks.empty()) {
            return std::nullopt;
        }
        auto task = std::move(tasks.front());
        tasks.pop();
        return task;
    }

    void Close() {
        {
            std::scoped_lock lock{mutex};
            accepting = false;
        }
        cv.notify_all();
    }

private:
    std::mutex mutex;
    std::condition_variable cv;
    std::queue<Task> tasks;
    bool accepting = false;
};

} // namespace Storage
