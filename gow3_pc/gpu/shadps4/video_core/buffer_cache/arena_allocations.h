// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace VideoCore {

// Tracks whole allocations, not guest ranges. Only allocations completely covered by reusable
// runs may be retired. Pending sparse unbinds must remain quarantined outside `runs`.
template <typename Handle>
class ArenaAllocations {
public:
    void Register(Handle memory, std::uint64_t bytes) {
        const auto [it, inserted] = allocations.try_emplace(memory, bytes);
        assert(inserted || it->second == bytes);
    }

    template <typename Runs>
    std::vector<std::pair<Handle, std::uint64_t>> TakeFullyFree(const Runs& runs,
                                                               unsigned block_shift) {
        using Range = std::pair<std::uint64_t, std::uint64_t>;
        std::map<Handle, std::vector<Range>> ranges;
        for (const auto& run : runs) {
            const auto it = allocations.find(run.memory);
            if (it == allocations.end() || run.offset > it->second ||
                run.blocks > ((it->second - run.offset) >> block_shift)) {
                continue;
            }
            ranges[run.memory].emplace_back(run.offset, run.offset + (run.blocks << block_shift));
        }
        std::vector<std::pair<Handle, std::uint64_t>> retired;
        for (auto& [memory, pieces] : ranges) {
            std::sort(pieces.begin(), pieces.end());
            std::uint64_t covered = 0;
            for (const auto& [begin, end] : pieces) {
                if (begin > covered) {
                    break; // A live suballocation (or the pool's unused tail) still owns a gap.
                }
                covered = std::max(covered, end);
            }
            const auto it = allocations.find(memory);
            if (covered == it->second) {
                retired.emplace_back(memory, it->second);
                allocations.erase(it);
            }
        }
        return retired;
    }

private:
    std::map<Handle, std::uint64_t> allocations;
};

} // namespace VideoCore
