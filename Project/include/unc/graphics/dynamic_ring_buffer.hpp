// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/graphics/dynamic_ring_buffer.hpp
// Uncharted: Sic Parvis Magna Collection — 16 MB Dynamic Ring Buffer (MAP_WRITE_NO_OVERWRITE / DISCARD)
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
#include <d3d11.h>
#else
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
#endif

namespace unc::graphics {

inline constexpr std::size_t kDefaultRingBufferCapacityBytes = 16ULL * 1024ULL * 1024ULL; // 16 MB

enum class RingMapMode : std::uint8_t {
    WriteNoOverwrite = 0, // D3D11_MAP_WRITE_NO_OVERWRITE
    WriteDiscard = 1      // D3D11_MAP_WRITE_DISCARD (only when wrapping around to 0)
};

struct RingAllocation {
    void*       cpuAddress{nullptr};
    std::size_t byteOffset{0};
    std::size_t sizeBytes{0};
    RingMapMode mapMode{RingMapMode::WriteNoOverwrite};
};

class alignas(64) DynamicRingBuffer {
public:
    DynamicRingBuffer() noexcept = default;
    ~DynamicRingBuffer() noexcept { Shutdown(); }

    DynamicRingBuffer(const DynamicRingBuffer&) = delete;
    DynamicRingBuffer& operator=(const DynamicRingBuffer&) = delete;

    bool Initialize(ID3D11Device* device,
                    std::size_t capacityBytes = kDefaultRingBufferCapacityBytes,
                    std::uint32_t bindFlags = 0x4U /* D3D11_BIND_CONSTANT_BUFFER */) noexcept;
    void Shutdown() noexcept;

    // Uses D3D11_MAP_WRITE_NO_OVERWRITE; applies D3D11_MAP_WRITE_DISCARD only when wrapping around the ring
    [[nodiscard]] bool Allocate(ID3D11DeviceContext* ctx, std::size_t sizeBytes,
                                std::size_t alignment, RingAllocation& outAlloc) noexcept;
    void Unmap(ID3D11DeviceContext* ctx) noexcept;

    [[nodiscard]] ID3D11Buffer* GetBuffer() const noexcept { return m_buffer; }
    [[nodiscard]] std::size_t GetCapacityBytes() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t GetHeadOffset() const noexcept { return m_head; }
    [[nodiscard]] std::uint64_t GetDiscardWrapCount() const noexcept { return m_discardWrapCount; }

private:
    ID3D11Buffer* m_buffer{nullptr};
    std::uint8_t* m_shadowBuffer{nullptr};
    std::size_t   m_capacity{0};
    std::size_t   m_head{0};
    std::uint64_t m_discardWrapCount{0};
    bool          m_mapped{false};
};

} // namespace unc::graphics
