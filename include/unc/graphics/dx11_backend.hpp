// SPDX-License-Identifier: GPL-2.0-or-later
// include/unc/graphics/dx11_backend.hpp
// Uncharted: Sic Parvis Magna Collection — DirectX 11.1 (Feature Level 11_0) Backend, BC1-BC7 Enforcement & BC5 Normal Math
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace unc::graphics {

enum class SceneTextureSemantic : std::uint8_t {
    AlbedoOpaque = 0,      // Mandatory: DXGI_FORMAT_BC1_UNORM_SRGB (4 bpp, 2 MB @ 2048x2048)
    RoughnessOrAoMask = 1, // Mandatory: DXGI_FORMAT_BC4_UNORM (4 bpp, 2 MB @ 2048x2048)
    NormalMap = 2,         // Mandatory: DXGI_FORMAT_BC5_UNORM (8 bpp, 4 MB @ 2048x2048, reconstruct Z)
    AlbedoAlphaOrUiHd = 3  // Mandatory: DXGI_FORMAT_BC7_UNORM_SRGB (or BC3 fallback, 8 bpp, 4 MB @ 2048x2048)
};

// Subset of DXGI_FORMAT values from dxgiformat.h for platform-independent static verification
enum class CompressedDxgiFormat : std::uint32_t {
    Unknown = 0U,
    R8G8B8A8_UNORM = 28U,       // FORBIDDEN for 3D scene textures
    B8G8R8A8_UNORM = 87U,       // FORBIDDEN for 3D scene textures
    BC1_UNORM = 71U,
    BC1_UNORM_SRGB = 72U,       // 4 bpp
    BC3_UNORM = 77U,
    BC3_UNORM_SRGB = 78U,       // 8 bpp fallback
    BC4_UNORM = 80U,            // 4 bpp
    BC5_UNORM = 83U,            // 8 bpp (X, Y channels)
    BC7_UNORM = 98U,
    BC7_UNORM_SRGB = 99U        // 8 bpp Feature Level 11_0
};

struct Float3 {
    float x{0.0f};
    float y{0.0f};
    float z{1.0f};
};

// Section 3.3: Strict Block Compression Validator (Forbids uncompressed RGBA8/BGRA8 scene textures)
[[nodiscard]] constexpr bool IsAllowed3DSceneTextureFormat(CompressedDxgiFormat fmt) noexcept {
    switch (fmt) {
        case CompressedDxgiFormat::BC1_UNORM:
        case CompressedDxgiFormat::BC1_UNORM_SRGB:
        case CompressedDxgiFormat::BC3_UNORM:
        case CompressedDxgiFormat::BC3_UNORM_SRGB:
        case CompressedDxgiFormat::BC4_UNORM:
        case CompressedDxgiFormat::BC5_UNORM:
        case CompressedDxgiFormat::BC7_UNORM:
        case CompressedDxgiFormat::BC7_UNORM_SRGB:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr CompressedDxgiFormat SelectMandatoryBlockFormat(
    SceneTextureSemantic semantic, bool supportsBc7 = true) noexcept {
    switch (semantic) {
        case SceneTextureSemantic::AlbedoOpaque:
            return CompressedDxgiFormat::BC1_UNORM_SRGB;
        case SceneTextureSemantic::RoughnessOrAoMask:
            return CompressedDxgiFormat::BC4_UNORM;
        case SceneTextureSemantic::NormalMap:
            return CompressedDxgiFormat::BC5_UNORM;
        case SceneTextureSemantic::AlbedoAlphaOrUiHd:
            return supportsBc7 ? CompressedDxgiFormat::BC7_UNORM_SRGB
                               : CompressedDxgiFormat::BC3_UNORM_SRGB;
    }
    return CompressedDxgiFormat::BC1_UNORM_SRGB;
}

[[nodiscard]] constexpr std::uint64_t ComputeCompressedMip0Bytes(
    std::uint32_t width, std::uint32_t height, CompressedDxgiFormat fmt) noexcept {
    const std::uint64_t blocksX = (static_cast<std::uint64_t>(width) + 3ULL) / 4ULL;
    const std::uint64_t blocksY = (static_cast<std::uint64_t>(height) + 3ULL) / 4ULL;
    const std::uint64_t bytesPerBlock =
        (fmt == CompressedDxgiFormat::BC1_UNORM || fmt == CompressedDxgiFormat::BC1_UNORM_SRGB ||
         fmt == CompressedDxgiFormat::BC4_UNORM)
            ? 8ULL
            : 16ULL;
    return blocksX * blocksY * bytesPerBlock;
}

// Section 3.3 Mandatory BC5 Normal Z Reconstruction Formula:
// N_xy = Sample_xy * 2.0 - 1.0
// N_z  = sqrt(max(0.0, 1.0 - (N_x^2 + N_y^2)))
[[nodiscard]] inline Float3 ReconstructBc5Normal(float sampleX, float sampleY) noexcept {
    const float nx = sampleX * 2.0f - 1.0f;
    const float ny = sampleY * 2.0f - 1.0f;
    const float rad = std::max(0.0f, 1.0f - (nx * nx + ny * ny));
    const float nz = std::sqrt(rad);
    return Float3{nx, ny, nz};
}

struct PrewarmedShaderCacheHeader {
    std::uint32_t magic{0x4D505355U}; // "USPM"
    std::uint32_t version{1U};
    std::uint32_t vertexShaderCount{0U};
    std::uint32_t pixelShaderCount{0U};
};

} // namespace unc::graphics
