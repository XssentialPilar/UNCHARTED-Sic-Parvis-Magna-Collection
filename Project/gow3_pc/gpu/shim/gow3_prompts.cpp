// Keyboard, mouse and controller button prompts. The three games draw the button icons in their
// text ("Press |X| to jump", the menus' legends) from one symbol font: data/fonts/ps4/symbol3.fnt
// with a 512x512 atlas (Uncharted 3 keeps its colour and alpha in two textures). While the player
// uses the keyboard and mouse, or a controller that is not a PlayStation one, the texture cache
// uploads a redrawn atlas instead: key caps and mice for the bound keys and mouse buttons, or
// Xbox-style buttons (A B X Y, LB RB, LT RT...) for the controller's, drawn here in the same cells
// from the labels runtime_pad.c passes; the guest data stays as it is. The prompts the games draw
// in the world ("Triangle -> chest") are separate BC1 pictures of the buttons (hud.pak; Uncharted
// 1: particle.pak), stored upside down: the round face buttons and Uncharted 3's shoulder buttons
// are recognised by the hash of their first mip and redrawn the same way, the bound key in place
// of the PlayStation symbol (re-encoded to BC1 here). A device switch invalidates them, and they
// upload again at their next use. runtime_pad.c picks the device (GOW3_PROMPTS);
// GOW3_PROMPTS_LOG=1 logs the hash of each candidate upload (to add textures).

#include "gow3_prompts.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <xxhash.h>

#include "../gow3gpu.h"
#ifndef GOW3_PROMPTS_TEST
#include "core/memory.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

extern "C" const unsigned char gow3_font_ttf[]; // gow3_overlay.cpp: DejaVu Sans

namespace Gow3Prompts {
namespace {

constexpr int Size = 512;

// The textures by the XXH3 hash of their first mip's guest data (1D-tiled): the symbol font atlases
// (512x512 RGBA8) and the button pictures (BC1 with mips, found by decoding the paks: 256x256 face
// and shoulder buttons, 512x512 flat "HOLD" buttons in two press positions). The three games share
// most pictures; Uncharted 1 and 2 have their own Cross, Uncharted 3 its own HOLD Triangles.
enum class Kind { Rgba, Color, Alpha, Face, Shoulder, Dpad, Journal };
struct KnownTexture {
    u64 hash;
    Kind kind;
    int button; // pictures: GOW3_PROMPT_*
    const char* name;
};
constexpr KnownTexture known_textures[] = {
    {0xad5ada34fd34c76bULL, Kind::Rgba, -1, "Uncharted 1 button atlas"},
    {0x97779d0906f16551ULL, Kind::Rgba, -1, "Uncharted 2 button atlas"},
    {0x8d03f2ff457f032dULL, Kind::Color, -1, "Uncharted 3 button atlas (colour)"},
    {0xae0c06306763d424ULL, Kind::Alpha, -1, "Uncharted 3 button atlas (alpha)"},
    {0x5db884aa1bae54f9ULL, Kind::Face, GOW3_PROMPT_TRIANGLE, "Triangle picture"},
    {0x7af1abf688d8c824ULL, Kind::Face, GOW3_PROMPT_CIRCLE, "Circle picture"},
    {0x9edd4f2f0f64f6e5ULL, Kind::Face, GOW3_PROMPT_SQUARE, "Square picture"},
    {0x2bf5d90eb864162fULL, Kind::Face, GOW3_PROMPT_CROSS, "Uncharted 1/2 Cross picture"},
    {0x305c2c731ec227e6ULL, Kind::Face, GOW3_PROMPT_CROSS, "Uncharted 3 Cross picture"},
    {0x25d954ce23a8f5e2ULL, Kind::Face, GOW3_PROMPT_TRIANGLE, "Uncharted 1/2 HOLD Triangle picture"},
    {0xc96e34fbb203279eULL, Kind::Face, GOW3_PROMPT_TRIANGLE, "Uncharted 1/2 HOLD Triangle picture (pressed)"},
    {0x4181f3ae6bd1efffULL, Kind::Face, GOW3_PROMPT_TRIANGLE, "Uncharted 3 HOLD Triangle picture"},
    {0x07ff3d0e4090be85ULL, Kind::Face, GOW3_PROMPT_TRIANGLE, "Uncharted 3 HOLD Triangle picture (pressed)"},
    // 1 Nov 2026 (#327, the Uncharted 3 bar fight's chokehold): the Circle to tap or hold, in the
    // same two press positions as the HOLD Triangles.
    {0x94d741f6b51815eeULL, Kind::Face, GOW3_PROMPT_CIRCLE, "Uncharted 3 HOLD Circle picture"},
    {0x93b46ba81469882cULL, Kind::Face, GOW3_PROMPT_CIRCLE, "Uncharted 3 HOLD Circle picture (pressed)"},
    {0x668cf3cc04ab6963ULL, Kind::Face, GOW3_PROMPT_SQUARE, "Uncharted 3 HOLD Square picture"},
    {0x1b7e089f40b3bb21ULL, Kind::Face, GOW3_PROMPT_SQUARE, "Uncharted 3 HOLD Square picture (pressed)"},
    {0x6c3fc301ede4114aULL, Kind::Shoulder, GOW3_PROMPT_L1, "Uncharted 3 L1 picture"},
    {0x2e8ae3386e2a91ddULL, Kind::Shoulder, GOW3_PROMPT_L2, "Uncharted 3 L2 picture"},
    {0xbd74a617a879f120ULL, Kind::Shoulder, GOW3_PROMPT_R1, "Uncharted 3 R1 picture"},
    {0x784278339a5a55bfULL, Kind::Shoulder, GOW3_PROMPT_R2, "Uncharted 3 R2 picture"},
    {0xb88838d137123548ULL, Kind::Shoulder, GOW3_PROMPT_L1, "Uncharted 1/2 L1 picture"},
    {0x4999c8b20a45cd6dULL, Kind::Shoulder, GOW3_PROMPT_R1, "Uncharted 1/2 R1 picture"},
    {0x539b560401a1ac83ULL, Kind::Shoulder, GOW3_PROMPT_L2, "Uncharted 1/2 L2 picture"},
    {0xe2c351d65c6a63f1ULL, Kind::Shoulder, GOW3_PROMPT_R2, "Uncharted 1/2 R2 picture"},
    // D-pad pictures:
    {0xe8a2f49b992621fcULL, Kind::Dpad, GOW3_PROMPT_UP, "Uncharted 3 d-pad up picture"},
    {0x705083db743dd2f9ULL, Kind::Dpad, GOW3_PROMPT_UP, "Uncharted 2 d-pad up picture"},
    {0xde25b56cbba0318fULL, Kind::Dpad, GOW3_PROMPT_UP, "Uncharted 1 d-pad up picture"},
    // Drake's Journal & Map textures (512x256 BC1):
    {0xf9e323d0de77089aULL, Kind::Journal, GOW3_PROMPT_TOUCHPAD, "Uncharted 2/3 Drake's Journal (select)"},
    {0xfd143a2450dbb4ccULL, Kind::Journal, GOW3_PROMPT_TOUCHPAD, "Uncharted 1 Drake's Journal (select)"},
    {0xbf9e5eb56abdcc3eULL, Kind::Journal, GOW3_PROMPT_TOUCHPAD, "Uncharted 2/3 Drake's Journal (add page)"},
    {0x3204d997bd026ab3ULL, Kind::Journal, GOW3_PROMPT_TOUCHPAD, "Uncharted Drake's Map (select)"},
};

// symbol3.fnt, the same in the three games: glyph id and its cell in the atlas.
struct Cell {
    int id, x, y, w, h;
};
constexpr Cell cells[] = {
    {1, 0, 0, 75, 75},      {2, 0, 75, 64, 64},     {3, 64, 75, 64, 64},    {4, 128, 75, 64, 64},
    {5, 192, 75, 64, 64},   {6, 84, 252, 77, 46},   {7, 0, 139, 64, 60},    {8, 161, 243, 77, 46},
    {9, 64, 139, 64, 60},   {10, 128, 139, 74, 53}, {11, 148, 192, 64, 51}, {12, 75, 0, 75, 75},
    {13, 150, 0, 75, 75},   {14, 0, 252, 84, 50},   {15, 0, 199, 74, 53},   {16, 74, 199, 74, 53},
    {18, 238, 216, 86, 86}, {21, 0, 355, 75, 75},   {24, 0, 302, 74, 53},   {25, 74, 302, 74, 53},
    {29, 148, 298, 64, 51}, {30, 0, 252, 77, 51},   {40, 340, 113, 77, 46}, {41, 256, 0, 64, 60},
    {42, 417, 104, 77, 46}, {43, 320, 0, 64, 60},   {44, 404, 53, 64, 51},  {45, 256, 60, 74, 53},
    {46, 330, 60, 74, 53},  {47, 256, 163, 74, 53}, {48, 330, 163, 74, 53}, {49, 404, 159, 64, 51},
};

// What each glyph shows. 1-30 and 40-49 are two drawings of the same buttons; 10 is the plain
// stick (moving). Share (29, 49) keeps its PlayStation icon: the PC has no such button. The
// touchpad has two cells (14, 30) on the same pixels: drawn once in their common part.
enum class Draw { Key, Pair, Dpad, Stick };
struct Plan {
    int id;
    Draw draw;
    int a, b; // GOW3_PROMPT_*: the key (Key), the pair (Pair), the stick's first direction (Stick)
};
constexpr Plan plans[] = {
    {5, Draw::Key, GOW3_PROMPT_CROSS},       {2, Draw::Key, GOW3_PROMPT_CIRCLE},
    {3, Draw::Key, GOW3_PROMPT_SQUARE},      {4, Draw::Key, GOW3_PROMPT_TRIANGLE},
    {8, Draw::Key, GOW3_PROMPT_L1},          {42, Draw::Key, GOW3_PROMPT_L1},
    {6, Draw::Key, GOW3_PROMPT_R1},          {40, Draw::Key, GOW3_PROMPT_R1},
    {9, Draw::Key, GOW3_PROMPT_L2},          {43, Draw::Key, GOW3_PROMPT_L2},
    {7, Draw::Key, GOW3_PROMPT_R2},          {41, Draw::Key, GOW3_PROMPT_R2},
    {24, Draw::Key, GOW3_PROMPT_L3},         {47, Draw::Key, GOW3_PROMPT_L3},
    {25, Draw::Key, GOW3_PROMPT_R3},         {48, Draw::Key, GOW3_PROMPT_R3},
    {11, Draw::Key, GOW3_PROMPT_OPTIONS},    {44, Draw::Key, GOW3_PROMPT_OPTIONS},
    {29, Draw::Key, GOW3_PROMPT_OPTIONS},    {49, Draw::Key, GOW3_PROMPT_OPTIONS},
    {14, Draw::Key, GOW3_PROMPT_TOUCHPAD},   {30, Draw::Key, GOW3_PROMPT_TOUCHPAD},
    {18, Draw::Key, GOW3_PROMPT_UP},
    {21, Draw::Key, GOW3_PROMPT_DOWN},       {12, Draw::Pair, GOW3_PROMPT_LEFT, GOW3_PROMPT_RIGHT},
    {13, Draw::Pair, GOW3_PROMPT_UP, GOW3_PROMPT_DOWN}, {1, Draw::Dpad},
    {10, Draw::Stick, GOW3_PROMPT_LS_UP},    {15, Draw::Stick, GOW3_PROMPT_LS_UP},
    {45, Draw::Stick, GOW3_PROMPT_LS_UP},    {16, Draw::Stick, GOW3_PROMPT_RS_UP},
    {46, Draw::Stick, GOW3_PROMPT_RS_UP},
};

struct Rgb {
    float r, g, b;
};
Rgb Mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
// The PlayStation icons' palette: warm light rims and glow, dark bodies, orange highlights.
constexpr Rgb glow{0.95f, 0.92f, 0.85f};
constexpr Rgb rim{0.84f, 0.81f, 0.75f};
constexpr Rgb body_top{0.34f, 0.33f, 0.31f};
constexpr Rgb body_bottom{0.12f, 0.12f, 0.11f};
constexpr Rgb ink{0.98f, 0.97f, 0.93f};
constexpr Rgb accent_top{1.0f, 0.68f, 0.30f};
constexpr Rgb accent_bottom{0.85f, 0.44f, 0.12f};
constexpr Rgb groove{0.06f, 0.06f, 0.06f};
constexpr float Margin = 4.0f; // the font's 2-pixel padding and room for the glow

// Signed distances in pixels (negative inside).
float RoundBox(float px, float py, float cx, float cy, float hw, float hh, float r) {
    r = std::min(r, std::min(hw, hh));
    const float qx = std::fabs(px - cx) - hw + r, qy = std::fabs(py - cy) - hh + r;
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}
float Segment(float px, float py, float ax, float ay, float bx, float by) {
    const float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    const float h = std::clamp((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.0f, 1.0f);
    return std::hypot(pax - bax * h, pay - bay * h);
}
// Exact triangle distance (corners v: x0 y0 x1 y1 x2 y2), after Inigo Quilez.
float Triangle(float px, float py, const float (&v)[6]) {
    const float e0x = v[2] - v[0], e0y = v[3] - v[1], e1x = v[4] - v[2], e1y = v[5] - v[3];
    const float e2x = v[0] - v[4], e2y = v[1] - v[5];
    const float w0x = px - v[0], w0y = py - v[1], w1x = px - v[2], w1y = py - v[3];
    const float w2x = px - v[4], w2y = py - v[5];
    const auto edge = [](float wx, float wy, float ex, float ey) {
        const float h = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
        const float dx = wx - ex * h, dy = wy - ey * h;
        return dx * dx + dy * dy;
    };
    const float s = e0x * e2y - e0y * e2x > 0 ? 1.0f : -1.0f;
    const float d = std::min({edge(w0x, w0y, e0x, e0y), edge(w1x, w1y, e1x, e1y), edge(w2x, w2y, e2x, e2y)});
    const float side = std::min({s * (w0x * e0y - w0y * e0x), s * (w1x * e1y - w1y * e1x),
                                 s * (w2x * e2y - w2y * e2x)});
    return side > 0 ? -std::sqrt(d) : std::sqrt(d);
}

// Premultiplied colour and coverage per texel, and which texels belong to redrawn cells.
struct Canvas {
    int w = Size, h = Size;
    std::vector<float> texels = std::vector<float>(size_t(w) * h * 4, 0.0f);
    std::vector<u8> drawn = std::vector<u8>(size_t(w) * h, 0);
};

// ---- Button pictures (BC1)
struct Picture {
    int w = 0, h = 0;
    std::vector<u8> rgba; // alpha 0: BC1 punch-through texels
};

class Painter {
public:
    // clear: the area starts transparent (atlas cells); otherwise drawing goes over the canvas.
    Painter(Canvas& canvas_, int x0_, int y0_, int x1_, int y1_, bool clear = true)
        : canvas{canvas_}, x0{x0_}, y0{y0_}, x1{x1_}, y1{y1_} {
        for (int y = y0; clear && y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                std::fill_n(&canvas.texels[(y * canvas.w + x) * 4], 4, 0.0f);
                canvas.drawn[y * canvas.w + x] = 1;
            }
        }
    }
    // Colour per point (gradients).
    template <typename Sdf, typename Color>
    void FillShaded(Sdf&& sdf, Color&& color, float opacity = 1.0f) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float cover = std::clamp(0.5f - sdf(px, py), 0.0f, 1.0f) * opacity;
                if (cover > 0.0f) {
                    Over(x, y, color(px, py), cover);
                }
            }
        }
    }
    void Fill(auto&& sdf, Rgb color, float opacity = 1.0f) {
        FillShaded(sdf, [color](float, float) { return color; }, opacity);
    }
    // A soft glow around a shape (as the PlayStation icons have), fading over `radius` pixels.
    void Halo(auto&& sdf, float radius, float opacity, Rgb color = glow) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const float d = sdf(x + 0.5f, y + 0.5f);
                const float t = d <= 0.0f ? 1.0f : std::max(0.0f, 1.0f - d / radius);
                if (t > 0.0f) {
                    Over(x, y, color, t * t * opacity);
                }
            }
        }
    }

    Canvas& GetCanvas() { return canvas; }

