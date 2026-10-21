// The noise module: see raw/renderer/noise.hpp.
//   lowbias32: C. Wellons, "Prospecting for Hash Functions" (2018).
//   Perlin: K. Perlin, "Improving Noise", SIGGRAPH 2002 (quintic fade, 12 edge gradients).
//   Simplex: the skew F = (sqrt(n + 1) - 1) / n and unskew G = (1 - 1 / sqrt(n + 1)) / n, kernels
//   (r0^2 - d^2)^4, written from the definitions (S. Gustavson, "Simplex noise demystified", 2005).
//   Worley: S. Worley, "A Cellular Texture Basis Function", SIGGRAPH 1996 (one point a cell).
//   Ridged and hybrid multifractal: F. K. Musgrave, in Ebert et al., "Texturing and Modeling".
#include "raw/renderer/noise.hpp"
#include "data/bluenoise64.hpp"
#include <cmath>
namespace raw::noise {
namespace {
std::uint32_t lowbias32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }
float unit(std::uint32_t h) { return float(h >> 8) * (1.0f / 16777216.0f); }
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
std::int32_t fl(float v) { return static_cast<std::int32_t>(std::floor(v)); }
// 2D: eight directions (axes and diagonals, the diagonals unnormalised as in Perlin's 3D set).
float grad2(std::uint32_t h, float x, float y) {
    switch (h & 7u) {
    case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
    case 4: return x; case 5: return -x; case 6: return y; default: return -y;
    }
}
// 3D: the twelve cube-edge directions, h mod 12 via the top bits.
float grad3(std::uint32_t h, float x, float y, float z) {
    switch ((h >> 4) % 12u) {
    case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
    case 4: return x + z; case 5: return -x + z; case 6: return x - z; case 7: return -x - z;
    case 8: return y + z; case 9: return -y + z; case 10: return y - z; default: return -y - z;
    }
}
}  // namespace

std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t seed) {
    return lowbias32(x ^ lowbias32(y ^ lowbias32(z ^ lowbias32(seed))));
}
float white(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) { return unit(hash(u(x), u(y), u(z), seed)); }

