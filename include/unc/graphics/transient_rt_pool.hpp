// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/graphics/transient_rt_pool.hpp
// Uncharted: Sic Parvis Magna Collection — 4-Phase Temporal Render Target Aliasing & Half-Res Post Pool
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace unc::graphics {

enum class FrameRenderPhase : std::uint8_t {
    Phase1_Shadows = 1,        // Shadow Atlas 2048x2048 (16 MB)
    Phase2_Geometry = 2,       // G-Buffer / Scene Color HDR + Depth (64 MB)
    Phase3_Lighting = 3,       // Reuses Transient Surface A (16 MB) for Half-Res SSAO (W/2 x H/2)
    Phase4_PostProcessing = 4  // Ping-Pongs Transient Surfaces A & B for Half-Res Bloom / Motion Blur / Tonemapping
};

struct TransientSurfaceDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint64_t physicalBlockId{0};
    std::uint64_t physicalBytes{0};
    bool          isHalfResolution{false};
};

class alignas(64) TransientRenderTargetPool {
public:
    static constexpr std::uint64_t kShadowAtlasBytes   = 16ULL * 1024ULL * 1024ULL; // 16 MB
    static constexpr std::uint64_t kGBufferPackBytes   = 64ULL * 1024ULL * 1024ULL; // 64 MB
    static constexpr std::uint64_t kTransientSlotBytes = 16ULL * 1024ULL * 1024ULL; // 16 MB (Surface A & B)

    TransientRenderTargetPool() noexcept = default;

    bool Initialize(std::uint32_t internalWidth, std::uint32_t internalHeight) noexcept;

    // Returns the aliased surface descriptor for the requested phase without allocating new VRAM
    [[nodiscard]] TransientSurfaceDesc AcquirePhaseSurface(FrameRenderPhase phase,
                                                           std::uint32_t pingPongIndex = 0U) const noexcept;

    // Total physical VRAM footprint stays capped at 96 MB (Shadow/TransientA alias + GBuffer + TransientB)
    [[nodiscard]] std::uint64_t GetTotalAliasedFootprintBytes() const noexcept {
        return m_totalAliasedBytes;
    }

    [[nodiscard]] std::uint64_t GetUnaliasedFootprintBytes() const noexcept {
        return m_unaliasedBytes;
    }

    [[nodiscard]] std::uint32_t GetHalfResWidth() const noexcept { return m_halfWidth; }
    [[nodiscard]] std::uint32_t GetHalfResHeight() const noexcept { return m_halfHeight; }

private:
    std::uint32_t m_internalWidth{1920U};
    std::uint32_t m_internalHeight{1080U};
    std::uint32_t m_halfWidth{960U};
    std::uint32_t m_halfHeight{540U};
    std::uint64_t m_totalAliasedBytes{96ULL * 1024ULL * 1024ULL};
    std::uint64_t m_unaliasedBytes{144ULL * 1024ULL * 1024ULL};
    bool          m_initialized{false};
};

} // namespace unc::graphics
