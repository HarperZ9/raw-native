// The dither module: see raw/renderer/dither.hpp.
#include "raw/renderer/dither.hpp"
#include "raw/renderer/noise.hpp"
#include <algorithm>
#include <cmath>
namespace raw::dither {
namespace {
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
// Nearest of `levels` evenly spaced levels.
float quant(float v, int levels) { const float s = float(levels - 1); return std::floor(clamp01(v) * s + 0.5f) / s; }
// Ordered: the level below v plus one step when the fractional part passes the threshold t in (0, 1).
float ordered(float v, float t, int levels) {
    const float s = float(levels - 1), x = clamp01(v) * s, lo = std::floor(x);
    return (lo + (x - lo > t ? 1.0f : 0.0f)) / s;
}
struct Tap { int dx, dy; float w; };
std::vector<float> diffuse(const std::vector<float>& g, int w, int h, int levels, const Tap* taps, int n, float denom) {
    std::vector<float> e(g.begin(), g.end()), out(g.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + x;
        const float q = quant(e[i], levels), err = e[i] - q;
        out[i] = q;
        for (int k = 0; k < n; ++k) {
            const int xx = x + taps[k].dx, yy = y + taps[k].dy;
            if (xx < 0 || xx >= w || yy >= h) continue;
            e[std::size_t(yy) * w + xx] += err * taps[k].w / denom;
        }
    }
    return out;
}
}  // namespace

const char* name(Method m) {
    switch (m) {
    case Method::Bayer2: return "bayer2"; case Method::Bayer4: return "bayer4"; case Method::Bayer8: return "bayer8";
    case Method::FloydSteinberg: return "floyd_steinberg"; case Method::Atkinson: return "atkinson"; case Method::Stucki: return "stucki";
    case Method::BlueNoise: return "blue_noise"; case Method::LineFill: return "line_fill"; default: return "contour_fill";
    }
}

int bayerRank(int n, int x, int y) {
    // The low bits of (x xor y, y) become the high bits of the rank: [[0, 2], [3, 1]] at n = 2.
    int k = 0;
    for (int m = n; m > 1; m >>= 1) ++k;
    int r = 0;
    for (int b = 0; b < k; ++b) r |= ((((x ^ y) >> b) & 1) << (2 * (k - 1 - b) + 1)) | (((y >> b) & 1) << (2 * (k - 1 - b)));
    return r;
}

std::vector<float> apply(Method m, const std::vector<float>& g, int w, int h, int levels, int period, int bands) {
    levels = std::max(2, levels);
    std::vector<float> out(g.size());
    const auto each = [&](auto f) { for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { const std::size_t i = std::size_t(y) * w + x; out[i] = f(g[i], x, y); } };
    switch (m) {
    case Method::Bayer2: case Method::Bayer4: case Method::Bayer8: {
        const int n = m == Method::Bayer2 ? 2 : (m == Method::Bayer4 ? 4 : 8);
        each([&](float v, int x, int y) { return ordered(v, (float(bayerRank(n, x % n, y % n)) + 0.5f) / float(n * n), levels); });
        return out;
    }
    case Method::BlueNoise:
        each([&](float v, int x, int y) { return ordered(v, noise::blue(x, y), levels); });
        return out;
    case Method::FloydSteinberg: { static const Tap t[] = {{1, 0, 7}, {-1, 1, 3}, {0, 1, 5}, {1, 1, 1}}; return diffuse(g, w, h, levels, t, 4, 16.0f); }
    case Method::Atkinson: { static const Tap t[] = {{1, 0, 1}, {2, 0, 1}, {-1, 1, 1}, {0, 1, 1}, {1, 1, 1}, {0, 2, 1}}; return diffuse(g, w, h, levels, t, 6, 8.0f); }
    case Method::Stucki: {
        static const Tap t[] = {{1, 0, 8}, {2, 0, 4}, {-2, 1, 2}, {-1, 1, 4}, {0, 1, 8}, {1, 1, 4}, {2, 1, 2}, {-2, 2, 1}, {-1, 2, 2}, {0, 2, 4}, {1, 2, 2}, {2, 2, 1}};
        return diffuse(g, w, h, levels, t, 12, 42.0f);
    }
    case Method::LineFill: {
        const int p = std::max(2, period);
        // A pixel lies on a dark stroke when its phase along the 45-degree diagonal falls in the
        // dark share (1 - tone) of the period: the period's mean equals the tone.
        each([&](float v, int x, int y) { const int ph = ((x + y) % p + p) % p; return (float(ph) + 0.5f) / float(p) < 1.0f - clamp01(v) ? 0.0f : 1.0f; });
        return out;
    }
    default: {
        const int b = std::max(2, bands);
        const auto band = [&](float v) { return std::min(b - 1, int(clamp01(v) * float(b))); };
        each([&](float v, int x, int y) {
            const int k = band(v);
            const bool edge = (x + 1 < w && band(g[std::size_t(y) * w + x + 1]) != k) || (y + 1 < h && band(g[std::size_t(y + 1) * w + x]) != k);
            if (edge) return 0.0f;
            return levels > 2 ? quant((float(k) + 0.5f) / float(b), levels) : 1.0f;   // two levels: line art on white paper
        });
        return out;
    }
    }
}

}  // namespace raw::dither