private:
    void Over(int x, int y, Rgb c, float a) {
        float* p = &canvas.texels[(y * canvas.w + x) * 4];
        p[0] = c.r * a + p[0] * (1.0f - a);
        p[1] = c.g * a + p[1] * (1.0f - a);
        p[2] = c.b * a + p[2] * (1.0f - a);
        p[3] = a + p[3] * (1.0f - a);
    }
    Canvas& canvas;
    int x0, y0, x1, y1;
};

const stbtt_fontinfo* Font() {
    static stbtt_fontinfo info;
    static const bool ok =
        stbtt_InitFont(&info, gow3_font_ttf, stbtt_GetFontOffsetForIndex(gow3_font_ttf, 0)) != 0;
    return ok ? &info : nullptr;
}

std::vector<int> Codepoints(const std::string& text) {
    std::vector<int> out;
    for (size_t i = 0; i < text.size();) {
        const u8 c = u8(text[i]);
        const int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        int cp = extra ? c & (0x3f >> extra) : c;
        for (int k = 1; k <= extra && i + k < text.size(); ++k) {
            cp = (cp << 6) | (u8(text[i + k]) & 0x3f);
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

// A label as signed distance fields (stb_truetype), centred by its ink on (cx, cy) with
// capitals `cap` pixels tall, condensed horizontally by `condense` (long key names stay
// legible in a square key) and made smaller to fit `max_width`.
class Text {
public:
    static constexpr int Pad = 8; // pixels of distance kept around each glyph (glows)
    static constexpr float DistScale = 128.0f / Pad;

    Text(const std::string& text, float cx, float cy, float cap, float max_width, float condense_ = 1.0f)
        : condense{condense_}, centre{cx} {
        const stbtt_fontinfo* font = Font();
        if (!font || text.empty()) {
            return;
        }
        const auto cps = Codepoints(text);
        float scale = Scale(cap);
        float x_min, x_max, y_min, y_max;
        Extent(cps, scale, x_min, x_max, y_min, y_max);
        if ((x_max - x_min) * condense > max_width && x_max > x_min) {
            scale *= max_width / ((x_max - x_min) * condense);
            Extent(cps, scale, x_min, x_max, y_min, y_max);
        }
        const float dx = cx - (x_min + x_max) / 2, dy = cy - (y_min + y_max) / 2;
        float pen = 0.0f;
        for (size_t i = 0; i < cps.size(); ++i) {
            int w = 0, h = 0, xoff = 0, yoff = 0;
            u8* sdf = stbtt_GetCodepointSDF(font, scale, cps[i], Pad, 128, DistScale, &w, &h, &xoff, &yoff);
            if (sdf) {
                glyphs.push_back({std::vector<u8>(sdf, sdf + w * h), w, h, dx + std::floor(pen) + xoff,
                                  dy + yoff});
                stbtt_FreeSDF(sdf, nullptr);
            }
            pen += Advance(cps, i, scale);
        }
    }

    // Capitals `cap` pixels tall: the label's ink width.
    static float Width(const std::string& text, float cap, float condense = 1.0f) {
        if (!Font() || text.empty()) {
            return 0.0f;
        }
        float x_min, x_max, y_min, y_max;
        Extent(Codepoints(text), Scale(cap), x_min, x_max, y_min, y_max);
        return (x_max - x_min) * condense;
    }

    float Distance(float px, float py) const {
        float best = 1e6f; // outside every glyph's field: far away
        const float qx = centre + (px - centre) / condense;
        for (const auto& g : glyphs) {
            const float u = qx - g.x - 0.5f, v = py - g.y - 0.5f;
            if (u < 0.0f || v < 0.0f || u > g.w - 1.0f || v > g.h - 1.0f) {
                continue;
            }
            const int iu = std::min(int(u), g.w - 2 < 0 ? 0 : g.w - 2);
            const int iv = std::min(int(v), g.h - 2 < 0 ? 0 : g.h - 2);
            const float fu = u - iu, fv = v - iv;
            auto at = [&](int x, int y) {
                return float(g.sdf[std::min(y, g.h - 1) * g.w + std::min(x, g.w - 1)]);
            };
            const float value = (at(iu, iv) * (1 - fu) + at(iu + 1, iv) * fu) * (1 - fv) +
                                (at(iu, iv + 1) * (1 - fu) + at(iu + 1, iv + 1) * fu) * fv;
            best = std::min(best, (128.0f - value) / DistScale);
        }
        if (best >= 1e6f) {
            return best;
        }
        // Condensed: horizontal distances shrink, vertical ones do not; in between.
        return best * (1.0f + condense) / 2;
    }

private:
    struct Glyph {
        std::vector<u8> sdf;
        int w, h;
        float x, y;
    };
    std::vector<Glyph> glyphs;
    float condense, centre;

    static float Scale(float cap) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBox(Font(), 'H', &x0, &y0, &x1, &y1);
        return cap / float(std::max(y1, 1));
    }
    // Pen advance after glyph i (kerning; tightened a little: key labels are short and bold).
    static float Advance(const std::vector<int>& cps, size_t i, float scale) {
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(Font(), cps[i], &advance, &bearing);
        float x = advance * scale;
        if (i + 1 < cps.size()) {
            x += stbtt_GetCodepointKernAdvance(Font(), cps[i], cps[i + 1]) * scale;
            x -= 0.04f * advance * scale;
        }
        return x;
    }
    // The ink box relative to the first pen position on the baseline (y down).
    static void Extent(const std::vector<int>& cps, float scale, float& x_min, float& x_max,
                       float& y_min, float& y_max) {
        x_min = y_min = 1e9f;
        x_max = y_max = -1e9f;
        float pen = 0.0f;
        for (size_t i = 0; i < cps.size(); ++i) {
            int x0, y0, x1, y1;
            if (stbtt_GetCodepointBox(Font(), cps[i], &x0, &y0, &x1, &y1)) {
                x_min = std::min(x_min, std::floor(pen) + x0 * scale);
                x_max = std::max(x_max, std::floor(pen) + x1 * scale);
                y_min = std::min(y_min, -y1 * scale);
                y_max = std::max(y_max, -y0 * scale);
            }
            pen += Advance(cps, i, scale);
        }
        if (x_min > x_max) {
            x_min = x_max = y_min = y_max = 0.0f;
        }
    }
};

float LabelCondense(size_t chars) {
    return chars <= 1 ? 1.0f : chars == 2 ? 0.9f : 0.78f;
}

// Keys and the mouse are drawn in two passes (glows first) so a cluster's glows stay behind its
// keys.
enum class Pass { Glow, Body };

void DrawKey(Painter& p, Pass pass, float cx, float cy, float w, float h, const std::string& label) {
    const float side = std::min(w, h);
    const float r = 0.22f * side, edge = std::max(1.4f, 0.06f * side);
    const auto outer = [=](float x, float y) { return RoundBox(x, y, cx, cy, w / 2, h / 2, r); };
    if (pass == Pass::Glow) {
        p.Halo(outer, std::max(2.0f, 0.06f * side), 0.6f);
        return;
    }
    const auto inner = [=](float x, float y) {
        return RoundBox(x, y, cx, cy, w / 2 - edge, h / 2 - edge, r - edge);
    };
    p.Fill(outer, rim);
    const float top = cy - h / 2;
    p.FillShaded(inner, [=](float, float y) { return Mix(body_top, body_bottom, (y - top) / h); });
    const size_t chars = Codepoints(label).size();
    const float cap = chars == 1 ? 0.5f * h : 0.42f * h;
    const Text text{label, cx, cy, cap, w - 2 * edge - 0.12f * h, LabelCondense(chars)};
    const float bold = (chars == 1 ? 0.06f : 0.05f) * cap;
    p.Fill([&](float x, float y) { return text.Distance(x, y) - bold; }, ink);
}

// An arrow head pointing up/down/left/right (dx, dy), light with a dark outline.
void DrawArrow(Painter& p, float cx, float cy, float size, int dx, int dy) {
    const float s = size / 2;
    float v[6];
    if (dy) {
        v[0] = cx, v[1] = cy + dy * s, v[2] = cx - s, v[3] = cy - dy * s * 0.6f, v[4] = cx + s,
        v[5] = cy - dy * s * 0.6f;
    } else {
        v[0] = cx + dx * s, v[1] = cy, v[2] = cx - dx * s * 0.6f, v[3] = cy - s,
        v[4] = cx - dx * s * 0.6f, v[5] = cy + s;
    }
    const float line = std::max(1.0f, 0.12f * size);
    p.Fill([&](float x, float y) { return Triangle(x, y, v) - line; }, groove);
    p.Fill([&](float x, float y) { return Triangle(x, y, v); }, ink);
}

enum class MousePart { None, Left, Right, Middle, WheelUp, WheelDown, Back, Forward, Move };

MousePart MousePartOf(const std::string& label) {
    static const std::pair<const char*, MousePart> names[] = {
        {"@LMB", MousePart::Left},         {"@RMB", MousePart::Right},
        {"@MMB", MousePart::Middle},       {"@WHEELUP", MousePart::WheelUp},
        {"@WHEELDOWN", MousePart::WheelDown}, {"@X1", MousePart::Back},
        {"@X2", MousePart::Forward},       {"@MOUSE", MousePart::Move},
    };
    for (const auto& [name, part] : names) {
        if (label == name) {
            return part;
        }
    }
    return MousePart::None;
}

// A mouse (height h, centred) with the used button in orange; arrows beside it for the wheel
// and for movement.
void DrawMouse(Painter& p, Pass pass, float cx, float cy, float h, float room, MousePart part) {
    const float w = 0.62f * h, edge = std::max(1.4f, 0.06f * w);
    const auto outer = [=](float x, float y) { return RoundBox(x, y, cx, cy, w / 2, h / 2, w / 2); };
    if (pass == Pass::Glow) {
        p.Halo(outer, std::max(2.0f, 0.06f * h), 0.6f);
        return;
    }
    const auto inner = [=](float x, float y) {
        return RoundBox(x, y, cx, cy, w / 2 - edge, h / 2 - edge, w / 2 - edge);
    };
    p.Fill(outer, rim);
    const float top = cy - h / 2;
    p.FillShaded(inner, [=](float, float y) { return Mix(body_top, body_bottom, (y - top) / h); });
    const float split = cy - 0.08f * h, line = std::max(1.0f, 0.05f * w);
    const auto accent = [=](float, float y) {
        return Mix(accent_top, accent_bottom, (y - top) / (split - top));
    };
    if (part == MousePart::Left || part == MousePart::Right) {
        const float sign = part == MousePart::Left ? 1.0f : -1.0f;
        p.FillShaded([=](float x, float y) {
            return std::max(inner(x, y), std::max(y - split, sign * (x - cx)));
        }, accent);
    }
    p.Fill([=](float x, float y) {
        const float d = std::min(Segment(x, y, cx, top, cx, split),
                                 Segment(x, y, cx - w, split, cx + w, split));
        return std::max(d - line / 2, inner(x, y));
    }, groove);
    const float wy = cy - 0.27f * h, whw = std::max(1.2f, 0.09f * w), whh = 0.1f * h;
    const bool wheel = part == MousePart::Middle || part == MousePart::WheelUp ||
                       part == MousePart::WheelDown;
    p.Fill([=](float x, float y) { return RoundBox(x, y, cx, wy, whw + line, whh + line, whw + line); },
           groove);
    p.Fill([=](float x, float y) { return RoundBox(x, y, cx, wy, whw, whh, whw); },
           wheel ? accent_top : rim);
    if (part == MousePart::Back || part == MousePart::Forward) {
        const float sy = cy + (part == MousePart::Back ? 0.16f : 0.02f) * h;
        const float sx = cx - w / 2 + edge + 0.07f * w;
        p.Fill([=](float x, float y) { return RoundBox(x, y, sx, sy, 0.07f * w, 0.065f * h, 0.05f * w); },
               accent_top);
    }
    const float arrow = std::min(0.26f * h, room * 0.8f);
    const float beside = cx + w / 2 + room / 2;
    if (part == MousePart::WheelUp || part == MousePart::WheelDown) {
        DrawArrow(p, beside, wy, arrow, 0, part == MousePart::WheelUp ? -1 : 1);
    }
    if (part == MousePart::Move) {
        DrawArrow(p, cx - w / 2 - room / 2, cy, arrow, -1, 0);
        DrawArrow(p, beside, cy, arrow, 1, 0);
    }
}

// Controller buttons in the Xbox style ("#A" "#B" "#X" "#Y" "#LB" "#RB" "#LT" "#RT" "#LS" "#RS"
// "#MENU" "#VIEW"): dark buttons with light rims; the face buttons' letters and glows in their
// colours.
Rgb FaceColor(char letter) {
    switch (letter) {
    case 'A':
        return {0.45f, 0.82f, 0.30f};
    case 'B':
        return {0.95f, 0.32f, 0.26f};
    case 'X':
        return {0.30f, 0.58f, 0.98f};
    default:
        return {0.99f, 0.80f, 0.22f};
    }
}

// A shape with a light rim, a dark body (lighter at the top) and a label; `outer` is the shape's
// distance, the body is `edge` pixels inside it.
template <typename Sdf>
void DrawShape(Painter& p, Pass pass, Sdf&& outer, float edge, float top, float height, Rgb halo,
               const std::string& label, float cx, float cy, float cap, float max_width, Rgb ink_color,
               float condense = 1.0f) {
    if (pass == Pass::Glow) {
        p.Halo(outer, std::max(2.0f, 0.06f * height), 0.6f, halo);
        return;
    }
    p.Fill(outer, rim);
    p.FillShaded([&](float x, float y) { return outer(x, y) + edge; },
                 [=](float, float y) { return Mix(body_top, body_bottom, (y - top) / height); });
    if (label.empty()) {
        return;
    }
    const Text text{label, cx, cy, cap, max_width, condense};
    const float bold = 0.06f * cap;
    p.Fill([&](float x, float y) { return text.Distance(x, y) - bold; }, ink_color);
}

void DrawPadButton(Painter& p, Pass pass, float cx, float cy, float w, float h, const std::string& token) {
    const std::string name = token.substr(1);
    const float side = std::min(w, h);
    const float edge = std::max(1.4f, 0.06f * side);
    if (name.size() == 1) { // A B X Y
        const float r = 0.47f * side;
        const Rgb color = FaceColor(name[0]);
        DrawShape(p, pass, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, Mix(glow, color, 0.6f), name, cx, cy, 0.95f * r, 1.3f * r, color);
    } else if (name == "LB" || name == "RB") { // bumper: wide, short, rounder on top
        const float hw = std::min(0.5f * w, 0.85f * h), hh = 0.3f * h;
        DrawShape(p, pass, [=](float x, float y) {
            return RoundBox(x, y, cx, cy, hw, hh, (y < cy ? 0.9f : 0.45f) * hh);
        }, edge, cy - hh, 2 * hh, glow, name, cx, cy, 0.95f * hh, 1.5f * hw, ink, 0.9f);
    } else if (name == "LT" || name == "RT") { // trigger: tall, round top
        const float hw = 0.36f * h, hh = 0.47f * h;
        DrawShape(p, pass, [=](float x, float y) {
            return RoundBox(x, y, cx, cy, hw, hh, (y < cy ? 0.95f : 0.35f) * hw);
        }, edge, cy - hh, 2 * hh, glow, name, cx, cy + 0.08f * hh, 0.62f * hw, 1.6f * hw, ink, 0.9f);
    } else if (name == "LS" || name == "RS") { // stick click
        const float r = 0.47f * side;
        DrawShape(p, pass, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, glow, name, cx, cy, 0.62f * r, 1.4f * r, ink, 0.9f);
    } else if (name == "MENU" || name == "VIEW") { // small round buttons with their symbols
        const float r = 0.36f * side;
        DrawShape(p, pass, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, glow, "", cx, cy, 0, 0, ink);
        if (pass == Pass::Glow) {
            return;
        }
        const float line = std::max(1.0f, 0.1f * r);
        if (name == "MENU") { // three lines
            for (int i = -1; i <= 1; ++i) {
                const float y0 = cy + i * 0.32f * r;
                p.Fill([=](float x, float y) { return Segment(x, y, cx - 0.45f * r, y0, cx + 0.45f * r, y0) - line; },
                       ink);
            }
        } else { // two overlapping windows
            const float s = 0.26f * r;
            p.Fill([=](float x, float y) {
                return std::fabs(RoundBox(x, y, cx - 0.14f * r, cy - 0.14f * r, s, s, 0.2f * s)) - line * 0.8f;
            }, ink);
            p.Fill([=](float x, float y) { return RoundBox(x, y, cx + 0.14f * r, cy + 0.14f * r, s, s, 0.2f * s); },
                   ink);
        }
    }
}

#pragma pack(push, 1)
struct PromptAssetHeader {
    char magic[4];       // 'SPMP'
    uint32_t version;    // 1
    uint32_t count;      // 298
    uint32_t ent_offset; // 32
    uint32_t dat_offset; // 19104
    uint8_t reserved[12];
};
static_assert(sizeof(PromptAssetHeader) == 32);

struct PromptAssetEntry {
    char id[32];
    uint16_t theme;     // 0 = OBSIDIAN, 1 = IVORY
    uint16_t pw, ph;    // nominal 128 height, width according to aspect
    uint16_t reserved;
    float uw, uh;       // physical aspect ratio (1.5, 1.0, 2.25, 6.25, etc.)
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t flags;
    uint32_t reserved2;
};
static_assert(sizeof(PromptAssetEntry) == 64);
#pragma pack(pop)

class PromptAssetBank {
public:
    static PromptAssetBank& Instance() {
        static PromptAssetBank bank;
        return bank;
    }

    PromptAssetBank() {
        Load();
    }

    bool IsLoaded() const { return loaded_; }

    const PromptAssetEntry* Find(const std::string& key_or_label, int theme = 0) const {
        if (!loaded_ || key_or_label.empty()) return nullptr;
        std::string canon = Canonicalize(key_or_label);
        for (const auto& e : entries_) {
            if (e.theme == theme && std::strcmp(e.id, canon.c_str()) == 0) {
                return &e;
            }
        }
        for (const auto& e : entries_) {
            if (std::strcmp(e.id, canon.c_str()) == 0) {
                return &e;
            }
        }
        return nullptr;
    }

    const PromptAssetEntry* Find1U(const std::string& key_or_label, int theme = 0) const {
        if (!loaded_ || key_or_label.empty()) return nullptr;
        std::string canon = Canonicalize(key_or_label);
        std::string u1 = canon + "_1U";
        for (const auto& e : entries_) {
            if (e.theme == theme && std::strcmp(e.id, u1.c_str()) == 0) {
                return &e;
            }
        }
        for (const auto& e : entries_) {
            if (std::strcmp(e.id, u1.c_str()) == 0) {
                return &e;
            }
        }
        return Find(key_or_label, theme);
    }

    const uint8_t* GetData(const PromptAssetEntry& entry) const {
        if (!loaded_) return nullptr;
        if (size_t(data_offset_ + entry.data_offset + entry.data_size) <= bytes_.size()) {
            return bytes_.data() + data_offset_ + entry.data_offset;
        }
        return nullptr;
    }

private:
    bool loaded_ = false;
    uint32_t data_offset_ = 0;
    std::vector<uint8_t> bytes_;
    std::vector<PromptAssetEntry> entries_;

    static std::string ToUpper(std::string s) {
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

    static std::string Canonicalize(const std::string& input) {
        std::string s = ToUpper(input);
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();

        if (s.empty()) return "";
        if (s == "TAB") return "TAB";
        if (s == "SPACE" || s == " ") return "SPACE";
        if (s == "RETURN" || s == "ENTER") return "ENTER";
        if (s == "ESC" || s == "ESCAPE") return "ESCAPE";
        if (s == "BKSP" || s == "BACKSPACE") return "BACKSPACE";
        if (s == "SHIFT" || s == "LEFT SHIFT" || s == "SHIFT_LEFT" || s == "LSHIFT") return "SHIFT_LEFT";
        if (s == "RIGHT SHIFT" || s == "SHIFT_RIGHT" || s == "RSHIFT") return "SHIFT_RIGHT";
        if (s == "CTRL" || s == "LEFT CTRL" || s == "CONTROL_LEFT" || s == "LCTRL") return "CONTROL_LEFT";
        if (s == "RIGHT CTRL" || s == "CONTROL_RIGHT" || s == "RCTRL") return "CONTROL_RIGHT";
        if (s == "ALT" || s == "LEFT ALT" || s == "ALT_LEFT" || s == "LALT") return "ALT_LEFT";
        if (s == "RIGHT ALT" || s == "ALT_RIGHT" || s == "RALT") return "ALT_RIGHT";
        if (s == "CAPS" || s == "CAPSLOCK" || s == "CAPS_LOCK") return "CAPS_LOCK";
        if (s == "DEL" || s == "DELETE") return "DELETE";
        if (s == "INS" || s == "INSERT") return "INSERT";
        if (s == "HOME") return "HOME";
        if (s == "END") return "END";
        if (s == "PGUP" || s == "PAGE_UP") return "PAGE_UP";
        if (s == "PGDN" || s == "PAGE_DOWN") return "PAGE_DOWN";
        if (s == "\xe2\x86\x91" || s == "UP" || s == "ARROW_UP") return "ARROW_UP";
        if (s == "\xe2\x86\x93" || s == "DOWN" || s == "ARROW_DOWN") return "ARROW_DOWN";
        if (s == "\xe2\x86\x90" || s == "LEFT" || s == "ARROW_LEFT") return "ARROW_LEFT";
        if (s == "\xe2\x86\x92" || s == "RIGHT" || s == "ARROW_RIGHT") return "ARROW_RIGHT";

        if (s == "@LMB" || s == "MOUSE_LEFT" || s == "LEFT BUTTON") return "MOUSE_LEFT";
        if (s == "@RMB" || s == "MOUSE_RIGHT" || s == "RIGHT BUTTON") return "MOUSE_RIGHT";
        if (s == "@MMB" || s == "MOUSE_MIDDLE" || s == "MIDDLE BUTTON") return "MOUSE_MIDDLE";
        if (s == "@WHEELUP" || s == "WHEEL UP" || s == "MOUSE_SCROLL_UP") return "MOUSE_SCROLL_UP";
        if (s == "@WHEELDOWN" || s == "WHEEL DOWN" || s == "MOUSE_SCROLL_DOWN") return "MOUSE_SCROLL_DOWN";
        if (s == "@X1" || s == "BACK BUTTON" || s == "MOUSE_SIDE_1") return "MOUSE_SIDE_1";
        if (s == "@X2" || s == "FORWARD BUTTON" || s == "MOUSE_SIDE_2") return "MOUSE_SIDE_2";

        if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'Z') {
            return std::string("KEY_") + s[0];
        }
        if (s.size() == 1 && s[0] >= '0' && s[0] <= '9') {
            return std::string("DIGIT_") + s[0];
        }
        return s;
    }

    void Load() {
        const char* paths[] = {
            "gow3_prompt_assets.bin",
            "Project/gow3_pc/gpu/shim/gow3_prompt_assets.bin",
            "Project/gow3_pc/out/gow3_prompt_assets.bin",
            "Distribution/Uncharted/gow3_prompt_assets.bin",
            "Build/Windows/gow3_prompt_assets.bin",
            "../Project/gow3_pc/gpu/shim/gow3_prompt_assets.bin",
            "../../Project/gow3_pc/gpu/shim/gow3_prompt_assets.bin"
        };
        for (const char* p : paths) {
            if (TryLoadFile(p)) return;
        }
        char mod[MAX_PATH] = {};
        if (GetModuleFileNameA(NULL, mod, MAX_PATH) > 0) {
            char* slash = std::strrchr(mod, '\\');
            if (!slash) slash = std::strrchr(mod, '/');
            if (slash) {
                std::strcpy(slash + 1, "gow3_prompt_assets.bin");
                if (TryLoadFile(mod)) return;
            }
        }
    }

    bool TryLoadFile(const char* filepath) {
        FILE* f = std::fopen(filepath, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < (long)sizeof(PromptAssetHeader)) {
            std::fclose(f);
            return false;
        }
        bytes_.resize(sz);
        size_t n = std::fread(bytes_.data(), 1, sz, f);
        std::fclose(f);
        if (n != size_t(sz)) {
            bytes_.clear();
            return false;
        }
        const auto* hdr = reinterpret_cast<const PromptAssetHeader*>(bytes_.data());
        if (std::memcmp(hdr->magic, "SPMP", 4) != 0 || hdr->version != 1) {
            bytes_.clear();
            return false;
        }
        data_offset_ = hdr->dat_offset;
        entries_.resize(hdr->count);
        for (uint32_t i = 0; i < hdr->count; ++i) {
            const auto* src_entry = reinterpret_cast<const PromptAssetEntry*>(
                bytes_.data() + hdr->ent_offset + i * sizeof(PromptAssetEntry));
            entries_[i] = *src_entry;
        }
        loaded_ = true;
        std::printf("Prompts: loaded %u vector prompt assets from %s\n", hdr->count, filepath);
        std::fflush(stdout);
        return true;
    }
};

static void BlitAsset(Canvas& canvas, const PromptAssetEntry& entry, const uint8_t* src_rgba,
                      float cx, float cy, float target_w, float target_h) {
    const int x0 = std::clamp(int(std::floor(cx - target_w / 2.0f)), 0, canvas.w - 1);
    const int y0 = std::clamp(int(std::floor(cy - target_h / 2.0f)), 0, canvas.h - 1);
    const int x1 = std::clamp(int(std::ceil(cx + target_w / 2.0f)), 0, canvas.w);
    const int y1 = std::clamp(int(std::ceil(cy + target_h / 2.0f)), 0, canvas.h);

    const float src_w = float(entry.pw);
    const float src_h = float(entry.ph);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float u = (float(x) + 0.5f - (cx - target_w / 2.0f)) / target_w * src_w - 0.5f;
            const float v = (float(y) + 0.5f - (cy - target_h / 2.0f)) / target_h * src_h - 0.5f;

            if (u < -0.5f || u >= src_w - 0.5f || v < -0.5f || v >= src_h - 0.5f) continue;

            const int iu = std::clamp(int(std::floor(u)), 0, int(entry.pw) - 2);
            const int iv = std::clamp(int(std::floor(v)), 0, int(entry.ph) - 2);
            const float fu = std::clamp(u - float(iu), 0.0f, 1.0f);
            const float fv = std::clamp(v - float(iv), 0.0f, 1.0f);

            const auto sample = [&](int su, int sv) -> const uint8_t* {
                return &src_rgba[(sv * entry.pw + su) * 4];
            };

            const uint8_t* c00 = sample(iu, iv);
            const uint8_t* c10 = sample(iu + 1, iv);
            const uint8_t* c01 = sample(iu, iv + 1);
            const uint8_t* c11 = sample(iu + 1, iv + 1);

            const float sa = ((c00[3] * (1.0f - fu) + c10[3] * fu) * (1.0f - fv) +
                              (c01[3] * (1.0f - fu) + c11[3] * fu) * fv) / 255.0f;
            if (sa <= 0.001f) continue;

            const float sr = ((c00[0] * (1.0f - fu) + c10[0] * fu) * (1.0f - fv) +
                              (c01[0] * (1.0f - fu) + c11[0] * fu) * fv) / 255.0f;
            const float sg = ((c00[1] * (1.0f - fu) + c10[1] * fu) * (1.0f - fv) +
                              (c01[1] * (1.0f - fu) + c11[1] * fu) * fv) / 255.0f;
            const float sb = ((c00[2] * (1.0f - fu) + c10[2] * fu) * (1.0f - fv) +
                              (c01[2] * (1.0f - fu) + c11[2] * fu) * fv) / 255.0f;

            float* p = &canvas.texels[(y * canvas.w + x) * 4];
            p[0] = sr * sa + p[0] * (1.0f - sa);
            p[1] = sg * sa + p[1] * (1.0f - sa);
            p[2] = sb * sa + p[2] * (1.0f - sa);
            p[3] = sa + p[3] * (1.0f - sa);
            canvas.drawn[y * canvas.w + x] = 1;
        }
    }
}

