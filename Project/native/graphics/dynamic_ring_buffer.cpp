// SPDX-License-Identifier: GPL-2.0-or-later
// native/graphics/dynamic_ring_buffer.cpp
// Uncharted: Sic Parvis Magna Collection — 16 MB Dynamic Ring Buffer (MAP_WRITE_NO_OVERWRITE / DISCARD)

#include "unc/graphics/dynamic_ring_buffer.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace unc::graphics {

bool DynamicRingBuffer::Initialize(ID3D11Device* device, std::size_t capacityBytes,
                                   std::uint32_t bindFlags) noexcept {
    Shutdown();
    if (capacityBytes == 0U) {
        return false;
    }
    m_capacity = capacityBytes;
    m_head = 0U;
    m_discardWrapCount = 0ULL;
    m_mapped = false;

#ifdef _WIN32
    if (device != nullptr) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(capacityBytes);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = bindFlags;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0U;
        desc.StructureByteStride = 0U;
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_buffer))) {
            return false;
        }
        return true;
    }
    m_shadowBuffer = static_cast<std::uint8_t*>(_aligned_malloc(capacityBytes, 64U));
#else
    (void)device;
    (void)bindFlags;
    const std::size_t alignedCap = (capacityBytes + 63U) & ~std::size_t(63U);
    m_shadowBuffer = static_cast<std::uint8_t*>(std::aligned_alloc(64U, alignedCap));
#endif
    return m_shadowBuffer != nullptr;
}

void DynamicRingBuffer::Shutdown() noexcept {
#ifdef _WIN32
    if (m_buffer != nullptr) {
        m_buffer->Release();
        m_buffer = nullptr;
    }
    if (m_shadowBuffer != nullptr) {
        _aligned_free(m_shadowBuffer);
        m_shadowBuffer = nullptr;
    }
#else
    if (m_shadowBuffer != nullptr) {
        std::free(m_shadowBuffer);
        m_shadowBuffer = nullptr;
    }
#endif
    m_capacity = 0U;
    m_head = 0U;
    m_mapped = false;
}

bool DynamicRingBuffer::Allocate(ID3D11DeviceContext* ctx, std::size_t sizeBytes,
                                 std::size_t alignment, RingAllocation& outAlloc) noexcept {
    if (sizeBytes == 0U || sizeBytes > m_capacity) {
        return false;
    }
    const std::size_t safeAlign = (alignment == 0U) ? 16U : alignment;
    const std::size_t mask = safeAlign - 1U;
    std::size_t alignedHead = (m_head + mask) & ~mask;
    RingMapMode mode = RingMapMode::WriteNoOverwrite;

    // Wrap around to 0 with D3D11_MAP_WRITE_DISCARD only when reaching the end of the 16 MB ring
    if (alignedHead + sizeBytes > m_capacity) {
        alignedHead = 0U;
        mode = RingMapMode::WriteDiscard;
        ++m_discardWrapCount;
    }

#ifdef _WIN32
    if (m_buffer != nullptr && ctx != nullptr) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const D3D11_MAP d3dMapType = (mode == RingMapMode::WriteDiscard)
                                         ? D3D11_MAP_WRITE_DISCARD
                                         : D3D11_MAP_WRITE_NO_OVERWRITE;
        if (FAILED(ctx->Map(m_buffer, 0U, d3dMapType, 0U, &mapped))) {
            return false;
        }
        outAlloc.cpuAddress = static_cast<std::uint8_t*>(mapped.pData) + alignedHead;
        outAlloc.byteOffset = alignedHead;
        outAlloc.sizeBytes = sizeBytes;
        outAlloc.mapMode = mode;
        m_head = alignedHead + sizeBytes;
        m_mapped = true;
        return true;
    }
#else
    (void)ctx;
#endif

    if (m_shadowBuffer == nullptr) {
        return false;
    }
    outAlloc.cpuAddress = m_shadowBuffer + alignedHead;
    outAlloc.byteOffset = alignedHead;
    outAlloc.sizeBytes = sizeBytes;
    outAlloc.mapMode = mode;
    m_head = alignedHead + sizeBytes;
    m_mapped = true;
    return true;
}

void DynamicRingBuffer::Unmap(ID3D11DeviceContext* ctx) noexcept {
#ifdef _WIN32
    if (m_buffer != nullptr && ctx != nullptr && m_mapped) {
        ctx->Unmap(m_buffer, 0U);
    }
#else
    (void)ctx;
#endif
    m_mapped = false;
}

} // namespace unc::graphics
