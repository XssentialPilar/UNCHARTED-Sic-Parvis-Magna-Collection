// SPDX-License-Identifier: GPL-2.0-or-later
#include "gow3_png.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Gow3Png {

namespace {

constexpr int kMaxSide = 4096; // save icons are 228x128; anything far larger is not one

bool HasCompletePngChunks(const unsigned char* data, size_t size) {
    static constexpr unsigned char kPngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data || size < 8 + 12 + 13 + 12 || std::memcmp(data, kPngSig, sizeof(kPngSig)) != 0) {
        return false;
    }
    size_t pos = sizeof(kPngSig);
    bool seen_ihdr = false;
    while (pos + 12 <= size) {
        const uint32_t len = (uint32_t(data[pos]) << 24) | (uint32_t(data[pos + 1]) << 16) |
                             (uint32_t(data[pos + 2]) << 8) | uint32_t(data[pos + 3]);
        if (len > 0x7FFFFFFFu || size - pos - 12 < size_t(len)) {
            return false;
        }
        const unsigned char* type = data + pos + 4;
        if (!seen_ihdr) {
            if (std::memcmp(type, "IHDR", 4) != 0 || len != 13) {
                return false;
            }
            seen_ihdr = true;
        }
        pos += 12 + size_t(len);
        if (std::memcmp(type, "IEND", 4) == 0) {
            return len == 0;
        }
    }
    return false;
}

} // namespace

bool DecodeRgba(const unsigned char* data, size_t size, Image& out) {
    out = {};
    if (!data || !size || size > (64u << 20) || !HasCompletePngChunks(data, size)) {
        return false;
    }
    int width = 0;
    int height = 0;
    int components = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, int(size), &width, &height, &components, 4);
    if (!pixels) {
        return false;
    }
    if (width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide) {
        stbi_image_free(pixels);
        return false;
    }
    const size_t bytes = size_t(width) * size_t(height) * 4u;
    out.width = width;
    out.height = height;
    out.rgba.assign(pixels, pixels + bytes);
    stbi_image_free(pixels);
    return true;
}

bool LoadRgba(const char* path, Image& out) {
    out = {};
    FILE* file = path ? std::fopen(path, "rb") : nullptr;
    if (!file) {
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char chunk[65536];
    size_t n = 0;
    while ((n = std::fread(chunk, 1, sizeof(chunk), file)) > 0 && bytes.size() <= (64u << 20)) {
        bytes.insert(bytes.end(), chunk, chunk + n);
    }
    std::fclose(file);
    return DecodeRgba(bytes.data(), bytes.size(), out);
}

} // namespace Gow3Png