static void BlitAssetToPicture(Picture& pic, const PromptAssetEntry& entry, const uint8_t* src_rgba,
                               float cx, float cy, float target_w, float target_h) {
    const int x0 = std::clamp(int(std::floor(cx - target_w / 2.0f)), 0, pic.w - 1);
    const int y0 = std::clamp(int(std::floor(cy - target_h / 2.0f)), 0, pic.h - 1);
    const int x1 = std::clamp(int(std::ceil(cx + target_w / 2.0f)), 0, pic.w);
    const int y1 = std::clamp(int(std::ceil(cy + target_h / 2.0f)), 0, pic.h);

    const float src_w = float(entry.pw);
    const float src_h = float(entry.ph);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float u = (float(x) + 0.5f - (cx - target_w / 2.0f)) / target_w * src_w - 0.5f;
            const float v = (float(y) + 0.5f - (cy - target_h / 2.0f)) / target_h * src_h - 0.5f;

            if (u < -0.5f || u >= src_w - 0.5f || v < -0.5f || v >= src_h - 0.5f) continue;

            const int iu = std::clamp(int(std::floor(u)), 0, int(entry.pw) - 2);
            const int iv = std::clamp(int(std::floor(v)), 0, int(entry.ph) - 2);
            const float fu = std::clamp(u - float(iu), 0.0f, 1.0f);
            const float fv = std::clamp(v - float(iv), 0.0f, 1.0f);

            const auto sample = [&](int su, int sv) -> const uint8_t* {
                return &src_rgba[(sv * entry.pw + su) * 4];
            };

            const uint8_t* c00 = sample(iu, iv);
            const uint8_t* c10 = sample(iu + 1, iv);
            const uint8_t* c01 = sample(iu, iv + 1);
            const uint8_t* c11 = sample(iu + 1, iv + 1);

            const float sa = ((c00[3] * (1.0f - fu) + c10[3] * fu) * (1.0f - fv) +
                              (c01[3] * (1.0f - fu) + c11[3] * fu) * fv) / 255.0f;
            if (sa <= 0.001f) continue;

            const float sr = (c00[0] * (1.0f - fu) + c10[0] * fu) * (1.0f - fv) +
                             (c01[0] * (1.0f - fu) + c11[0] * fu) * fv;
            const float sg = (c00[1] * (1.0f - fu) + c10[1] * fu) * (1.0f - fv) +
                             (c01[1] * (1.0f - fu) + c11[1] * fu) * fv;
            const float sb = (c00[2] * (1.0f - fu) + c10[2] * fu) * (1.0f - fv) +
                             (c01[2] * (1.0f - fu) + c11[2] * fu) * fv;

            uint8_t* p = &pic.rgba[(y * pic.w + x) * 4];
            p[0] = uint8_t(std::clamp(sr * sa + p[0] * (1.0f - sa) + 0.5f, 0.0f, 255.0f));
            p[1] = uint8_t(std::clamp(sg * sa + p[1] * (1.0f - sa) + 0.5f, 0.0f, 255.0f));
            p[2] = uint8_t(std::clamp(sb * sa + p[2] * (1.0f - sa) + 0.5f, 0.0f, 255.0f));
            p[3] = 255;
        }
    }
}

