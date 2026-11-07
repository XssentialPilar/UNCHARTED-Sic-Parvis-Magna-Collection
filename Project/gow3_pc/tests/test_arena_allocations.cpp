// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdint>
#include <vector>
#include "video_core/buffer_cache/arena_allocations.h"

struct Run {
    unsigned memory;
    std::uint64_t offset, blocks;
};

int main() {
    VideoCore::ArenaAllocations<unsigned> allocations;
    allocations.Register(1, 16 * 4096);
    allocations.Register(1, 16 * 4096); // Multiple suballocations share one physical allocation.
    allocations.Register(2, 8 * 4096);
    std::vector<Run> reusable{{1, 0, 4}, {1, 8 * 4096, 8}, {2, 0, 8}};
    auto retired = allocations.TakeFullyFree(reusable, 12);
    assert((retired == std::vector<std::pair<unsigned, std::uint64_t>>{{2, 8 * 4096}}));
    assert(allocations.TakeFullyFree(reusable, 12).empty()); // Cannot retire twice.

    // The missing middle remains resident or quarantined: neither may be freed early.
    reusable.push_back({1, 0, 4}); // Duplicates do not turn a hole into free space.
    assert(allocations.TakeFullyFree(reusable, 12).empty());
    reusable.push_back({1, 4 * 4096, 4}); // Sparse unbind has now left quarantine.
    retired = allocations.TakeFullyFree(reusable, 12);
    assert((retired == std::vector<std::pair<unsigned, std::uint64_t>>{{1, 16 * 4096}}));

    // A chunk tail abandoned at a transition must be accounted for, even though never bound.
    allocations.Register(3, 64 * 4096);
    reusable = {{3, 0, 60}};
    assert(allocations.TakeFullyFree(reusable, 12).empty());
    reusable.push_back({3, 60 * 4096, 4});
    assert(allocations.TakeFullyFree(reusable, 12).size() == 1);

    // Reject out-of-bounds runs and arithmetic overflow rather than retiring live memory.
    allocations.Register(4, 4096);
    reusable = {{4, UINT64_MAX, 1}, {4, 0, UINT64_MAX}, {99, 0, 1}};
    assert(allocations.TakeFullyFree(reusable, 12).empty());

    // Repeated unload/load cycles must return every wholly idle allocation exactly once.
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        allocations.Register(5, 64 * 4096);
        reusable.clear();
        for (unsigned block = 0; block < 64; ++block) {
            reusable.push_back({5, block * 4096, 1});
            if (block < 63) assert(allocations.TakeFullyFree(reusable, 12).empty());
        }
        assert(allocations.TakeFullyFree(reusable, 12).size() == 1);
        assert(allocations.TakeFullyFree(reusable, 12).empty());
    }
}
