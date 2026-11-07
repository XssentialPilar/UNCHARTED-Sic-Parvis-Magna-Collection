// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace VideoCore {

// More pressure must admit newer idle resources, rather than protecting them for longer.
// Actual Vulkan destruction is still deferred until their submitted commands complete.
constexpr std::uint64_t GcAge(bool pressured, bool aggressive) {
    return aggressive ? 16 : pressured ? 80 : 160;
}

// SuperMedo/uncharted_pc 0.3.13 (GPL-2.0-or-later): exclude images already queued for
// destruction so several submissions do not evict more textures for the same pending bytes.
constexpr std::uint64_t EffectiveMemoryUsage(std::uint64_t usage, std::uint64_t pending_free) {
    return usage > pending_free ? usage - pending_free : 0;
}

} // namespace VideoCore