using Labels = std::vector<std::string>;

// One binding in a w x h area: a key cap fitted to its label, the mouse, or a controller button.
void DrawBinding(Painter& p, Pass pass, float cx, float cy, float w, float h, const std::string& label) {
    if (!label.empty() && label[0] == '#') {
        DrawPadButton(p, pass, cx, cy, w, h, label);
        return;
    }
    const auto* asset = (w / h < 1.35f) ?
        PromptAssetBank::Instance().Find1U(label, 0) :
        PromptAssetBank::Instance().Find(label, 0);
    if (!asset) asset = PromptAssetBank::Instance().Find(label, 0);
    if (asset) {
        if (pass == Pass::Body) {
            const uint8_t* src = PromptAssetBank::Instance().GetData(*asset);
            if (src) {
                float aspect = asset->uw / asset->uh;
                float th = h * 1.05f;
                float tw = th * aspect;
                if (tw > w * 1.05f) {
                    tw = w * 1.05f;
                    th = tw / aspect;
                }
                BlitAsset(p.GetCanvas(), *asset, src, cx, cy, tw, th);
            }
        }
        return;
    }
    const MousePart part = MousePartOf(label);
    if (part != MousePart::None) {
        const float mh = h, mw = 0.62f * mh;
        DrawMouse(p, pass, cx, cy, mh, std::max(0.0f, (w - mw) / 2), part);
        return;
    }
    float kw = h;
    if (Codepoints(label).size() > 1) {
        const size_t chars = Codepoints(label).size();
        kw = std::clamp(Text::Width(label, 0.42f * h, LabelCondense(chars)) + 0.4f * h, h, w);
    }
    DrawKey(p, pass, cx, cy, std::min(kw, w), h, label);
}

