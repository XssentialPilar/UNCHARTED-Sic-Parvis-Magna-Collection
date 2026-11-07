// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/graphics/vram_budget_mgr.hpp
// Uncharted: Sic Parvis Magna Collection — Real-Time DXGI Telemetry, 1 GB/2 GB Hard Caps & Mip0 Eviction
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

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

enum class VramCategory : std::uint8_t {
    Textures = 0,         // Resident textures, streaming mips, cubemaps
    Buffers = 1,          // Vertex/index arenas, uniform/storage buffers
    RenderTargets = 2,    // Color targets, depth/stencil, resolve, swapchain
    TransientStaging = 3, // Upload ring buffers, staging memory
    ShadersPipelines = 4, // Pipeline state objects, shader code
    Count = 5
};

struct VramCategoryBreakdown {
    std::uint64_t texturesBytes{0};
    std::uint64_t buffersBytes{0};
    std::uint64_t renderTargetsBytes{0};
    std::uint64_t transientStagingBytes{0};
    std::uint64_t shadersPipelinesBytes{0};

    [[nodiscard]] std::uint64_t TotalEngineBytes() const noexcept {
        return texturesBytes + buffersBytes + renderTargetsBytes +
               transientStagingBytes + shadersPipelinesBytes;
    }

    [[nodiscard]] std::uint64_t NonTextureBytes() const noexcept {
        return buffersBytes + renderTargetsBytes +
               transientStagingBytes + shadersPipelinesBytes;
    }
};

struct VramTradeoffAction {
    const char* actionName{nullptr};
    std::uint64_t potentialSavingsBytes{0};
    const char* rationale{nullptr};
};

struct BudgetComplianceReport {
    bool isCompliant{true};
    std::uint64_t configuredCapBytes{kHardCap1GbVramBytes};
    std::uint64_t totalEngineBytes{0};
    std::uint64_t totalDriverHeapBytes{0};
    std::int64_t  deficitBytes{0}; // > 0 if over budget
    const char*   primaryCulpritCategory{"None"};
    std::size_t   tradeoffCount{0};
    VramTradeoffAction tradeoffs[4]{};
};

struct VramTelemetry {
    std::uint64_t     budgetBytes{0};            // Engine configured hard cap (e.g. 820 MB = 859,832,320 B)
    std::uint64_t     currentUsageBytes{0};      // Engine total managed usage
    std::uint64_t     nonLocalUsageBytes{0};     // Non-local / PCIe shared RAM paging (must be 0)
    float             usageRatio{0.0f};          // currentUsageBytes / budgetBytes
    VramPressureLevel pressure{VramPressureLevel::Normal};
    std::uint32_t     evictedMip0Count{0};

    // Driver & Categorized Breakdown
    std::uint64_t     driverHeapUsageBytes{0};   // System-wide heap usage (OS + DWM + apps)
    std::uint64_t     driverHeapBudgetBytes{0};  // Driver reported heap budget
    float             driverUsageRatio{0.0f};    // driverHeapUsageBytes / driverHeapBudgetBytes
    VramCategoryBreakdown breakdown{};
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

    static VramBudgetManager& Instance() noexcept;

    bool Initialize(IDXGIAdapter3* adapter,
                    std::uint64_t hardCapBytes = kHardCap2GbVramBytes) noexcept;

    // Categorized Allocation Auditing & Tracking
    void TrackAllocation(VramCategory cat, std::uint64_t bytes) noexcept;
    void TrackDeallocation(VramCategory cat, std::uint64_t bytes) noexcept;
    void SetCategoryUsage(VramCategory cat, std::uint64_t bytes) noexcept;
    [[nodiscard]] VramCategoryBreakdown GetCategoryBreakdown() const noexcept;

    // Strict Net Texture Budget Derivation
    [[nodiscard]] std::uint64_t GetNetTextureBudget() const noexcept;

    // Tradeoff & Non-silent Budget Compliance Evaluation
    [[nodiscard]] BudgetComplianceReport EvaluateCompliance(
        std::uint64_t targetCapBytes = 0) const noexcept;

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

    // Centralized atomic allocation counters per category
    std::atomic<std::uint64_t> m_categoryBytes[static_cast<std::size_t>(VramCategory::Count)]{};
};

} // namespace unc::graphics
