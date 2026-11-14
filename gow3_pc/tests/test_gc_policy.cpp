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

    constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
    // 1 GB VRAM (e.g. GTX 460 / GTX 550 Ti / GTX 650 / Radeon HD 5770 / HD 7750):
    // All GC thresholds MUST stay strictly below 1024 MiB so GC runs before VRAM exhaustion.
    constexpr auto w1gb = VideoCore::ComputeGcWatermarks(1024 * kMiB);
    static_assert(w1gb.trigger_gc_memory < w1gb.pressure_gc_memory);
    static_assert(w1gb.pressure_gc_memory < w1gb.critical_gc_memory);
    static_assert(w1gb.critical_gc_memory < 1024 * kMiB);

    // 2 GB VRAM (e.g. GTX 750 Ti / GTX 660 / Radeon HD 7850 / R7 265 / R9 270):
    constexpr auto w2gb = VideoCore::ComputeGcWatermarks(2048 * kMiB);
    static_assert(w2gb.trigger_gc_memory < w2gb.pressure_gc_memory);
    static_assert(w2gb.pressure_gc_memory < w2gb.critical_gc_memory);
    static_assert(w2gb.critical_gc_memory < 2048 * kMiB);
}