// Four keys as an inverted T (WASD, the d-pad's keys).
void DrawCluster(Painter& p, const Cell& c, const std::string& up, const std::string& left,
                 const std::string& down, const std::string& right) {
    const float gap = 1.5f;
    const float s = std::min((c.w - 2.0f - 2 * gap) / 3, (c.h - 2.0f - gap) / 2);
    const float cx = c.x + c.w / 2.0f, cy = c.y + c.h / 2.0f;
    const float row0 = cy - (s + gap) / 2, row1 = cy + (s + gap) / 2;
    for (const Pass pass : {Pass::Glow, Pass::Body}) {
        DrawBinding(p, pass, cx, row0, s, s, up);
        DrawBinding(p, pass, cx - (s + gap), row1, s, s, left);
        DrawBinding(p, pass, cx, row1, s, s, down);
        DrawBinding(p, pass, cx + (s + gap), row1, s, s, right);
    }
}

void DrawPlan(Canvas& canvas, const Plan& plan, const Cell& c, const Labels& labels) {
    const auto label = [&](int i) -> const std::string& { return labels[i]; };
    switch (plan.draw) {
    case Draw::Key:
        if (label(plan.a).empty()) {
            return;
        }
        break;
    case Draw::Pair:
        if (label(plan.a).empty() || label(plan.b).empty()) {
            return;
        }
        break;
    case Draw::Dpad:
        if (label(GOW3_PROMPT_UP).empty() || label(GOW3_PROMPT_LEFT).empty() ||
            label(GOW3_PROMPT_DOWN).empty() || label(GOW3_PROMPT_RIGHT).empty()) {
            return;
        }
        break;
    case Draw::Stick:
        if (label(plan.a).empty()) {
            return;
        }
        break;
    }
    Cell cell = c;
    if (plan.id == 30) {
        cell.w = 77, cell.h = 50; // the part shared with glyph 14
        Painter{canvas, 0, 252, 84, 303};
    }
    Painter p{canvas, cell.x, cell.y, cell.x + cell.w, cell.y + cell.h};
    const float cx = cell.x + cell.w / 2.0f, cy = cell.y + cell.h / 2.0f;
    const float w = cell.w - 2.0f, h = cell.h - 2.0f;
    switch (plan.draw) {
    case Draw::Key:
        for (const Pass pass : {Pass::Glow, Pass::Body}) {
            DrawBinding(p, pass, cx, cy, w, h, label(plan.a));
        }
        break;
    case Draw::Pair: {
        const float gap = 2.0f, s = std::min((w - gap) / 2, h);
        for (const Pass pass : {Pass::Glow, Pass::Body}) {
            DrawBinding(p, pass, cx - (s + gap) / 2, cy, s, s, label(plan.a));
            DrawBinding(p, pass, cx + (s + gap) / 2, cy, s, s, label(plan.b));
        }
        break;
    }
    case Draw::Dpad:
        DrawCluster(p, cell, label(GOW3_PROMPT_UP), label(GOW3_PROMPT_LEFT), label(GOW3_PROMPT_DOWN),
                    label(GOW3_PROMPT_RIGHT));
        break;
    case Draw::Stick:
        if (MousePartOf(label(plan.a)) != MousePart::None) {
            for (const Pass pass : {Pass::Glow, Pass::Body}) {
                DrawBinding(p, pass, cx, cy, w, h, label(plan.a));
            }
        } else if (!label(plan.a + 1).empty() && !label(plan.a + 2).empty() &&
                   !label(plan.a + 3).empty()) {
            DrawCluster(p, cell, label(plan.a), label(plan.a + 1), label(plan.a + 2), label(plan.a + 3));
        }
        break;
    }
}

std::shared_ptr<const Canvas> Render(const Labels& labels) {
    auto canvas = std::make_shared<Canvas>();
    for (const Plan& plan : plans) {
        for (const Cell& cell : cells) {
            if (cell.id == plan.id) {
                DrawPlan(*canvas, plan, cell, labels);
            }
        }
    }
    return canvas;
}

// The redrawn cells over the guest's atlas (1D-tiled: 8x8 tiles in rows, Z order inside).
std::vector<u8> Compose(const Canvas& canvas, const u8* guest, Kind kind) {
    std::vector<u8> out(Size * Size * 4);
    for (int y = 0; y < Size; ++y) {
        for (int x = 0; x < Size; ++x) {
            u32 element = 0;
            for (int b = 0; b < 3; ++b) {
                element |= ((x >> b) & 1) << (2 * b);
                element |= ((y >> b) & 1) << (2 * b + 1);
            }
            const u32 tile = (y / 8) * (Size / 8) + x / 8;
            u8* o = &out[(y * Size + x) * 4];
            std::memcpy(o, guest + (tile * 64 + element) * 4, 4);
            if (!canvas.drawn[y * Size + x]) {
                continue;
            }
            const float* t = &canvas.texels[(y * Size + x) * 4];
            const float a = t[3];
            // Transparent texels take the glow colour: filtering at the edges blends towards it.
            const Rgb c = a > 1e-4f ? Rgb{t[0] / a, t[1] / a, t[2] / a} : glow;
            const auto byte = [](float v) { return u8(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); };
            switch (kind) {
            case Kind::Rgba:
                o[0] = byte(c.r), o[1] = byte(c.g), o[2] = byte(c.b), o[3] = byte(a);
                break;
            case Kind::Color:
                o[0] = byte(c.r), o[1] = byte(c.g), o[2] = byte(c.b);
                break;
            case Kind::Alpha:
                o[0] = o[1] = o[2] = byte(a);
                break;
            default:
                break;
            }
        }
    }
    return out;
}

// ---- Button pictures (BC1)
Rgb Decode565(u32 c) {
    return {float((c >> 11) & 31) / 31.0f, float((c >> 5) & 63) / 63.0f, float(c & 31) / 31.0f};
}

