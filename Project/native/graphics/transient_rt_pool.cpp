// SPDX-License-Identifier: GPL-2.0-or-later
// native/graphics/transient_rt_pool.cpp
// Uncharted: Sic Parvis Magna Collection — 4-Phase Temporal Render Target Aliasing & Half-Res Post Pool

#include "unc/graphics/transient_rt_pool.hpp"

#include <algorithm>

namespace unc::graphics {

bool TransientRenderTargetPool::Initialize(std::uint32_t internalWidth,
                                           std::uint32_t internalHeight) noexcept {
    if (internalWidth == 0U || internalHeight == 0U) {
        return false;
    }
    m_internalWidth = internalWidth;
    m_internalHeight = internalHeight;
    // Golden Rule Section 3.4: SSAO, Bloom, and Depth of Field execute at Half-Res (W/2 x H/2)
    m_halfWidth = std::max<std::uint32_t>(1U, internalWidth / 2U);
    m_halfHeight = std::max<std::uint32_t>(1U, internalHeight / 2U);

    // Physical blocks:
    // Block 0 (16 MB): Phase 1 Shadow Atlas (2048x2048) -> reused in Phase 3 (Half-Res SSAO Surface A)
    //                  -> reused in Phase 4 (Post Ping-Pong Surface A)
    // Block 1 (64 MB): Phase 2 G-Buffer + Scene HDR + Depth
    // Block 2 (16 MB): Phase 4 Post Ping-Pong Surface B
    m_totalAliasedBytes = kShadowAtlasBytes + kGBufferPackBytes + kTransientSlotBytes; // 96 MB
    m_unaliasedBytes =
        kShadowAtlasBytes + kGBufferPackBytes + (3ULL * kTransientSlotBytes); // 144 MB without aliasing
    m_initialized = true;
    return true;
}

TransientSurfaceDesc TransientRenderTargetPool::AcquirePhaseSurface(
    FrameRenderPhase phase, std::uint32_t pingPongIndex) const noexcept {
    TransientSurfaceDesc desc{};
    switch (phase) {
        case FrameRenderPhase::Phase1_Shadows:
            desc.width = 2048U;
            desc.height = 2048U;
            desc.physicalBlockId = 0ULL; // Physical Block 0 (16 MB)
            desc.physicalBytes = kShadowAtlasBytes;
            desc.isHalfResolution = false;
            break;

        case FrameRenderPhase::Phase2_Geometry:
            desc.width = m_internalWidth;
            desc.height = m_internalHeight;
            desc.physicalBlockId = 1ULL; // Physical Block 1 (64 MB)
            desc.physicalBytes = kGBufferPackBytes;
            desc.isHalfResolution = false;
            break;

        case FrameRenderPhase::Phase3_Lighting:
            // Aliases Physical Block 0 (Surface A, 16 MB) at Half-Resolution (W/2 x H/2) for SSAO
            desc.width = m_halfWidth;
            desc.height = m_halfHeight;
            desc.physicalBlockId = 0ULL;
            desc.physicalBytes = kTransientSlotBytes;
            desc.isHalfResolution = true;
            break;

        case FrameRenderPhase::Phase4_PostProcessing:
        default:
            // Ping-Pongs Physical Block 0 (Surface A) and Physical Block 2 (Surface B) at Half-Res
            desc.width = m_halfWidth;
            desc.height = m_halfHeight;
            desc.physicalBlockId = (pingPongIndex & 1U) == 0U ? 0ULL : 2ULL;
            desc.physicalBytes = kTransientSlotBytes;
            desc.isHalfResolution = true;
            break;
    }
    return desc;
}

} // namespace unc::graphics
