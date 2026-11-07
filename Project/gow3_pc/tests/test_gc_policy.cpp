// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <vector>
#include "common/lru_cache.h"
#include "video_core/texture_cache/gc_policy.h"

int main() {
    using VideoCore::GcAge;
    static_assert(VideoCore::EffectiveMemoryUsage(5000, 500) == 4500);
    static_assert(VideoCore::EffectiveMemoryUsage(100, 200) == 0);
    static_assert(VideoCore::EffectiveMemoryUsage(100, 100) == 0);
    static_assert(VideoCore::EffectiveMemoryUsage(100, 0) == 100);
    static_assert(GcAge(true, true) < GcAge(true, false));
    static_assert(GcAge(true, false) < GcAge(false, false));
    Common::LeastRecentlyUsedCache<int, u64> cache;
    cache.Insert(1, 10);
    cache.Insert(2, 100);
    cache.Insert(3, 180);
    cache.Insert(4, 200); // Current submission is never a candidate.
    std::vector<int> seen;
    auto collect = [&](bool pressure, bool aggressive) {
        seen.clear();
        cache.ForEachItemBelow(200 - GcAge(pressure, aggressive),
                               [&](int value) { seen.push_back(value); });
        return seen;
    };
    assert((collect(false, false) == std::vector<int>{1}));
    assert((collect(true, false) == std::vector<int>{1, 2}));
    assert((collect(true, true) == std::vector<int>{1, 2, 3}));
}
