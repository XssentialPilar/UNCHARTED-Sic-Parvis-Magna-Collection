// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/graphics/vram_budget_mgr.hpp
// Uncharted: Sic Parvis Magna Collection — Real-Time DXGI Telemetry, 1 GB/2 GB Hard Caps & Mip0 Eviction
#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11_1.h>
#include <dxgi1_4.h>
#else
struct IDXGIAdapter3;
struct ID3D11DeviceContext1;
#endif

namespace unc::graphics {

inline constexpr std::uint64_t kHardCap1GbVramBytes = 859832320ULL;  // 820 MiB hard cap for 1 GB GPUs
inline constexpr std::uint64_t kHardCap2GbVramBytes = 1803550720ULL; // 1720 MiB hard cap for 2 GB GPUs
inline constexpr std::size_t   kDefaultMaxUploadBytesPerFrame = 2621440U; // 2.5 MiB/frame strict DMA slice

enum class VramPressureLevel : std::uint8_t {
    Normal = 0,   // < 80% of budget: Mip 0 allowed
    Elevated = 1, // 80% - 88%: Freeze Mip 0 promotions, force Mip 1
    Critical = 2  // >= 88%: Evict Mip 0 in LRU order + DiscardResource
};

struct VramTelemetry {
    std::uint64_t     budgetBytes{0};
    std::uint64_t     currentUsageBytes{0};
    std::uint64_t     nonLocalUsageBytes{0};
    float             usageRatio{0.0f};
    VramPressureLevel pressure{VramPressureLevel::Normal};
    std::uint32_t     evictedMip0Count{0};
};

// Flat SoA-friendly resident texture tracking entry (zero heap allocations in hot path)
struct ResidentTextureSlot {
    std::uint64_t textureId{0};
    std::uint64_t mip0Bytes{0};
    std::uint64_t pinnedMipTailBytes{0}; // Mip2..MipN (<= 512x512) permanently pinned
    std::uint64_t lastUsedFrame{0};
    float         distanceMeters{0.0f};
    bool          mip0Resident{false};
    bool          active{false};
};

class alignas(64) VramBudgetManager {
public:
    static constexpr std::size_t kMaxTrackedTextures = 256U;

    VramBudgetManager() noexcept = default;
    ~VramBudgetManager() noexcept = default;

    bool Initialize(IDXGIAdapter3* adapter,
                    std::uint64_t hardCapBytes = kHardCap2GbVramBytes) noexcept;

    // Called once at the beginning of every frame (Step 2 of Present hook)
    VramTelemetry PollAndEnforceBudget(ID3D11DeviceContext1* ctx,
                                       std::uint64_t frameIndex) noexcept;

    // Strict per-frame upload quota (default 2.5 MB/frame, Step 3 of Present hook)
    [[nodiscard]] bool RequestUploadSlice(std::size_t bytesToUpload) noexcept;
    void ResetFrameUploadQuota() noexcept { m_uploadedThisFrame = 0; }

    [[nodiscard]] std::uint32_t GetRecommendedMinMipLevel() const noexcept;

    // Deterministic simulation & resident texture registration for 1 GB / 2 GB verification
    bool RegisterTexture(std::uint64_t textureId, std::uint64_t mip0Bytes,
                         std::uint64_t pinnedMipTailBytes, float distanceMeters,
                         std::uint64_t frameIndex, bool promoteMip0) noexcept;
    void SetSimulatedUsageBytes(std::uint64_t simulatedUsageBytes) noexcept;
    [[nodiscard]] std::uint64_t GetHardCapBytes() const noexcept { return m_hardCapBytes; }
    [[nodiscard]] std::size_t GetUploadedThisFrame() const noexcept { return m_uploadedThisFrame; }
    [[nodiscard]] std::size_t GetMaxUploadPerFrame() const noexcept { return m_maxUploadPerFrame; }
    [[nodiscard]] const VramTelemetry& GetLastTelemetry() const noexcept { return m_lastTelemetry; }

    // Mandatory BC5 Mip-sizing math: Bytes(Mip_{k+1}) = 1/4 * Bytes(Mip_k)
    [[nodiscard]] static constexpr std::uint64_t ComputeNextMipBytes(
        std::uint64_t currentMipBytes) noexcept {
        return currentMipBytes >> 2U;
    }

private:
    std::uint32_t EvictDistantMip0Lru(std::uint64_t frameIndex, std::uint64_t targetBytes) noexcept;

    IDXGIAdapter3* m_adapter{nullptr};
    std::uint64_t  m_hardCapBytes{kHardCap2GbVramBytes}; // 1720 MB default for 2 GB GPUs
    std::size_t    m_maxUploadPerFrame{kDefaultMaxUploadBytesPerFrame}; // 2.5 MB/frame
    std::size_t    m_uploadedThisFrame{0};
    std::uint64_t  m_simulatedUsageBytes{0};
    bool           m_useSimulation{false};
    VramTelemetry  m_lastTelemetry{};
    ResidentTextureSlot m_slots[kMaxTrackedTextures]{};
};

} // namespace unc::graphics
