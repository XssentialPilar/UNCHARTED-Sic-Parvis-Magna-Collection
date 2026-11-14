// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace Gow3Prompts {

// Built only when a binding's cached picture is regenerated. The contour comes from
// the supplied asset's alpha, including concavities and mouse shapes, not a PS disc
// or a rectangular approximation. Exact separable squared-Euclidean transform.
class AlphaContour {
public:
    AlphaContour(int width, int height, std::span<const uint8_t> rgba)
        : width{width}, height{height} {
        if (width <= 0 || height <= 0 || rgba.size() != size_t(width) * height * 4) return;
        constexpr float infinity = 1.0e20f;
        const size_t count = size_t(width) * height;
        std::vector<float> to_ink(count), to_clear(count);
        for (size_t i = 0; i < count; ++i) {
            const bool ink = rgba[i * 4 + 3] >= 128;
            to_ink[i] = ink ? 0.0f : infinity;
            to_clear[i] = ink ? infinity : 0.0f;
        }
        const int size = std::max(width, height);
        std::vector<float> input(size), output(size), cuts(size + 1);
        std::vector<int> sites(size);
        const auto line = [&](int length) {
            int last = -1;
            for (int q = 0; q < length; ++q) {
                if (input[q] >= infinity * 0.5f) continue;
                float cross = -infinity;
                while (last >= 0) {
                    const int p = sites[last];
                    cross = ((input[q] + float(q * q)) - (input[p] + float(p * p))) /
                            float(2 * (q - p));
                    if (cross > cuts[last]) break;
                    --last;
                }
                ++last;
                sites[last] = q;
                cuts[last] = last == 0 ? -infinity : cross;
                cuts[last + 1] = infinity;
            }
            if (last < 0) {
                std::fill_n(output.begin(), length, infinity);
                return;
            }
            int k = 0;
            for (int q = 0; q < length; ++q) {
                while (k < last && cuts[k + 1] < float(q)) ++k;
                const int delta = q - sites[k];
                output[q] = float(delta * delta) + input[sites[k]];
            }
        };
        const auto transform = [&](std::vector<float>& field) {
            for (int x = 0; x < width; ++x) {
                for (int y = 0; y < height; ++y) input[y] = field[size_t(y) * width + x];
                line(height);
                for (int y = 0; y < height; ++y) field[size_t(y) * width + x] = output[y];
            }
            for (int y = 0; y < height; ++y) {
                std::copy_n(field.begin() + size_t(y) * width, width, input.begin());
                line(width);
                std::copy_n(output.begin(), width, field.begin() + size_t(y) * width);
            }
        };
        transform(to_ink);
        transform(to_clear);
        distances.resize(count);
        for (size_t i = 0; i < count; ++i) {
            distances[i] = rgba[i * 4 + 3] >= 128 ? 0.5f - std::sqrt(to_clear[i])
                                                 : std::sqrt(to_ink[i]) - 0.5f;
        }
    }

    float At(float x, float y) const {
        if (distances.empty()) return 1.0e10f;
        const int px = std::clamp(int(std::floor(x)), 0, width - 1);
        const int py = std::clamp(int(std::floor(y)), 0, height - 1);
        return distances[size_t(py) * width + px];
    }

private:
    int width, height;
    std::vector<float> distances;
};

} // namespace Gow3Prompts