float value2(float x, float y, std::uint32_t seed) {
    const std::int32_t i = fl(x), j = fl(y);
    const float fx = fade(x - float(i)), fy = fade(y - float(j));
    const float a = white(i, j, 0, seed), b = white(i + 1, j, 0, seed), c = white(i, j + 1, 0, seed), d = white(i + 1, j + 1, 0, seed);
    return lerp(lerp(a, b, fx), lerp(c, d, fx), fy);
}
float value3(float x, float y, float z, std::uint32_t seed) {
    const std::int32_t i = fl(x), j = fl(y), k = fl(z);
    const float fx = fade(x - float(i)), fy = fade(y - float(j)), fz = fade(z - float(k));
    const auto w = [&](int a, int b, int c) { return white(i + a, j + b, k + c, seed); };
    return lerp(lerp(lerp(w(0, 0, 0), w(1, 0, 0), fx), lerp(w(0, 1, 0), w(1, 1, 0), fx), fy),
                lerp(lerp(w(0, 0, 1), w(1, 0, 1), fx), lerp(w(0, 1, 1), w(1, 1, 1), fx), fy), fz);
}
float perlin2(float x, float y, std::uint32_t seed) {
    const std::int32_t i = fl(x), j = fl(y);
    const float rx = x - float(i), ry = y - float(j), fx = fade(rx), fy = fade(ry);
    const auto g = [&](int a, int b) { return grad2(hash(u(i + a), u(j + b), 0u, seed), rx - float(a), ry - float(b)); };
    return lerp(lerp(g(0, 0), g(1, 0), fx), lerp(g(0, 1), g(1, 1), fx), fy) * 0.70710678f;   // the diagonal gradients reach sqrt 2
}
float perlin3(float x, float y, float z, std::uint32_t seed) {
    const std::int32_t i = fl(x), j = fl(y), k = fl(z);
    const float rx = x - float(i), ry = y - float(j), rz = z - float(k), fx = fade(rx), fy = fade(ry), fz = fade(rz);
    const auto g = [&](int a, int b, int c) { return grad3(hash(u(i + a), u(j + b), u(k + c), seed), rx - float(a), ry - float(b), rz - float(c)); };
    return lerp(lerp(lerp(g(0, 0, 0), g(1, 0, 0), fx), lerp(g(0, 1, 0), g(1, 1, 0), fx), fy),
                lerp(lerp(g(0, 0, 1), g(1, 0, 1), fx), lerp(g(0, 1, 1), g(1, 1, 1), fx), fy), fz);
}
float simplex2(float x, float y, std::uint32_t seed) {
    const float F2 = 0.36602540378f, G2 = 0.21132486541f;   // (sqrt 3 - 1) / 2, (3 - sqrt 3) / 6
    const float s = (x + y) * F2;
    const std::int32_t i = fl(x + s), j = fl(y + s);
    const float t = float(i + j) * G2, x0 = x - (float(i) - t), y0 = y - (float(j) - t);
    const std::int32_t i1 = x0 > y0 ? 1 : 0, j1 = 1 - i1;
    const float xs[3] = {x0, x0 - float(i1) + G2, x0 - 1.0f + 2.0f * G2}, ys[3] = {y0, y0 - float(j1) + G2, y0 - 1.0f + 2.0f * G2};
    const std::int32_t di[3] = {0, i1, 1}, dj[3] = {0, j1, 1};
    float n = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float r = 0.5f - xs[c] * xs[c] - ys[c] * ys[c];
        if (r <= 0.0f) continue;
        r *= r;
        n += r * r * grad2(hash(u(i + di[c]), u(j + dj[c]), 0u, seed), xs[c], ys[c]);
    }
    return 70.0f * n * 0.70710678f;
}
float simplex3(float x, float y, float z, std::uint32_t seed) {
    const float F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
    const float s = (x + y + z) * F3;
    const std::int32_t i = fl(x + s), j = fl(y + s), k = fl(z + s);
    const float t = float(i + j + k) * G3, x0 = x - (float(i) - t), y0 = y - (float(j) - t), z0 = z - (float(k) - t);
    std::int32_t a[3], b[3];   // the second and third corners' offsets, by the order of x0, y0, z0
    if (x0 >= y0) {
        if (y0 >= z0) { a[0] = 1; a[1] = 0; a[2] = 0; b[0] = 1; b[1] = 1; b[2] = 0; }
        else if (x0 >= z0) { a[0] = 1; a[1] = 0; a[2] = 0; b[0] = 1; b[1] = 0; b[2] = 1; }
        else { a[0] = 0; a[1] = 0; a[2] = 1; b[0] = 1; b[1] = 0; b[2] = 1; }
    } else {
        if (y0 < z0) { a[0] = 0; a[1] = 0; a[2] = 1; b[0] = 0; b[1] = 1; b[2] = 1; }
        else if (x0 < z0) { a[0] = 0; a[1] = 1; a[2] = 0; b[0] = 0; b[1] = 1; b[2] = 1; }
        else { a[0] = 0; a[1] = 1; a[2] = 0; b[0] = 1; b[1] = 1; b[2] = 0; }
    }
    const std::int32_t off[4][3] = {{0, 0, 0}, {a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {1, 1, 1}};
    float n = 0.0f;
    for (int c = 0; c < 4; ++c) {
        const float g = G3 * float(c);
        const float px = x0 - float(off[c][0]) + g, py = y0 - float(off[c][1]) + g, pz = z0 - float(off[c][2]) + g;
        float r = 0.6f - px * px - py * py - pz * pz;
        if (r <= 0.0f) continue;
        r *= r;
        n += r * r * grad3(hash(u(i + off[c][0]), u(j + off[c][1]), u(k + off[c][2]), seed), px, py, pz);
    }
    return 32.0f * n;
}
Cell worley2(float x, float y, std::uint32_t seed) {
    const std::int32_t i = fl(x), j = fl(y);
    float d1 = 1e30f, d2 = 1e30f;
    for (int b = -1; b <= 1; ++b) for (int a = -1; a <= 1; ++a) {
        const std::uint32_t h = hash(u(i + a), u(j + b), 0x5eedu, seed);
        const float px = float(i + a) + unit(h), py = float(j + b) + unit(lowbias32(h)), dx = px - x, dy = py - y, d = dx * dx + dy * dy;
        if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
    }
    return {std::sqrt(d1), std::sqrt(d2)};
}
Cell worley3(float x, float y, float z, std::uint32_t seed) {
    const std::int32_t i = fl(x), j = fl(y), k = fl(z);
    float d1 = 1e30f, d2 = 1e30f;
    for (int c = -1; c <= 1; ++c) for (int b = -1; b <= 1; ++b) for (int a = -1; a <= 1; ++a) {
        const std::uint32_t h = hash(u(i + a), u(j + b), u(k + c), seed ^ 0x5eedu), h2 = lowbias32(h);
        const float px = float(i + a) + unit(h), py = float(j + b) + unit(h2), pz = float(k + c) + unit(lowbias32(h2));
        const float dx = px - x, dy = py - y, dz = pz - z, d = dx * dx + dy * dy + dz * dz;
        if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
    }
    return {std::sqrt(d1), std::sqrt(d2)};
}

float base3(Base b, float x, float y, float z, std::uint32_t seed) {
    switch (b) {
    case Base::Perlin: return perlin3(x, y, z, seed);
    case Base::Simplex: return simplex3(x, y, z, seed);
    default: return value3(x, y, z, seed) * 2.0f - 1.0f;
    }
}
float fbm(Base b, float x, float y, float z, std::uint32_t seed, Octaves o) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f, f = 1.0f;
    for (int k = 0; k < o.count; ++k) {
        sum += amp * base3(b, x * f, y * f, z * f, seed + std::uint32_t(k));
        norm += amp; amp *= o.gain; f *= o.lacunarity;
    }
    return sum / norm;
}
float ridged(Base b, float x, float y, float z, std::uint32_t seed, Octaves o) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f, f = 1.0f, weight = 1.0f;
    for (int k = 0; k < o.count; ++k) {
        float s = 1.0f - std::fabs(base3(b, x * f, y * f, z * f, seed + std::uint32_t(k)));
        s = s * s * weight;
        weight = s * 2.0f > 1.0f ? 1.0f : (s * 2.0f < 0.0f ? 0.0f : s * 2.0f);
        sum += amp * s; norm += amp; amp *= o.gain; f *= o.lacunarity;
    }
    return sum / norm;
}
float billow(Base b, float x, float y, float z, std::uint32_t seed, Octaves o) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f, f = 1.0f;
    for (int k = 0; k < o.count; ++k) {
        sum += amp * (2.0f * std::fabs(base3(b, x * f, y * f, z * f, seed + std::uint32_t(k))) - 1.0f);
        norm += amp; amp *= o.gain; f *= o.lacunarity;
    }
    return sum / norm;
}
float hybrid(Base b, float x, float y, float z, std::uint32_t seed, Octaves o, float offset) {
    float amp = 1.0f, f = 1.0f;
    float result = base3(b, x, y, z, seed) + offset, weight = result;
    for (int k = 1; k < o.count; ++k) {
        amp *= o.gain; f *= o.lacunarity;
        if (weight > 1.0f) weight = 1.0f;
        const float signal = (base3(b, x * f, y * f, z * f, seed + std::uint32_t(k)) + offset) * amp;
        result += weight * signal;
        weight *= signal;
    }
    return result < 0.0f ? 0.0f : result;
}
Warped warp(float x, float y, float z, std::uint32_t seed, float strength, Octaves o) {
    const float wx = fbm(Base::Simplex, x + 1.7f, y + 9.2f, z + 3.1f, seed + 101u, o);
    const float wy = fbm(Base::Simplex, x + 8.3f, y + 2.8f, z + 6.5f, seed + 202u, o);
    const float wz = fbm(Base::Simplex, x + 4.6f, y + 7.4f, z + 0.9f, seed + 303u, o);
    return {x + strength * wx, y + strength * wy, z + strength * wz};
}
float blue(std::int32_t x, std::int32_t y) {
    const std::uint32_t xi = u(x) & 63u, yi = u(y) & 63u;
    return (float(style::data::kBlueNoise64[yi * 64u + xi]) + 0.5f) * (1.0f / 4096.0f);
}

}  // namespace raw::noise
