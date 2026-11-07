// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Comprehensive Mathematical, VRAM & 10,000-Frame Invariant Suite

#include "unc/core.hpp"
#include "unc/graphics.hpp"
#include "unc/input.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

bool NearlyEqual(double a, double b, double eps = 1e-5) noexcept {
    return std::abs(a - b) <= eps;
}

void TestInvariant2_1_ViewportAndAspectDecoupling() {
    using namespace unc::graphics;

    constexpr double aspect_16_9 = ComputeLogicalAspectRatio(1920U, 1080U);
    static_assert(aspect_16_9 > 1.777 && aspect_16_9 < 1.778);

    // 21:9 ultrawide window (2560x1080) presenting a 16:9 logical scene -> Pillarbox
    const ViewportRect vp_pillar = ComputeDecoupledViewport(2560U, 1080U, 16.0 / 9.0);
    assert(NearlyEqual(vp_pillar.width, 1920.0));
    assert(NearlyEqual(vp_pillar.height, 1080.0));
    assert(NearlyEqual(vp_pillar.x, 320.0));
    assert(NearlyEqual(vp_pillar.y, 0.0));

    // 16:10 display (1920x1200) presenting a 16:9 logical scene -> Letterbox
    const ViewportRect vp_letter = ComputeDecoupledViewport(1920U, 1200U, 16.0 / 9.0);
    assert(NearlyEqual(vp_letter.width, 1920.0));
    assert(NearlyEqual(vp_letter.height, 1080.0));
    assert(NearlyEqual(vp_letter.x, 0.0));
    assert(NearlyEqual(vp_letter.y, 60.0));

    // Projection compensation for 21:9 (3440x1440): Hor+ 3D FOV, 16:9 safe HUD container
    const ProjectionCompensation comp_uw = ComputeProjectionCompensation(3440U, 1440U);
    assert(comp_uw.horizontal_fov_scale > 1.3);
    assert(NearlyEqual(comp_uw.vertical_fov_scale, 1.0));
    assert(comp_uw.hud_safe_scale_x < 0.76);
}

void TestInvariant2_2_InputCoordinateNormalizationAndAxisIndependence() {
    using namespace unc::graphics;
    using namespace unc::input;

    const ViewportRect vp{280.0, 120.0, 2000.0, 1200.0, 0.0, 1.0};
    const ClientPoint client_pt{1280.0, 720.0};
    const GamePoint game_pt = RemapClientToGameCoordinates(client_pt, vp, 1280U, 720U);

    assert(NearlyEqual(game_pt.x, 640.0));
    assert(NearlyEqual(game_pt.y, 360.0));

    // Section 5.2 & 8.3: InputMapper stateless normalization across 16:9, 16:10, and 21:9
    const ViewportBounds bounds_16_10{0.0, 60.0, 1920.0, 1080.0};
    const NormalizedGameCoords norm_16_10 =
        InputMapper::NormalizeClientCoordinates(960.0, 600.0, bounds_16_10, 1920U, 1080U);
    assert(NearlyEqual(norm_16_10.xGame, 960.0));
    assert(NearlyEqual(norm_16_10.yGame, 540.0));

    const ViewportBounds bounds_21_9{320.0, 0.0, 1920.0, 1080.0};
    const NormalizedGameCoords norm_21_9 =
        InputMapper::NormalizeClientCoordinates(1280.0, 540.0, bounds_21_9, 1920U, 1080U);
    assert(NearlyEqual(norm_21_9.xGame, 960.0));
    assert(NearlyEqual(norm_21_9.yGame, 540.0));

    // Verify zero cross-axis contamination across 1,000 menu open/close cycles
    UiAxisScrollState uiState{0.0, 250.0};
    for (int i = 0; i < 1000; ++i) {
        InputMapper::ApplyHorizontalCarouselDelta(uiState, 12.5);
        InputMapper::ApplyHorizontalCarouselDelta(uiState, -12.5);
    }
    assert(NearlyEqual(uiState.horizontalCarouselOffset, 0.0));
    assert(NearlyEqual(uiState.verticalInventoryScroll, 250.0));
}

