// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/graphics/dx9_backend.hpp
// Uncharted: Sic Parvis Magna Collection — Direct3D 9 / 9Ex Native Backend
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace unc::graphics {

// Direct3D 9 Texture Format Enums for Native Block Compression & Backbuffer
enum class D3D9TextureFormat : std::uint32_t {
    Unknown = 0,
    DXT1 = 0x31545844, // 'DXT1' (FourCC) - 4 bpp, Opaque / 1-bit Alpha
    DXT3 = 0x33545844, // 'DXT3' (FourCC) - 8 bpp, Explicit Alpha
    DXT5 = 0x35545844, // 'DXT5' (FourCC) - 8 bpp, Interpolated Alpha / BC3 equivalent
    A8R8G8B8 = 21,     // 32-bit ARGB pixel format with alpha
    X8R8G8B8 = 22,     // 32-bit XRGB pixel format without alpha
    D24S8 = 75,        // 24-bit depth, 8-bit stencil buffer
};

// Direct3D 9 Architectural Capabilities & Limits (Shader Model 3.0)
struct D3D9ArchitectureLimits {
    static constexpr std::uint32_t kMaxVertexShaderVersion = 0x0300;   // vs_3_0
    static constexpr std::uint32_t kMaxPixelShaderVersion  = 0x0300;   // ps_3_0
    static constexpr std::uint32_t kMaxSimultaneousRenderTargets = 4; // Max 4 MRTs
    static constexpr std::uint32_t kMaxPixelShaderInstructionSlots = 512;
    static constexpr bool kSupportsComputeShaders = false;             // SM 5.0 only (GNM/PS4)
    static constexpr bool kSupportsUnorderedAccessViews = false;       // UAV/SSBO unsupported in D3D9
    static constexpr bool kSupportsStructuredBuffers = false;
};

// Section 3: Texture Format Compatibility Checker for D3D9
[[nodiscard]] constexpr bool IsValidD3D9BlockFormat(D3D9TextureFormat fmt) noexcept {
    return fmt == D3D9TextureFormat::DXT1 ||
           fmt == D3D9TextureFormat::DXT3 ||
           fmt == D3D9TextureFormat::DXT5;
}

// Compute compressed mip 0 size in bytes for D3D9 DXT formats
[[nodiscard]] constexpr std::size_t ComputeD3D9CompressedBytes(
    std::uint32_t width, std::uint32_t height, D3D9TextureFormat fmt) noexcept {
    const std::uint32_t block_w = (width + 3U) / 4U;
    const std::uint32_t block_h = (height + 3U) / 4U;
    const std::size_t block_bytes = (fmt == D3D9TextureFormat::DXT1) ? 8ULL : 16ULL;
    return static_cast<std::size_t>(block_w) * static_cast<std::size_t>(block_h) * block_bytes;
}

} // namespace unc::graphics
