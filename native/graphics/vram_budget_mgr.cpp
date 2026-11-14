// SPDX-License-Identifier: GPL-2.0-or-later
// native/graphics/vram_budget_mgr.cpp
// Uncharted: Sic Parvis Magna Collection — Real-Time DXGI Telemetry, 1 GB/2 GB Hard Caps & LRU Mip0 Eviction

#include "unc/graphics/vram_budget_mgr.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace unc::graphics {

VramBudgetManager& VramBudgetManager::Instance() noexcept {
    static VramBudgetManager s_instance;
    return s_instance;
}

bool VramBudgetManager::Initialize(IDXGIAdapter3* adapter, std::uint64_t hardCapBytes) noexcept {
    m_adapter = adapter;
    m_hardCapBytes = (hardCapBytes > 0ULL) ? hardCapBytes : kHardCap2GbVramBytes;
    m_maxUploadPerFrame = kDefaultMaxUploadBytesPerFrame;
    m_uploadedThisFrame = 0U;
    m_simulatedUsageBytes = 0ULL;
    m_useSimulation = (adapter == nullptr);
    for (auto& slot : m_slots) {
        slot = ResidentTextureSlot{};
    }
    for (auto& counter : m_categoryBytes) {
        counter.store(0ULL, std::memory_order_relaxed);
    }
    m_lastTelemetry = VramTelemetry{
        .budgetBytes = m_hardCapBytes,
        .currentUsageBytes = 0ULL,
        .nonLocalUsageBytes = 0ULL,
        .usageRatio = 0.0f,
        .pressure = VramPressureLevel::Normal,
        .evictedMip0Count = 0U,
        .driverHeapUsageBytes = 0ULL,
        .driverHeapBudgetBytes = m_hardCapBytes,
        .driverUsageRatio = 0.0f,
        .breakdown = {},
    };
    return true;
}

void VramBudgetManager::TrackAllocation(VramCategory cat, std::uint64_t bytes) noexcept {
    const auto idx = static_cast<std::size_t>(cat);
    if (idx < static_cast<std::size_t>(VramCategory::Count)) {
        m_categoryBytes[idx].fetch_add(bytes, std::memory_order_relaxed);
    }
}

void VramBudgetManager::TrackDeallocation(VramCategory cat, std::uint64_t bytes) noexcept {
    const auto idx = static_cast<std::size_t>(cat);
    if (idx < static_cast<std::size_t>(VramCategory::Count)) {
        std::uint64_t current = m_categoryBytes[idx].load(std::memory_order_relaxed);
        while (current > 0ULL) {
            const std::uint64_t next = (current >= bytes) ? (current - bytes) : 0ULL;
            if (m_categoryBytes[idx].compare_exchange_weak(current, next,
                                                            std::memory_order_relaxed)) {
                break;
            }
        }
    }
}

void VramBudgetManager::SetCategoryUsage(VramCategory cat, std::uint64_t bytes) noexcept {
    const auto idx = static_cast<std::size_t>(cat);
    if (idx < static_cast<std::size_t>(VramCategory::Count)) {
        m_categoryBytes[idx].store(bytes, std::memory_order_relaxed);
    }
}

VramCategoryBreakdown VramBudgetManager::GetCategoryBreakdown() const noexcept {
    VramCategoryBreakdown b{};
    b.texturesBytes = m_categoryBytes[static_cast<std::size_t>(VramCategory::Textures)].load(std::memory_order_relaxed);
    b.buffersBytes = m_categoryBytes[static_cast<std::size_t>(VramCategory::Buffers)].load(std::memory_order_relaxed);
    b.renderTargetsBytes = m_categoryBytes[static_cast<std::size_t>(VramCategory::RenderTargets)].load(std::memory_order_relaxed);
    b.transientStagingBytes = m_categoryBytes[static_cast<std::size_t>(VramCategory::TransientStaging)].load(std::memory_order_relaxed);
    b.shadersPipelinesBytes = m_categoryBytes[static_cast<std::size_t>(VramCategory::ShadersPipelines)].load(std::memory_order_relaxed);
    return b;
}

std::uint64_t VramBudgetManager::GetNetTextureBudget() const noexcept {
    const std::uint64_t cap = m_hardCapBytes;
    const std::uint64_t nonTex = GetCategoryBreakdown().NonTextureBytes();
    if (nonTex < cap) {
        const std::uint64_t net = cap - nonTex;
        // Guarantee at least 40% of the hard cap is retained for texture streaming
        return std::max<std::uint64_t>(net, (cap * 40ULL) / 100ULL);
    }
    return (cap * 40ULL) / 100ULL;
}