// Mip 0 of a 1D-tiled BC1 image (8x8 blocks per tile, Z order inside, tiles in rows).
Picture DecodeBc1(const u8* guest, int width, int height) {
    Picture pic{width, height, std::vector<u8>(size_t(width) * height * 4)};
    const int nbx = width / 4, nby = height / 4;
    for (int by = 0; by < nby; ++by) {
        for (int bx = 0; bx < nbx; ++bx) {
            u32 element = 0;
            for (int b = 0; b < 3; ++b) {
                element |= ((bx >> b) & 1) << (2 * b);
                element |= ((by >> b) & 1) << (2 * b + 1);
            }
            const u8* blk = guest + ((by / 8) * (nbx / 8) + bx / 8) * 512 + element * 8;
            const u32 c0 = blk[0] | blk[1] << 8, c1 = blk[2] | blk[3] << 8;
            const Rgb a = Decode565(c0), b = Decode565(c1);
            Rgb pal[4] = {a, b, Mix(a, b, 1.0f / 3), Mix(a, b, 2.0f / 3)};
            if (c0 <= c1) {
                pal[2] = Mix(a, b, 0.5f);
            }
            const u32 bits = blk[4] | blk[5] << 8 | blk[6] << 16 | u32(blk[7]) << 24;
            for (int k = 0; k < 16; ++k) {
                const u32 i = (bits >> (2 * k)) & 3;
                u8* o = &pic.rgba[((by * 4 + k / 4) * width + bx * 4 + k % 4) * 4];
                o[0] = u8(pal[i].r * 255 + 0.5f), o[1] = u8(pal[i].g * 255 + 0.5f), o[2] = u8(pal[i].b * 255 + 0.5f);
                o[3] = c0 <= c1 && i == 3 ? 0 : 255;
            }
        }
    }
    return pic;
}

u32 To565(const float (&c)[3]) {
    const auto q = [](float v, int bits) {
        const int max = (1 << bits) - 1;
        return u32(std::clamp(int(v / 255.0f * max + 0.5f), 0, max));
    };
    return q(c[0], 5) << 11 | q(c[1], 6) << 5 | q(c[2], 5);
}

// One 4x4 block: endpoints along the colours' main axis, punch-through for transparent texels.
void EncodeBlock(const u8 (&px)[16][4], u8* out) {
    float mean[3] = {}, n = 0;
    bool transparent = false;
    for (const auto& c : px) {
        if (c[3] < 128) {
            transparent = true;
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            mean[i] += c[i];
        }
        ++n;
    }
    if (n == 0) {
        std::memcpy(out, "\0\0\0\0\xff\xff\xff\xff", 8);
        return;
    }
    for (float& m : mean) {
        m /= n;
    }
    float cov[3][3] = {};
    for (const auto& c : px) {
        if (c[3] < 128) {
            continue;
        }
        const float d[3] = {c[0] - mean[0], c[1] - mean[1], c[2] - mean[2]};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                cov[i][j] += d[i] * d[j];
            }
        }
    }
    float axis[3] = {0.577f, 0.577f, 0.577f};
    for (int it = 0; it < 8; ++it) {
        float next[3];
        for (int i = 0; i < 3; ++i) {
            next[i] = cov[i][0] * axis[0] + cov[i][1] * axis[1] + cov[i][2] * axis[2];
        }
        const float len = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2]);
        if (len < 1e-6f) {
            break;
        }
        for (int i = 0; i < 3; ++i) {
            axis[i] = next[i] / len;
        }
    }
    float lo = 1e9f, hi = -1e9f;
    for (const auto& c : px) {
        if (c[3] < 128) {
            continue;
        }
        const float t = (c[0] - mean[0]) * axis[0] + (c[1] - mean[1]) * axis[1] + (c[2] - mean[2]) * axis[2];
        lo = std::min(lo, t), hi = std::max(hi, t);
    }
    const float inset = (hi - lo) / 16;
    float e0[3], e1[3];
    for (int i = 0; i < 3; ++i) {
        e0[i] = mean[i] + axis[i] * (hi - inset);
        e1[i] = mean[i] + axis[i] * (lo + inset);
    }
    u32 c0 = To565(e0), c1 = To565(e1);
    if (transparent ? c0 > c1 : c0 < c1) {
        std::swap(c0, c1);
    }
    const Rgb a = Decode565(c0), b = Decode565(c1);
    Rgb pal[4];
    int count;
    if (c0 > c1) {
        pal[0] = a, pal[1] = b, pal[2] = Mix(a, b, 1.0f / 3), pal[3] = Mix(a, b, 2.0f / 3), count = 4;
    } else {
        pal[0] = a, pal[1] = b, pal[2] = Mix(a, b, 0.5f), count = 3;
    }
    u32 bits = 0;
    for (int k = 0; k < 16; ++k) {
        u32 best = 3;
        if (px[k][3] >= 128) {
            float best_d = 1e9f;
            for (int i = 0; i < count; ++i) {
                const float dr = pal[i].r * 255 - px[k][0], dg = pal[i].g * 255 - px[k][1], db = pal[i].b * 255 - px[k][2];
                const float d = dr * dr + dg * dg + db * db;
                if (d < best_d) {
                    best_d = d, best = u32(i);
                }
            }
        }
        bits |= best << (2 * k);
    }
    out[0] = u8(c0), out[1] = u8(c0 >> 8), out[2] = u8(c1), out[3] = u8(c1 >> 8);
    out[4] = u8(bits), out[5] = u8(bits >> 8), out[6] = u8(bits >> 16), out[7] = u8(bits >> 24);
}

// Linear BC1 blocks of a picture, appended to out.
void EncodeBc1(const Picture& pic, std::vector<u8>& out) {
    const int bw = std::max(1, (pic.w + 3) / 4), bh = std::max(1, (pic.h + 3) / 4);
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            u8 px[16][4];
            for (int k = 0; k < 16; ++k) {
                const int x = std::min(bx * 4 + k % 4, pic.w - 1), y = std::min(by * 4 + k / 4, pic.h - 1);
                std::memcpy(px[k], &pic.rgba[(y * pic.w + x) * 4], 4);
            }
            const size_t at = out.size();
            out.resize(at + 8);
            EncodeBlock(px, &out[at]);
        }
    }
}

Picture Half(const Picture& pic) {
    Picture half{std::max(1, pic.w / 2), std::max(1, pic.h / 2), {}};
    half.rgba.resize(size_t(half.w) * half.h * 4);
    for (int y = 0; y < half.h; ++y) {
        for (int x = 0; x < half.w; ++x) {
            int sum[3] = {}, opaque = 0;
            for (int k = 0; k < 4; ++k) {
                const int sx = std::min(x * 2 + k % 2, pic.w - 1), sy = std::min(y * 2 + k / 2, pic.h - 1);
                const u8* s = &pic.rgba[(sy * pic.w + sx) * 4];
                for (int i = 0; i < 3; ++i) {
                    sum[i] += s[i];
                }
                opaque += s[3] >= 128;
            }
            u8* o = &half.rgba[(y * half.w + x) * 4];
            o[0] = u8(sum[0] / 4), o[1] = u8(sum[1] / 4), o[2] = u8(sum[2] / 4), o[3] = opaque >= 2 ? 255 : 0;
        }
    }
    return half;
}

void FlipRows(Picture& pic) {
    const size_t row = size_t(pic.w) * 4;
    for (int y = 0; y < pic.h / 2; ++y) {
        std::swap_ranges(pic.rgba.begin() + y * row, pic.rgba.begin() + (y + 1) * row,
                         pic.rgba.begin() + (pic.h - 1 - y) * row);
    }
}

float Luma(const u8* c) {
    return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
}

// The picture on a canvas (opaque) and back.
Canvas ToCanvas(const Picture& pic) {
    Canvas c{pic.w, pic.h, std::vector<float>(size_t(pic.w) * pic.h * 4), std::vector<u8>(size_t(pic.w) * pic.h)};
    for (size_t i = 0; i < size_t(pic.w) * pic.h; ++i) {
        for (int k = 0; k < 3; ++k) {
            c.texels[i * 4 + k] = pic.rgba[i * 4 + k] / 255.0f;
        }
        c.texels[i * 4 + 3] = 1.0f;
    }
    return c;
}
void FromCanvas(const Canvas& c, Picture& pic) {
    for (size_t i = 0; i < size_t(pic.w) * pic.h; ++i) {
        for (int k = 0; k < 3; ++k) {
            pic.rgba[i * 4 + k] = u8(std::clamp(c.texels[i * 4 + k] * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
}

// What a binding label draws inside a picture's button: text (a key, or a controller's letters)
// or a mouse.
void DrawPictureLabel(Painter& p, const std::string& label, float cx, float cy, float height, float max_width,
                      Rgb color) {
    if (label.empty()) {
        return;
    }
    const auto* asset = (max_width / height < 1.35f) ?
        PromptAssetBank::Instance().Find1U(label, 0) :
        PromptAssetBank::Instance().Find(label, 0);
    if (!asset) asset = PromptAssetBank::Instance().Find(label, 0);
    if (asset) {
        const uint8_t* src = PromptAssetBank::Instance().GetData(*asset);
        if (src) {
            float aspect = asset->uw / asset->uh;
            float th = height * 0.96f;
            float tw = th * aspect;
            if (tw > max_width * 0.98f) {
                tw = max_width * 0.98f;
                th = tw / aspect;
            }
            BlitAsset(p.GetCanvas(), *asset, src, cx, cy, tw, th);
            return;
        }
    }
    const MousePart part = MousePartOf(label);
    if (part != MousePart::None) {
        for (const Pass pass : {Pass::Glow, Pass::Body}) {
            DrawMouse(p, pass, cx, cy, height, 0.0f, part);
        }
        return;
    }
    const std::string text_label = label[0] == '#' ? label.substr(1) : label;
    const size_t chars = Codepoints(text_label).size();
    const float cap = chars == 1 ? 0.72f * height : 0.50f * height;
    const Text text{text_label, cx, cy, cap, max_width, LabelCondense(chars)};
    const float bold = 0.08f * cap;
    // The glow ends inside the glyphs' distance fields (they end Text::Pad pixels out).
    const float glow_radius = std::min(0.12f * height, Text::Pad - bold - 1.0f);
    p.Halo([&](float x, float y) { return text.Distance(x, y) - bold; }, glow_radius, 0.45f, color);
    p.Fill([&](float x, float y) { return text.Distance(x, y) - bold; }, color);
}

// A round face button (upright): its symbol replaced by the binding; for the controller set the
// glow colour becomes the face button's (Xbox A green, B red, X blue, Y yellow).
void RedrawFace(Picture& pic, const std::string& label, bool controller) {
    const int w = pic.w, h = pic.h;
    float bg[3] = {}, nb = 0;
    float cx = 0, cy = 0;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] < 128) {
                continue;
            }
            if (x < w / 16 || y < h / 16 || x >= w - w / 16 || y >= h - h / 16) {
                for (int i = 0; i < 3; ++i) {
                    bg[i] += c[i];
                }
                ++nb;
            } else if (Luma(c) < 80) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            }
        }
    }
    if (nb == 0 || x1 < x0) {
        return;
    }
    for (float& v : bg) {
        v /= nb;
    }
    cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
    // The dark button: round, or an ellipse (pictures of the button pressed are flat).
    const float rx = (x1 - x0 + 1) / 2.0f, ry = (y1 - y0 + 1) / 2.0f, r = std::min(rx, ry);
    const auto unit = [=](float x, float y) { return std::hypot((x - cx) / rx, (y - cy) / ry); };
    float body[3] = {}, symbol[3] = {}, n_body = 0, n_symbol = 0, symbol_reach = 0.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            const float d = unit(x + 0.5f, y + 0.5f) * r;
            if (c[3] < 128 || d > 0.95f * r) {
                continue;
            }
            if (Luma(c) < 80 && d > 0.6f * r) {
                for (int i = 0; i < 3; ++i) {
                    body[i] += c[i];
                }
                ++n_body;
            } else if (Luma(c) >= 80 && d < 0.75f * r) {
                for (int i = 0; i < 3; ++i) {
                    symbol[i] += c[i];
                }
                ++n_symbol;
            }
            if (std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}) > 60 && unit(x + 0.5f, y + 0.5f) < 0.92f) {
                symbol_reach = std::max(symbol_reach, unit(x + 0.5f, y + 0.5f));
            }
        }
    }
    const Rgb body_color = n_body ? Rgb{body[0] / n_body / 255, body[1] / n_body / 255, body[2] / n_body / 255}
                                  : Rgb{0.18f, 0.18f, 0.18f};
    Rgb symbol_color = n_symbol ? Rgb{symbol[0] / n_symbol / 255, symbol[1] / n_symbol / 255, symbol[2] / n_symbol / 255}
                                : Rgb{bg[0] / 255, bg[1] / 255, bg[2] / 255};
    // The symbol's bright lines: towards the glow colour (the average includes their dim edges).
    symbol_color = Mix(symbol_color, Rgb{bg[0] / 255, bg[1] / 255, bg[2] / 255}, 0.6f);
    if (controller && label.size() == 2 && label[0] == '#') {
        // Recolour: each texel's part of the glow colour (against the dark body) takes the new one.
        const Rgb face = FaceColor(label[1]);
        const float nf[3] = {face.r * 255, face.g * 255, face.b * 255};
        const float bd[3] = {body_color.r * 255, body_color.g * 255, body_color.b * 255};
        const float axis[3] = {bg[0] - bd[0], bg[1] - bd[1], bg[2] - bd[2]};
        const float len2 = std::max(1.0f, axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            u8* c = &pic.rgba[i * 4];
            const float s = std::clamp(((c[0] - bd[0]) * axis[0] + (c[1] - bd[1]) * axis[1] + (c[2] - bd[2]) * axis[2]) / len2,
                                       0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) {
                c[k] = u8(std::clamp(c[k] + s * (nf[k] - bg[k]), 0.0f, 255.0f));
            }
        }
        symbol_color = Mix(face, Rgb{1, 1, 1}, 0.15f);
    }
    Canvas canvas = ToCanvas(pic);
    Painter p{canvas, 0, 0, w, h, false};
    const float inner = 0.8f * r;
    const Rgb top = Mix(body_color, Rgb{1, 1, 1}, 0.08f), bottom = Mix(body_color, Rgb{0, 0, 0}, 0.25f);
    const float cover = std::clamp(symbol_reach + 0.04f, 0.8f, 0.92f);
    p.FillShaded([=](float x, float y) { return (unit(x, y) - cover) * r; },
                 [=](float, float y) { return Mix(top, bottom, (y - (cy - cover * ry)) / (2 * cover * ry)); });
    const float face_size = 1.85f * r;
    DrawPictureLabel(p, label, cx, cy, face_size, face_size, symbol_color);
    FromCanvas(canvas, pic);
}

