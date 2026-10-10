#pragma once
// The CPU sampler reference (ROADMAP M2 criterion 3): what a GPU returns when a
// fragment shader samples an RGBA8 texture at mip level 0, by the rules D3D12 and
// WebGPU share.
//   nearest: the texel at floor(u * width), floor(v * height)
//   linear:  the four texels around (u * width - 0.5, v * height - 0.5), blended
//   clamp:   texel indices clamped to [0, size - 1]; repeat: wrapped modulo size
// The result is in [0, 1] per channel, before the render target rounds it to 8 bits.
// The GPU identity check (src/renderer/gpu/texture_identity.cpp) compares a render
// against it, with the bounds in evidence/m2-rhi-texture-bounds.json.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
namespace raw::sampler {

enum class Filter : uint8_t { Nearest, Linear };
enum class Address : uint8_t { Clamp, Repeat };
struct Image { int width{0}, height{0}; std::span<const uint8_t> rgba; };   // rows top-down, tight

inline int wrap(int i, int n, Address a) {
    if (a == Address::Clamp) return i < 0 ? 0 : i >= n ? n - 1 : i;
    const int m = i % n;
    return m < 0 ? m + n : m;
}
inline std::array<double, 4> texel(const Image& im, int x, int y) {
    const uint8_t* p = im.rgba.data() + (size_t(y) * im.width + x) * 4;
    return {p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, p[3] / 255.0};
}
inline std::array<double, 4> sample(const Image& im, double u, double v, Filter f, Address a) {
    if (f == Filter::Nearest) {
        // Repeat applies to the coordinate before the floor: u in [1, 2) reads the same texels as [0, 1).
        return texel(im, wrap(int(std::floor(u * im.width)), im.width, a), wrap(int(std::floor(v * im.height)), im.height, a));
    }
    const double x = u * im.width - 0.5, y = v * im.height - 0.5, x0 = std::floor(x), y0 = std::floor(y), fx = x - x0, fy = y - y0;
    const int ix = int(x0), iy = int(y0);
    const auto t00 = texel(im, wrap(ix, im.width, a), wrap(iy, im.height, a)), t10 = texel(im, wrap(ix + 1, im.width, a), wrap(iy, im.height, a));
    const auto t01 = texel(im, wrap(ix, im.width, a), wrap(iy + 1, im.height, a)), t11 = texel(im, wrap(ix + 1, im.width, a), wrap(iy + 1, im.height, a));
    std::array<double, 4> o{};
    for (int c = 0; c < 4; ++c) o[c] = (t00[c] * (1 - fx) + t10[c] * fx) * (1 - fy) + (t01[c] * (1 - fx) + t11[c] * fx) * fy;
    return o;
}
// Distance, in texels, from the sample point to the nearest texel edge on either axis:
// near zero, nearest filtering has two right answers within float precision.
inline double edgeDistance(const Image& im, double u, double v) {
    const double x = u * im.width, y = v * im.height;
    return std::min(std::fabs(x - std::round(x)), std::fabs(y - std::round(y)));
}
inline uint8_t unorm8(double c) { return uint8_t(std::lround(std::clamp(c, 0.0, 1.0) * 255.0)); }

}  // namespace raw::sampler