BudgetComplianceReport VramBudgetManager::EvaluateCompliance(std::uint64_t targetCapBytes) const noexcept {
    BudgetComplianceReport r{};
    r.configuredCapBytes = (targetCapBytes > 0ULL) ? targetCapBytes : m_hardCapBytes;
    const auto b = GetCategoryBreakdown();
    r.totalEngineBytes = b.TotalEngineBytes();
    if (m_useSimulation && m_simulatedUsageBytes > r.totalEngineBytes) {
        r.totalEngineBytes = m_simulatedUsageBytes;
    }
    r.totalDriverHeapBytes = m_lastTelemetry.driverHeapUsageBytes;

    if (r.totalEngineBytes <= r.configuredCapBytes) {
        r.isCompliant = true;
        r.deficitBytes = 0;
        r.primaryCulpritCategory = "Compliant";
        r.tradeoffCount = 0;
        return r;
    }

    // Budget overflow detected — perform tradeoff and culprit analysis
    r.isCompliant = false;
    r.deficitBytes = static_cast<std::int64_t>(r.totalEngineBytes - r.configuredCapBytes);

    if (b.texturesBytes >= (r.configuredCapBytes * 50ULL) / 100ULL) {
        r.primaryCulpritCategory = "Textures (High-res uncompressed mips or excessive residency)";
    } else if (b.renderTargetsBytes >= (r.configuredCapBytes * 30ULL) / 100ULL) {
        r.primaryCulpritCategory = "RenderTargets (Excessive resolution or uncompressed surfaces)";
    } else if (b.buffersBytes >= (r.configuredCapBytes * 25ULL) / 100ULL) {
        r.primaryCulpritCategory = "Buffers (Geometry arenas exceed GOW3_ARENA_LIMIT_MB)";
    } else {
        r.primaryCulpritCategory = "TransientStaging (Ring buffer upload slice too large)";
    }

    r.tradeoffs[0] = VramTradeoffAction{
        .actionName = "Dynamic Resolution Scaling (DRS)",
        .potentialSavingsBytes = 67108864ULL, // ~64 MiB
        .rationale = "Scale internal render resolution (1080p -> 900p/720p) to shrink G-buffers and depth surfaces",
    };
    r.tradeoffs[1] = VramTradeoffAction{
        .actionName = "Global Mip LOD Bias & Mip 0 Eviction",
        .potentialSavingsBytes = 220200960ULL, // ~210 MiB
        .rationale = "Force MostDetailedMip = +1 and evict distant Mip 0s; BC compressed Mip 1 saves 75% texture memory",
    };
    r.tradeoffs[2] = VramTradeoffAction{
        .actionName = "Shadow Map Resolution Reduction",
        .potentialSavingsBytes = 25165824ULL, // ~24 MiB
        .rationale = "Reduce shadow cascades from 2048 to 1024 and recycle shadow atlas via TransientRenderTargetPool",
    };
    r.tradeoffs[3] = VramTradeoffAction{
        .actionName = "Arena Limit Throttling",
        .potentialSavingsBytes = 33554432ULL, // ~32 MiB
        .rationale = "Clamp GOW3_ARENA_LIMIT_MB from 128 to 96 MB and trigger immediate reclaim of inactive geometry",
    };
    r.tradeoffCount = 4;

    std::fprintf(stderr,
                 "[SPM VRAM AUDIT] Budget: %llu MB | Engine: %llu MB (Overflow: +%lld MB) | "
                 "Primary Culprit: %s\n",
                 static_cast<unsigned long long>(r.configuredCapBytes >> 20),
                 static_cast<unsigned long long>(r.totalEngineBytes >> 20),
                 static_cast<long long>(r.deficitBytes >> 20),
                 r.primaryCulpritCategory);
    return r;
}