// Uncharted 3's shoulder buttons (upright): the "L1" etc. on the dark button replaced.
void RedrawShoulder(Picture& pic, const std::string& label) {
    const int w = pic.w, h = pic.h;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    float body[3] = {}, n_body = 0;
    for (int y = h / 16; y < h - h / 16; ++y) {
        for (int x = w / 16; x < w - w / 16; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] >= 128 && Luma(c) < 90) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                for (int i = 0; i < 3; ++i) {
                    body[i] += c[i];
                }
                ++n_body;
            }
        }
    }
    if (x1 < x0 || n_body == 0) {
        return;
    }
    // The text: light texels well inside the button.
    const int ix0 = x0 + (x1 - x0) / 8, ix1 = x1 - (x1 - x0) / 8, iy0 = y0 + (y1 - y0) / 6, iy1 = y1 - (y1 - y0) / 6;
    int tx0 = w, ty0 = h, tx1 = -1, ty1 = -1;
    float ink_sum[3] = {}, n_ink = 0;
    for (int y = iy0; y <= iy1; ++y) {
        for (int x = ix0; x <= ix1; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] >= 128 && Luma(c) >= 120) {
                tx0 = std::min(tx0, x), ty0 = std::min(ty0, y), tx1 = std::max(tx1, x), ty1 = std::max(ty1, y);
                for (int i = 0; i < 3; ++i) {
                    ink_sum[i] += c[i];
                }
                ++n_ink;
            }
        }
    }
    if (tx1 < tx0) {
        tx0 = ix0, tx1 = ix1, ty0 = iy0, ty1 = iy1;
    }
    const Rgb body_color{body[0] / n_body / 255, body[1] / n_body / 255, body[2] / n_body / 255};
    const Rgb text_color = n_ink ? Rgb{ink_sum[0] / n_ink / 255, ink_sum[1] / n_ink / 255, ink_sum[2] / n_ink / 255}
                                 : Rgb{0.93f, 0.88f, 0.78f};
    // The button: between its dark texels along both the row and the column (its text included).
    std::vector<int> row0(h, w), row1(h, -1), col0(w, h), col1(w, -1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] >= 128 && Luma(c) < 90) {
                row0[y] = std::min(row0[y], x), row1[y] = std::max(row1[y], x);
                col0[x] = std::min(col0[x], y), col1[x] = std::max(col1[x], y);
            }
        }
    }
    std::vector<u8> on_button(size_t(w) * h);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            on_button[y * w + x] = x >= row0[y] && x <= row1[y] && y >= col0[x] && y <= col1[x];
        }
    }
    const auto button = [&](float x, float y) {
        const int ix = std::clamp(int(x), 0, w - 1), iy = std::clamp(int(y), 0, h - 1);
        return on_button[iy * w + ix] ? -1.0f : 1.0f;
    };
    Canvas canvas = ToCanvas(pic);
    Painter p{canvas, 0, 0, w, h, false};
    const float pad = 0.04f * w;
    const float cx = (tx0 + tx1 + 1) / 2.0f, cy = (ty0 + ty1 + 1) / 2.0f;
    const float hw = (tx1 - tx0 + 1) / 2.0f + pad, hh = (ty1 - ty0 + 1) / 2.0f + pad;
    const Rgb top = Mix(body_color, Rgb{1, 1, 1}, 0.05f), bottom = Mix(body_color, Rgb{0, 0, 0}, 0.15f);
    p.FillShaded([&](float x, float y) { return std::max(RoundBox(x, y, cx, cy, hw, hh, 0.4f * hh), button(x, y)); },
                 [=](float, float y) { return Mix(top, bottom, (y - (cy - hh)) / (2 * hh)); });
    const float bw = (x1 - x0) * 0.95f, bh = (y1 - y0) * 0.92f;
    DrawPictureLabel(p, label, (x0 + x1 + 1) / 2.0f, cy, std::min(bh, 3.2f * hh), bw, text_color);
    FromCanvas(canvas, pic);
}

// Uncharted 3's d-pad picture with one arm lit (orange): the bound key is written on that arm, in
// the dark of the other arms, so the picture still reads as "this direction" (keyboard and mouse
// only: other controllers' d-pads look the same).
void RedrawDpad(Picture& pic, const std::string& label) {
    const int w = pic.w, h = pic.h;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    float dark[3] = {}, n_dark = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u8* c = &pic.rgba[(y * w + x) * 4];
            if (c[3] < 128) {
                continue;
            }
            // The lit arm: saturated orange (red well above blue).
            if (c[0] > 150 && c[0] - c[2] > 90 && c[0] >= c[1]) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            } else if (Luma(c) < 90) {
                for (int i = 0; i < 3; ++i) {
                    dark[i] += c[i];
                }
                ++n_dark;
            }
        }
    }
    if (x1 < x0 || (x1 - x0) < w / 16 || (y1 - y0) < h / 16) {
        return;
    }
    const Rgb ink_color = n_dark ? Rgb{dark[0] / n_dark / 255, dark[1] / n_dark / 255, dark[2] / n_dark / 255}
                                 : Rgb{0.12f, 0.12f, 0.11f};
    Canvas canvas = ToCanvas(pic);
    Painter p{canvas, 0, 0, w, h, false};
    const float cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
    const float aw = float(x1 - x0 + 1), ah = float(y1 - y0 + 1);
    DrawPictureLabel(p, label, cx, cy, 0.92f * std::min(aw, ah), 0.95f * aw, ink_color);
    FromCanvas(canvas, pic);
}

// Drake's Journal & Map pictures (512x256 BC1, upright):
// Preserves the leather journal / antique map on the left (x < 192), clears the PS4 touchpad badge,
// and blits the authentic vector keycap (TAB or configured key) preserving physical 1.5U proportions.
void RedrawJournal(Picture& pic, const std::string& label, bool controller, bool is_map) {
    const int w = pic.w, h = pic.h;
    if (w < 256 || h < 128) return;

    const float sx = float(w) / 512.0f;
    const float sy = float(h) / 256.0f;

    const float cx = (is_map ? 238.5f : 248.0f) * sx;
    const float cy = (is_map ? 163.5f : 157.0f) * sy;
    const int bx0 = int((is_map ? 170.0f : 180.0f) * sx);
    const int bx1 = int((is_map ? 310.0f : 320.0f) * sx);
    const int by0 = int(75.0f * sy);
    const int by1 = int((is_map ? 245.0f : 235.0f) * sy);

    for (int y = std::max(0, by0); y <= std::min(h - 1, by1); ++y) {
        for (int x = std::max(0, bx0); x <= std::min(w - 1, bx1); ++x) {
            uint8_t* p = &pic.rgba[(y * w + x) * 4];
            float luma = 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2];
            if (luma > 65.0f || (p[0] > 70 && p[1] > 70 && p[2] > 70)) {
                p[0] = 48; p[1] = 44; p[2] = 40; p[3] = 255;
            }
        }
    }

    if (!controller) {
        std::string effective_key = label.empty() ? "TAB" : label;
        const auto* asset = PromptAssetBank::Instance().Find(effective_key, 0);
        if (!asset) asset = PromptAssetBank::Instance().Find("TAB", 0);
        if (asset) {
            const uint8_t* src = PromptAssetBank::Instance().GetData(*asset);
            if (src) {
                float th = 112.0f * sy;
                float tw = th * (asset->uw / asset->uh);
                BlitAssetToPicture(pic, *asset, src, cx, cy, tw, th);
                return;
            }
        }
    } else {
        Canvas canvas = ToCanvas(pic);
        Painter p{canvas, bx0, by0, bx1 + 1, by1 + 1, false};
        const float r = 24.0f;
        const float edge = 2.0f;
        DrawShape(p, Pass::Glow, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, glow, "", cx, cy, 0, 0, ink);
        DrawShape(p, Pass::Body, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; }, edge, cy - r,
                  2 * r, glow, "", cx, cy, 0, 0, ink);
        const float line = 2.0f;
        const float s = 7.0f;
        p.Fill([=](float x, float y) {
            return std::fabs(RoundBox(x, y, cx - 3.5f, cy - 3.5f, s, s, 1.5f)) - line * 0.8f;
        }, ink);
        p.Fill([=](float x, float y) {
            return RoundBox(x, y, cx + 3.5f, cy + 3.5f, s, s, 1.5f);
        }, ink);
        FromCanvas(canvas, pic);
    }
}