void TestSection3_VramBudgetsBcCompressionAndTransientAliasing() {
    using namespace unc::graphics;

    // 1. Mandatory Block Compression (BC1, BC4, BC5, BC7) & forbidding uncompressed RGBA8/BGRA8
    static_assert(!IsAllowed3DSceneTextureFormat(CompressedDxgiFormat::R8G8B8A8_UNORM));
    static_assert(!IsAllowed3DSceneTextureFormat(CompressedDxgiFormat::B8G8R8A8_UNORM));
    static_assert(IsAllowed3DSceneTextureFormat(
        SelectMandatoryBlockFormat(SceneTextureSemantic::AlbedoOpaque)));
    static_assert(ComputeCompressedMip0Bytes(2048U, 2048U, CompressedDxgiFormat::BC1_UNORM_SRGB) ==
                  2ULL * 1024ULL * 1024ULL);
    static_assert(ComputeCompressedMip0Bytes(2048U, 2048U, CompressedDxgiFormat::BC4_UNORM) ==
                  2ULL * 1024ULL * 1024ULL);
    static_assert(ComputeCompressedMip0Bytes(2048U, 2048U, CompressedDxgiFormat::BC5_UNORM) ==
                  4ULL * 1024ULL * 1024ULL);
    static_assert(ComputeCompressedMip0Bytes(2048U, 2048U, CompressedDxgiFormat::BC7_UNORM_SRGB) ==
                  4ULL * 1024ULL * 1024ULL);
    static_assert(VramBudgetManager::ComputeNextMipBytes(4ULL * 1024ULL * 1024ULL) ==
                  1ULL * 1024ULL * 1024ULL);

    // 2. Mandatory BC5 Normal Z Reconstruction: N_xy = Sample * 2 - 1, N_z = sqrt(max(0, 1 - (Nx^2 + Ny^2)))
    const Float3 flatNormal = ReconstructBc5Normal(0.5f, 0.5f);
    assert(NearlyEqual(flatNormal.x, 0.0, 1e-4));
    assert(NearlyEqual(flatNormal.y, 0.0, 1e-4));
    assert(NearlyEqual(flatNormal.z, 1.0, 1e-4));

    // 3. Section 8.2: 1 GB Hard Cap (859,832,320 bytes = 820 MiB) & 2 GB Hard Cap (1,803,550,720 bytes = 1720 MiB)
    for (std::uint64_t hardCap : {kHardCap1GbVramBytes, kHardCap2GbVramBytes}) {
        VramBudgetManager mgr{};
        assert(mgr.Initialize(nullptr, hardCap));
        // Populate distant (> 5m) textures until simulated usage reaches 92% of hardCap (Critical >= 88%)
        const std::uint64_t ninetyTwoPercent = (hardCap * 92ULL) / 100ULL;
        const std::uint64_t perTexMip0 = 16ULL * 1024ULL * 1024ULL;
        const std::uint64_t perTexTail = 1ULL * 1024ULL * 1024ULL;
        std::uint64_t texId = 1ULL;
        while (mgr.GetLastTelemetry().currentUsageBytes < ninetyTwoPercent && texId <= 160ULL) {
            (void)mgr.RegisterTexture(texId, perTexMip0, perTexTail, 25.0f /* > 5m */, texId, true);
            ++texId;
            if ((texId * (perTexMip0 + perTexTail)) >= ninetyTwoPercent) {
                break;
            }
        }
        const VramTelemetry telemetry = mgr.PollAndEnforceBudget(nullptr, 500ULL);
        assert(telemetry.currentUsageBytes <= hardCap);
        assert(telemetry.nonLocalUsageBytes == 0ULL);
        assert(telemetry.evictedMip0Count > 0U);
        if (hardCap == kHardCap1GbVramBytes) {
            assert(mgr.GetRecommendedMinMipLevel() >= 1U);
        }
    }

    // 4. Section 3.4: TransientRenderTargetPool 4-Phase Temporal Aliasing & Half-Res SSAO/Post
    TransientRenderTargetPool rtPool{};
    assert(rtPool.Initialize(1920U, 1080U));
    assert(rtPool.GetHalfResWidth() == 960U);
    assert(rtPool.GetHalfResHeight() == 540U);
    const TransientSurfaceDesc sShadow =
        rtPool.AcquirePhaseSurface(FrameRenderPhase::Phase1_Shadows);
    const TransientSurfaceDesc sSsao =
        rtPool.AcquirePhaseSurface(FrameRenderPhase::Phase3_Lighting);
    assert(sShadow.physicalBlockId == sSsao.physicalBlockId); // Physical surface A reused!
    assert(sSsao.isHalfResolution);
    assert(rtPool.GetTotalAliasedFootprintBytes() < rtPool.GetUnaliasedFootprintBytes());
}