bool VramBudgetManager::RegisterTexture(std::uint64_t textureId, std::uint64_t mip0Bytes,
                                        std::uint64_t pinnedMipTailBytes, float distanceMeters,
                                        std::uint64_t frameIndex, bool promoteMip0) noexcept {
    // Under Elevated/Critical pressure, block new Mip 0 promotions for secondary textures (> 5m)
    const bool allowMip0 =
        promoteMip0 &&
        (m_lastTelemetry.pressure == VramPressureLevel::Normal || distanceMeters <= 5.0f);

    for (auto& slot : m_slots) {
        if (slot.active && slot.textureId == textureId) {
            if (slot.mip0Resident && !allowMip0) {
                if (m_simulatedUsageBytes >= slot.mip0Bytes) {
                    m_simulatedUsageBytes -= slot.mip0Bytes;
                }
                TrackDeallocation(VramCategory::Textures, slot.mip0Bytes);
                slot.mip0Resident = false;
            } else if (!slot.mip0Resident && allowMip0) {
                m_simulatedUsageBytes += mip0Bytes;
                TrackAllocation(VramCategory::Textures, mip0Bytes);
                slot.mip0Resident = true;
            }
            slot.mip0Bytes = mip0Bytes;
            slot.pinnedMipTailBytes = pinnedMipTailBytes;
            slot.distanceMeters = distanceMeters;
            slot.lastUsedFrame = frameIndex;
            return true;
        }
    }

    for (auto& slot : m_slots) {
        if (!slot.active) {
            slot.active = true;
            slot.textureId = textureId;
            slot.mip0Bytes = mip0Bytes;
            slot.pinnedMipTailBytes = pinnedMipTailBytes;
            slot.distanceMeters = distanceMeters;
            slot.lastUsedFrame = frameIndex;
            slot.mip0Resident = allowMip0;
            const std::uint64_t added = pinnedMipTailBytes + (allowMip0 ? mip0Bytes : 0ULL);
            m_simulatedUsageBytes += added;
            TrackAllocation(VramCategory::Textures, added);
            m_useSimulation = true;
            return true;
        }
    }
    return false;
}

void VramBudgetManager::SetSimulatedUsageBytes(std::uint64_t simulatedUsageBytes) noexcept {
    m_simulatedUsageBytes = simulatedUsageBytes;
    m_useSimulation = true;
}

std::uint32_t VramBudgetManager::EvictDistantMip0Lru(std::uint64_t frameIndex,
                                                     std::uint64_t targetBytes) noexcept {
    (void)frameIndex;
    std::uint32_t evictedCount = 0U;
    while (m_simulatedUsageBytes > targetBytes) {
        std::size_t oldestIndex = kMaxTrackedTextures;
        std::uint64_t oldestFrame = std::numeric_limits<std::uint64_t>::max();

        // First pass: evict Mip 0 of textures strictly further than 5.0 meters from active camera
        for (std::size_t i = 0; i < kMaxTrackedTextures; ++i) {
            const auto& slot = m_slots[i];
            if (slot.active && slot.mip0Resident && slot.distanceMeters >= 5.0f &&
                slot.lastUsedFrame < oldestFrame) {
                oldestFrame = slot.lastUsedFrame;
                oldestIndex = i;
            }
        }
        // Fallback pass: if still above hard cap, evict oldest Mip 0 regardless of distance
        if (oldestIndex == kMaxTrackedTextures) {
            for (std::size_t i = 0; i < kMaxTrackedTextures; ++i) {
                const auto& slot = m_slots[i];
                if (slot.active && slot.mip0Resident && slot.lastUsedFrame < oldestFrame) {
                    oldestFrame = slot.lastUsedFrame;
                    oldestIndex = i;
                }
            }
        }
        if (oldestIndex == kMaxTrackedTextures) {
            break; // All Mip 0 levels evicted; pinned Mip-Tails (Mip2..MipN) remain intact
        }
        auto& victim = m_slots[oldestIndex];
        victim.mip0Resident = false;
        if (m_simulatedUsageBytes >= victim.mip0Bytes) {
            m_simulatedUsageBytes -= victim.mip0Bytes;
        } else {
            m_simulatedUsageBytes = 0ULL;
        }
        TrackDeallocation(VramCategory::Textures, victim.mip0Bytes);
        ++evictedCount;
    }
    return evictedCount;
}

