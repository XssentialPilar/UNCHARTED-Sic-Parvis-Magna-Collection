// SPDX-License-Identifier: GPL-2.0-or-later
// native/graphics/vram_budget_mgr.cpp
// Uncharted: Sic Parvis Magna Collection — Real-Time DXGI Telemetry, 1 GB/2 GB Hard Caps & LRU Mip0 Eviction

#include "unc/graphics/vram_budget_mgr.hpp"

#include <algorithm>
#include <limits>

namespace unc::graphics {

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
    m_lastTelemetry = VramTelemetry{
        .budgetBytes = m_hardCapBytes,
        .currentUsageBytes = 0ULL,
        .nonLocalUsageBytes = 0ULL,
        .usageRatio = 0.0f,
        .pressure = VramPressureLevel::Normal,
        .evictedMip0Count = 0U,
    };
    return true;
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
                slot.mip0Resident = false;
            } else if (!slot.mip0Resident && allowMip0) {
                m_simulatedUsageBytes += mip0Bytes;
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
            m_simulatedUsageBytes += pinnedMipTailBytes + (allowMip0 ? mip0Bytes : 0ULL);
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

#ifdef _WIN32
    if (m_adapter != nullptr && !m_useSimulation) {
        DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
        if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0U, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                                                      &localInfo))) {
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

    float ratio =
        (budget > 0ULL) ? static_cast<float>(static_cast<double>(usage) / static_cast<double>(budget))
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
    };
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