// Label sets: keyboard and mouse, other controllers. Device: which icons show (GOW3_PROMPT_*).
constexpr int Sets = 2;
int SetOf(int device) {
    return device == 1 ? 0 : device == 2 ? 1 : -1;
}

struct State {
    std::mutex mutex;
    u64 generation[Sets]{};
    std::shared_ptr<const Canvas> canvas[Sets];
    Labels labels[Sets];
    struct Output {
        u64 generation;
        std::shared_ptr<const Texels> texels;
    };
    std::unordered_map<u64, Output> outputs[Sets];     // by atlas hash
    std::vector<std::pair<VAddr, u32>> seen;           // atlas uploads (invalidated on a switch)
    std::unordered_set<u64> logged;                    // GOW3_PROMPTS_LOG: address ^ hash
};
State& GetState() {
    static State state;
    return state;
}
std::atomic<int> device{0}; // 0 the game's PlayStation icons, 1 keyboard and mouse, 2 other controller

void InvalidateSeen() {
    std::vector<std::pair<VAddr, u32>> seen;
    {
        auto& state = GetState();
        std::scoped_lock lock{state.mutex};
        seen = state.seen;
    }
#ifndef GOW3_PROMPTS_TEST
    auto* rasterizer = Core::Memory::Instance()->GetRasterizer();
    if (!rasterizer) {
        return;
    }
    for (const auto& [address, size] : seen) {
        rasterizer->GetTextureCache().InvalidateMemory(address, size);
    }
#endif
}

} // namespace

namespace {

std::shared_ptr<const Texels> MakePicture(const KnownTexture& known, const u8* guest, int width, int height,
                                          u32 levels, const std::string& label, bool controller) {
    Picture pic = DecodeBc1(guest, width, height);
    FlipRows(pic); // stored upside down
    if (known.kind == Kind::Face) {
        RedrawFace(pic, label, controller);
    } else if (known.kind == Kind::Dpad) {
        RedrawDpad(pic, label);
    } else if (known.kind == Kind::Journal) {
        RedrawJournal(pic, label, controller, known.hash == 0x3204d997bd026ab3ULL);
    } else {
        RedrawShoulder(pic, label);
    }
    FlipRows(pic);
    auto texels = std::make_shared<Texels>();
    for (u32 m = 0; m < levels; ++m) {
        if (m) {
            pic = Half(pic);
        }
        const u32 offset = u32(texels->data.size());
        EncodeBc1(pic, texels->data);
        texels->mips.push_back({offset, u32(std::max(1, (pic.w + 3) / 4) * 4), u32(std::max(1, (pic.h + 3) / 4) * 4)});
    }
    return texels;
}

} // namespace

std::shared_ptr<const Texels> Replacement(VAddr address, u32 guest_size, u32 mip0_size, u32 width, u32 height,
                                          u32 levels, bool bc1, u32 tile_mode) {
    static const bool log = [] {
        const char* env = std::getenv("GOW3_PROMPTS_LOG");
        return env && env[0] == '1';
    }();
    const bool atlas = !bc1 && width == Size && height == Size && levels == 1 && mip0_size == Size * Size * 4;
    const auto side = [](u32 v) { return v >= 64 && v <= 512 && (v & (v - 1)) == 0; };
    // Redrawn pictures are 1D-tiled (tile mode 13: their mip 0 is width * height / 2 bytes).
    const bool picture = bc1 && side(width) && side(height) && mip0_size == width * height / 2;
    if (!atlas && !picture) {
        return nullptr;
    }
    const u8* guest = reinterpret_cast<const u8*>(address);
    const u64 hash = XXH3_64bits(guest, mip0_size);
    const KnownTexture* known = nullptr;
    for (const auto& k : known_textures) {
        if (k.hash == hash &&
            (k.kind == Kind::Face || k.kind == Kind::Shoulder || k.kind == Kind::Dpad || k.kind == Kind::Journal) == picture) {
            known = &k;
        }
    }
    // GOW3_PROMPTS_TEST_HASH=<hash hex>:face|shoulder:<GOW3_PROMPT_* index> (tests of the picture
    // upload path): that BC1 image counts as a button picture.
    static const KnownTexture test_texture = [] {
        KnownTexture k{0, Kind::Face, GOW3_PROMPT_TRIANGLE, "test picture"};
        if (const char* env = std::getenv("GOW3_PROMPTS_TEST_HASH")) {
            char kind[16] = {};
            unsigned long long h = 0;
            int button = GOW3_PROMPT_TRIANGLE;
            if (std::sscanf(env, "%llx:%15[a-z]:%d", &h, kind, &button) >= 1) {
                k.hash = h;
                k.kind = std::strcmp(kind, "shoulder") == 0 ? Kind::Shoulder : Kind::Face;
                k.button = std::clamp(button, 0, int(GOW3_PROMPT_COUNT) - 1);
            }
        }
        return k;
    }();
    if (!known && picture && test_texture.hash && test_texture.hash == hash) {
        known = &test_texture;
    }
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    if (log && state.logged.insert(address ^ hash).second) {
        std::printf("Prompts: %ux%u %s image (tile mode %u) at 0x%llx, hash %016llx%s%s\n", width, height,
                    bc1 ? "BC1" : "RGBA8", tile_mode, static_cast<unsigned long long>(address),
                    static_cast<unsigned long long>(hash), known ? ": " : "", known ? known->name : "");
        std::fflush(stdout);
    }
    if (!known) {
        return nullptr;
    }
    if (std::find(state.seen.begin(), state.seen.end(), std::pair{address, guest_size}) == state.seen.end()) {
        state.seen.emplace_back(address, guest_size);
    }
    const int set = SetOf(device.load(std::memory_order_relaxed));
    if (set < 0 || !state.canvas[set]) {
        return nullptr;
    }
    if (known->kind == Kind::Dpad && set != 0) {
        return nullptr; // other controllers' d-pads look like the PlayStation one
    }
    std::string label;
    if (picture) {
        label = state.labels[set][known->button];
        if (label.empty() && known->kind == Kind::Journal) {
            label = "TAB";
        }
        if (label.empty()) {
            return nullptr;
        }
    }
    auto& output = state.outputs[set][hash];
    if (!output.texels || output.generation != state.generation[set]) {
        std::shared_ptr<const Texels> texels;
        if (picture) {
            texels = MakePicture(*known, guest, int(width), int(height), levels, label, set == 1);
        } else {
            auto atlas_texels = std::make_shared<Texels>();
            atlas_texels->data = Compose(*state.canvas[set], guest, known->kind);
            atlas_texels->mips.push_back({0, u32(Size), u32(Size)});
            texels = std::move(atlas_texels);
        }
        output = {state.generation[set], std::move(texels)};
        std::printf("Prompts: %s icons in the %s\n", set == 0 ? "keyboard and mouse" : "controller", known->name);
        std::fflush(stdout);
    }
    return output.texels;
}

} // namespace Gow3Prompts

extern "C" void gow3gpu_prompts_set_labels(int set, const char* const* labels, int count) {
    using namespace Gow3Prompts;
    if (set < 0 || set >= Sets) {
        return;
    }
    Labels list(GOW3_PROMPT_COUNT);
    for (int i = 0; i < std::min(count, int(GOW3_PROMPT_COUNT)); ++i) {
        list[i] = labels[i] ? labels[i] : "";
    }
    auto canvas = Render(list);
    {
        auto& state = GetState();
        std::scoped_lock lock{state.mutex};
        state.canvas[set] = std::move(canvas);
        state.labels[set] = list;
        ++state.generation[set];
    }
    if (SetOf(device.load()) == set) {
        InvalidateSeen();
    }
}

extern "C" void gow3gpu_prompts_set_device(int next) {
    using namespace Gow3Prompts;
    if (device.exchange(next) == next) {
        return;
    }
    static const char* const names[] = {"PlayStation", "keyboard and mouse", "controller"};
    std::printf("Prompts: %s icons\n", names[std::clamp(next, 0, 2)]);
    std::fflush(stdout);
    InvalidateSeen();
}

#ifdef GOW3_PROMPTS_TEST
// Picture check without the game: prompts-test face|shoulder LABEL CONTROLLER(0|1) IN OUT SIZE
// IN: the picture's first mip (1D-tiled BC1, as in the pak); OUT: the redrawn mip 0 as raw
// RGBA (stored upside down, as in the game).
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    ".global gow3_font_ttf\n"
    "gow3_font_ttf:\n"
    ".incbin \"" GOW3_FONT_PATH "\"\n"
    ".text\n");

int main(int argc, char** argv) {
    using namespace Gow3Prompts;
    if (argc < 7) {
        return 2;
    }
    const int size = std::atoi(argv[6]);
    FILE* in = std::fopen(argv[4], "rb");
    if (!in) {
        return 1;
    }
    std::vector<u8> guest(size_t(size) * size / 2);
    const size_t got = std::fread(guest.data(), 1, guest.size(), in);
    std::fclose(in);
    if (got != guest.size()) {
        return 1;
    }
    const std::string kind_name = argv[1];
    const KnownTexture known{0,
                             kind_name == "face"   ? Kind::Face
                             : kind_name == "dpad" ? Kind::Dpad
                                                   : Kind::Shoulder,
                             0, "test"};
    const auto texels = MakePicture(known, guest.data(), size, size, 9, argv[2], argv[3][0] == '1');
    // Mip 0 back from linear BC1 blocks.
    std::vector<u8> rgba(size_t(size) * size * 4);
    const int nb = size / 4;
    for (int by = 0; by < nb; ++by) {
        for (int bx = 0; bx < nb; ++bx) {
            const u8* blk = &texels->data[(by * nb + bx) * 8];
            const u32 c0 = blk[0] | blk[1] << 8, c1 = blk[2] | blk[3] << 8;
            const Rgb a = Decode565(c0), b = Decode565(c1);
            Rgb pal[4] = {a, b, c0 > c1 ? Mix(a, b, 1.0f / 3) : Mix(a, b, 0.5f), Mix(a, b, 2.0f / 3)};
            const u32 bits = blk[4] | blk[5] << 8 | blk[6] << 16 | u32(blk[7]) << 24;
            for (int k = 0; k < 16; ++k) {
                const u32 i = (bits >> (2 * k)) & 3;
                u8* o = &rgba[((by * 4 + k / 4) * size + bx * 4 + k % 4) * 4];
                o[0] = u8(pal[i].r * 255), o[1] = u8(pal[i].g * 255), o[2] = u8(pal[i].b * 255);
                o[3] = c0 <= c1 && i == 3 ? 0 : 255;
            }
        }
    }
    FILE* out = std::fopen(argv[5], "wb");
    std::fwrite(rgba.data(), 1, rgba.size(), out);
    std::fclose(out);
    std::printf("%zu bytes, %zu mips\n", texels->data.size(), texels->mips.size());
    return 0;
}
#endif