void TestSection8_ZeroAllocationsAcross10000Presents() {
    using namespace unc::core;
    using namespace unc::graphics;

    LinearArenaAllocator frameArena(1024ULL * 1024ULL); // 1 MB pre-allocated frame arena
    FramePacer framePacer{};
    // Disable actual OS wait during the 10,000-iteration unit test loop so it runs in milliseconds
    VramBudgetManager vramMgr{};
    assert(vramMgr.Initialize(nullptr, kHardCap1GbVramBytes));
    vramMgr.SetSimulatedUsageBytes(500ULL * 1024ULL * 1024ULL);

    SpscQueue<std::size_t, 64> uploadQueue{};
    DynamicRingBuffer ringBuffer{};
    assert(ringBuffer.Initialize(nullptr, kDefaultRingBufferCapacityBytes));

    auto dx9 = CreateDx9Backend();
    assert(dx9->GetApi() == unc::core::GraphicsApi::DirectX9);
    assert(SUCCEEDED(dx9->Initialize(1280U, 720U, true)));
    assert(dx9->IsInitialized());

    auto dx11 = CreateDx11Backend();
    assert(SUCCEEDED(dx11->Initialize(1280U, 720U, true)));
    SwapchainHook hook(dx11.get());

    // Push a 6 MB texture broken into three 2 MB chunks; each frame can upload at most 2.5 MB
    assert(uploadQueue.TryPush(2000000U));
    assert(uploadQueue.TryPush(2000000U));
    assert(uploadQueue.TryPush(2000000U));

    const std::size_t initialCapacity = frameArena.GetCapacityBytes();
    DeterministicPresentTrace trace{};

    // Section 8.1: 10,000 consecutive Present calls with zero heap growth
    for (std::uint64_t frame = 1ULL; frame <= 10000ULL; ++frame) {
        // Allocate per-frame command/UI structures from the O(1) LinearArenaAllocator
        auto* cmdPacket = frameArena.Emplace<ProjectionCompensation>();
        assert(cmdPacket != nullptr);
        assert(frameArena.GetUsedBytes() >= sizeof(ProjectionCompensation));

        const HRESULT hr = ExecuteDeterministic7StepPresent(
            frame, 1U, 0U, framePacer, vramMgr, uploadQueue, ringBuffer, frameArena, hook,
            (frame == 1ULL) ? &trace : nullptr);
        assert(SUCCEEDED(hr));
        assert(frameArena.GetUsedBytes() == 0U);
        assert(frameArena.GetCapacityBytes() == initialCapacity);
    }

    assert(frameArena.GetResetCount() == 10000U);
    assert(trace.stepCount == 7U);
    assert(trace.executedSteps[0] == DeterministicPresentStep::Step1_BeginFrameQpcTimer);
    assert(trace.executedSteps[1] == DeterministicPresentStep::Step2_PollVramAndEnforceLru);
    assert(trace.executedSteps[2] == DeterministicPresentStep::Step3_ProcessTimeSlicedUploadQueue);
    assert(trace.executedSteps[3] ==
           DeterministicPresentStep::Step4_EvaluateDirtyFlagsAndRingBufferConstants);
    assert(trace.executedSteps[4] == DeterministicPresentStep::Step5_RenderAdaptedUiAndHud);
    assert(trace.executedSteps[5] == DeterministicPresentStep::Step6_ResetFrameLinearArena);
    assert(trace.executedSteps[6] ==
           DeterministicPresentStep::Step7_HybridFramePaceAndOriginalPresent);
    // Frame 1 uploaded at most 2.5 MB (2,621,440 bytes)
    assert(trace.uploadedBytesThisFrame <= kDefaultMaxUploadBytesPerFrame);
    assert(uploadQueue.IsEmpty());
}

} // namespace

int main() {
    TestInvariant2_1_ViewportAndAspectDecoupling();
    TestInvariant2_2_InputCoordinateNormalizationAndAxisIndependence();
    TestSection3_VramBudgetsBcCompressionAndTransientAliasing();
    TestSection8_ZeroAllocationsAcross10000Presents();
    std::printf("All Uncharted: Sic Parvis Magna Collection invariants (including 10,000-frame zero-allocation & 1GB/2GB VRAM caps) passed.\n");
    return 0;
}
