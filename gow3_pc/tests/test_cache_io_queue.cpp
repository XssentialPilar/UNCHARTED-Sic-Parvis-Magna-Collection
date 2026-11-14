// SPDX-License-Identifier: GPL-2.0-or-later
#include <atomic>
#include <cassert>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>
#include "video_core/cache_io_queue.h"

int main() {
    using Storage::CacheIoQueue;
    CacheIoQueue queue;
    assert(!queue.Push(CacheIoQueue::Task{[] {}}));
    for (unsigned session = 0; session < 3; ++session) {
        queue.Open();
        std::promise<void> entered, release;
        auto gate = release.get_future().share();
        assert(queue.Push(CacheIoQueue::Task{[&] { entered.set_value(); gate.wait(); }}));
        std::thread worker{[&] { while (auto task = queue.Pop()) (*task)(); }};
        entered.get_future().wait();
        // A failed file operation must not terminate the consumer or lose later requests.
        assert(queue.Push(CacheIoQueue::Task{[] { throw std::runtime_error("write failed"); }}));
        std::vector<std::thread> producers;
        std::vector<std::vector<unsigned>> results(8);
        std::atomic<unsigned> completed{};
        for (unsigned p = 0; p < results.size(); ++p) {
            producers.emplace_back([&, p] {
                for (unsigned i = 0; i < 1000; ++i) {
                    assert(queue.Push(CacheIoQueue::Task{[&, p, i] {
                        results[p].push_back(i);
                        ++completed;
                    }}));
                }
            });
        }
        for (auto& producer : producers) producer.join();
        queue.Close(); // Close while consumer is blocked and 8,001 requests are pending.
        assert(!queue.Push(CacheIoQueue::Task{[] { assert(false); }}));
        release.set_value();
        worker.join();
        assert(completed == 8000);
        for (const auto& result : results) {
            assert(result.size() == 1000);
            for (unsigned i = 0; i < result.size(); ++i) assert(result[i] == i);
        }
        assert(!queue.Pop());
    }
    queue.Open();
    std::thread idle{[&] { assert(!queue.Pop()); }};
    queue.Close(); // An empty blocked consumer must also wake up.
    idle.join();
}