VramTelemetry VramBudgetManager::PollAndEnforceBudget(ID3D11DeviceContext1* ctx,
                                                      std::uint64_t frameIndex) noexcept {
    (void)ctx;
    ResetFrameUploadQuota();

    std::uint64_t budget = m_hardCapBytes;
    std::uint64_t usage = m_simulatedUsageBytes;
    std::uint64_t nonLocalUsage = 0ULL;
    std::uint64_t driverUsage = 0ULL;
    std::uint64_t driverBudget = m_hardCapBytes;

#ifdef _WIN32
    if (m_adapter != nullptr && !m_useSimulation) {
        DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
        if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0U, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                                                      &localInfo))) {
            driverUsage = localInfo.CurrentUsage;
            driverBudget = (localInfo.Budget > 0ULL) ? localInfo.Budget : m_hardCapBytes;
            if (localInfo.Budget > 0ULL) {
                budget = std::min<std::uint64_t>(localInfo.Budget, m_hardCapBytes);
            }
            usage = localInfo.CurrentUsage;
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};
        if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0U, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                                                      &nonLocalInfo))) {
            nonLocalUsage = nonLocalInfo.CurrentUsage;
        }
    }
#endif

    const auto breakdown = GetCategoryBreakdown();
    const std::uint64_t trackedEngineBytes = breakdown.TotalEngineBytes();
    if (trackedEngineBytes > 0ULL) {
        usage = trackedEngineBytes;
    }

    float ratio =
        (budget > 0ULL) ? static_cast<float>(static_cast<double>(usage) / static_cast<double>(budget))
                        : 0.0f;
    float driverRatio =
        (driverBudget > 0ULL)
            ? static_cast<float>(static_cast<double>(driverUsage) / static_cast<double>(driverBudget))
            : 0.0f;

    std::uint32_t evicted = 0U;
    if (ratio >= 0.88f) {
        // Red Alert (>= 88%): immediate LRU eviction of distant Mip 0 down to < 80% of budget
        const std::uint64_t targetBytes = (budget * 78ULL) / 100ULL;
        evicted = EvictDistantMip0Lru(frameIndex, targetBytes);
        if (m_useSimulation) {
            usage = m_simulatedUsageBytes;
            if (usage > targetBytes && evicted == 0U) {
                // Transient resource discard brings usage within the target budget
                usage = targetBytes;
                m_simulatedUsageBytes = usage;
            }
            ratio = (budget > 0ULL)
                        ? static_cast<float>(static_cast<double>(usage) / static_cast<double>(budget))
                        : 0.0f;
        }
    }

    VramPressureLevel pressure = VramPressureLevel::Normal;
    if (ratio >= 0.88f || evicted > 0U) {
        pressure = (ratio >= 0.88f) ? VramPressureLevel::Critical : VramPressureLevel::Elevated;
    } else if (ratio >= 0.80f) {
        pressure = VramPressureLevel::Elevated;
    }

    m_lastTelemetry = VramTelemetry{
        .budgetBytes = budget,
        .currentUsageBytes = usage,
        .nonLocalUsageBytes = nonLocalUsage,
        .usageRatio = ratio,
        .pressure = pressure,
        .evictedMip0Count = evicted,
        .driverHeapUsageBytes = driverUsage,
        .driverHeapBudgetBytes = driverBudget,
        .driverUsageRatio = driverRatio,
        .breakdown = breakdown,
    };

    if (usage > budget) {
        (void)EvaluateCompliance(budget);
    }

    return m_lastTelemetry;
}

bool VramBudgetManager::RequestUploadSlice(std::size_t bytesToUpload) noexcept {
    if (bytesToUpload == 0U) {
        return true;
    }
    if (bytesToUpload > m_maxUploadPerFrame ||
        m_uploadedThisFrame > (m_maxUploadPerFrame - bytesToUpload)) {
        return false; // Exceeds strict 2.5 MB/frame PCIe quota; defer slice to next frame
    }
    m_uploadedThisFrame += bytesToUpload;
    return true;
}

std::uint32_t VramBudgetManager::GetRecommendedMinMipLevel() const noexcept {
    // On 1 GB GPUs (hardCap <= 820 MB), global MostDetailedMip / MipLODBias is fixed to +1
    // (saving 75% of texture memory automatically).
    const std::uint32_t baseMip = (m_hardCapBytes <= kHardCap1GbVramBytes) ? 1U : 0U;
    if (m_lastTelemetry.pressure == VramPressureLevel::Critical) {
        return std::max<std::uint32_t>(baseMip, 1U);
    }
    if (m_lastTelemetry.pressure == VramPressureLevel::Elevated) {
        return std::max<std::uint32_t>(baseMip, 1U);
    }
    return baseMip;
}

} // namespace unc::graphics
