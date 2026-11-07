// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/graphics/spm_graphics_core.hpp
// Uncharted: Sic Parvis Magna Collection — SPM Native Graphics Architecture Core
#pragma once

#include "unc/core.hpp"
#include "unc/graphics.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace unc::graphics {

/// API-independent Graphics Command Type
enum class SpmCommandType : uint8_t {
    DrawIndexed = 1,
    DrawNonIndexed = 2,
    SetRenderTargets = 3,
    SetViewportScissor = 4,
    SetBlendState = 5,
    SetDepthStencilState = 6,
    SetRasterizerState = 7,
    BindPipeline = 8,
    BindConstantBuffer = 9,
    DispatchCompute = 10,
    MemoryBarrier = 11,
};

/// 64-bit Bitmask Dirty Flags for high-performance zero-overhead state tracking
enum class SpmStateDirtyBits : uint64_t {
    None = 0ULL,
    Pipeline = 1ULL << 0ULL,
    ViewportScissor = 1ULL << 1ULL,
    BlendState = 1ULL << 2ULL,
    DepthStencil = 1ULL << 3ULL,
    Rasterizer = 1ULL << 4ULL,
    VertexBuffers = 1ULL << 5ULL,
    IndexBuffer = 1ULL << 6ULL,
    ConstantBuffers = 1ULL << 7ULL,
    Samplers = 1ULL << 8ULL,
    Textures = 1ULL << 9ULL,
    RenderTargets = 1ULL << 10ULL,
    All = ~0ULL,
};

[[nodiscard]] constexpr SpmStateDirtyBits operator|(SpmStateDirtyBits a, SpmStateDirtyBits b) noexcept {
    return static_cast<SpmStateDirtyBits>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

[[nodiscard]] constexpr SpmStateDirtyBits operator&(SpmStateDirtyBits a, SpmStateDirtyBits b) noexcept {
    return static_cast<SpmStateDirtyBits>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

[[nodiscard]] constexpr bool HasDirtyBit(SpmStateDirtyBits mask, SpmStateDirtyBits bit) noexcept {
    return (static_cast<uint64_t>(mask) & static_cast<uint64_t>(bit)) != 0ULL;
}

/// Primitive topology types supported across all APIs
enum class SpmPrimitiveTopology : uint8_t {
    TriangleList = 0,
    TriangleStrip = 1,
    LineList = 2,
    LineStrip = 3,
    PointList = 4,
};

/// High-performance Intermediate Draw Packet
struct SpmDrawPacket {
    SpmPrimitiveTopology topology{SpmPrimitiveTopology::TriangleList};
    bool is_indexed{true};
    uint32_t index_count{0};
    uint32_t instance_count{1};
    uint32_t first_index{0};
    int32_t vertex_offset{0};
    uint32_t first_instance{0};
    uint64_t pgm_hash_vs{0};
    uint64_t pgm_hash_ps{0};
};

/// Fast State Cache preventing redundant state changes
class SpmStateCache {
public:
    SpmStateCache() noexcept = default;

    void InvalidateAll() noexcept {
        dirty_flags_ = SpmStateDirtyBits::All;
        cached_vs_hash_ = 0;
        cached_ps_hash_ = 0;
        cached_blend_hash_ = 0;
        cached_ds_hash_ = 0;
    }

    [[nodiscard]] bool UpdatePipeline(uint64_t vs_hash, uint64_t ps_hash) noexcept {
        if (vs_hash != cached_vs_hash_ || ps_hash != cached_ps_hash_) {
            cached_vs_hash_ = vs_hash;
            cached_ps_hash_ = ps_hash;
            dirty_flags_ = dirty_flags_ | SpmStateDirtyBits::Pipeline;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool UpdateBlendState(uint32_t blend_hash) noexcept {
        if (blend_hash != cached_blend_hash_) {
            cached_blend_hash_ = blend_hash;
            dirty_flags_ = dirty_flags_ | SpmStateDirtyBits::BlendState;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool UpdateDepthStencilState(uint32_t ds_hash) noexcept {
        if (ds_hash != cached_ds_hash_) {
            cached_ds_hash_ = ds_hash;
            dirty_flags_ = dirty_flags_ | SpmStateDirtyBits::DepthStencil;
            return true;
        }
        return false;
    }

    [[nodiscard]] SpmStateDirtyBits ConsumeDirtyFlags() noexcept {
        const auto flags = dirty_flags_;
        dirty_flags_ = SpmStateDirtyBits::None;
        return flags;
    }

    [[nodiscard]] SpmStateDirtyBits PeekDirtyFlags() const noexcept {
        return dirty_flags_;
    }

private:
    SpmStateDirtyBits dirty_flags_{SpmStateDirtyBits::All};
    uint64_t cached_vs_hash_{0};
    uint64_t cached_ps_hash_{0};
    uint32_t cached_blend_hash_{0};
    uint32_t cached_ds_hash_{0};
};

/// Unified SPM Graphics Backend Interface
class ISpmBackend {
public:
    virtual ~ISpmBackend() = default;

    [[nodiscard]] virtual core::GraphicsApi GetApi() const noexcept = 0;
    [[nodiscard]] virtual std::string_view GetApiName() const noexcept = 0;
    [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;

    virtual HRESULT Initialize(uint32_t width, uint32_t height, void* native_window) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual void BeginFrame() noexcept = 0;
    virtual void EndFrame() noexcept = 0;

    virtual void ExecuteDraw(const SpmDrawPacket& packet) noexcept = 0;
    virtual void Present(uint32_t sync_interval) noexcept = 0;

    virtual void SetViewport(double x, double y, double width, double height, double min_z, double max_z) noexcept = 0;
    virtual void SetScissor(int32_t left, int32_t top, int32_t right, int32_t bottom) noexcept = 0;
};

/// SPM Graphics Core Manager
class SpmGraphicsCore final {
public:
    static SpmGraphicsCore& Instance() noexcept;

    HRESULT Initialize(core::GraphicsApi preferred_api, uint32_t width, uint32_t height, void* hwnd);
    void Shutdown() noexcept;

    [[nodiscard]] ISpmBackend* GetActiveBackend() noexcept { return active_backend_.get(); }
    [[nodiscard]] SpmStateCache& GetStateCache() noexcept { return state_cache_; }
    [[nodiscard]] bool IsInitialized() const noexcept { return is_initialized_; }

    void RecordDraw(const SpmDrawPacket& packet) noexcept;
    void RecordDispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z) noexcept;

    // Statistics
    [[nodiscard]] uint64_t GetTotalDrawCalls() const noexcept { return total_draw_calls_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t GetTotalDispatches() const noexcept { return total_dispatches_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t GetRedundantStateChangesSaved() const noexcept { return redundant_states_saved_.load(std::memory_order_relaxed); }

    void IncRedundantStateSaved() noexcept { redundant_states_saved_.fetch_add(1, std::memory_order_relaxed); }

private:
    SpmGraphicsCore() noexcept = default;
    ~SpmGraphicsCore() { Shutdown(); }

    SpmGraphicsCore(const SpmGraphicsCore&) = delete;
    SpmGraphicsCore& operator=(const SpmGraphicsCore&) = delete;

    std::unique_ptr<ISpmBackend> active_backend_{nullptr};
    SpmStateCache state_cache_{};
    bool is_initialized_{false};
    std::atomic<uint64_t> total_draw_calls_{0};
    std::atomic<uint64_t> total_dispatches_{0};
    std::atomic<uint64_t> redundant_states_saved_{0};
};

} // namespace unc::graphics
