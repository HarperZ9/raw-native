// Cube maps, the procedural test sky, the GGX prefilter and spherical harmonics:
// see raw/renderer/lighting.hpp. src/renderer/gpu/shaders/light.wgsl mirrors the cube
// addressing and the prefilter.
#include "raw/renderer/lighting.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
namespace raw::lighting {

std::size_t Cube::levelOffset(int level) const {
    std::size_t off = 0;
    for (int k = 0; k < level; ++k) { const std::size_t n = std::size_t(cube::levelSize(size, k)); off += 6 * n * n * 3; }
    return off;
}
Rgb Cube::texel(int level, int face, int y, int x) const {
    const int n = cube::levelSize(size, level);
    const std::size_t i = levelOffset(level) + ((std::size_t(face) * n + y) * n + x) * 3;
    return {rgb[i], rgb[i + 1], rgb[i + 2]};
}
Rgb Cube::sample(int level, D3 dir) const {
    const int n = cube::levelSize(size, level);
    double u, v;
    const int f = cube::faceOf(dir, u, v);
    const double fx = u * n - 0.5, fy = v * n - 0.5;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const double ax = fx - x0, ay = fy - y0;
    const auto at = [&](int y, int x) { int ff = f; cube::tap(n, ff, y, x); return texel(level, ff, y, x); };
    const Rgb a = at(y0, x0), b = at(y0, x0 + 1), c = at(y0 + 1, x0), d = at(y0 + 1, x0 + 1);
    const auto mix = [&](double p, double q, double r, double s) { return (p * (1 - ax) + q * ax) * (1 - ay) + (r * (1 - ax) + s * ax) * ay; };
    return {mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b)};
}
Rgb Cube::sampleRough(double r, D3 dir) const {
    const double l = std::clamp(r, 0.0, 1.0) * (levels - 1);
    const int l0 = std::min(int(l), levels - 1), l1 = std::min(l0 + 1, levels - 1);
    const double f = l - l0;
    const Rgb a = sample(l0, dir), b = sample(l1, dir);
    return {a.r * (1 - f) + b.r * f, a.g * (1 - f) + b.g * f, a.b * (1 - f) + b.b * f};
}

Rgb proceduralSky(D3 d) {
    const D3 sun = cube::sunDirection();
    const double cs = d.x * sun.x + d.y * sun.y + d.z * sun.z;
    if (cs >= std::cos(cube::kSunRadius)) return {1.0e5, 0.95e5, 0.85e5};
    if (d.y < 0.0) return {240.0, 200.0, 160.0};
    const double t = std::sqrt(d.y);
    return {1800.0 * (1 - t) + 1000.0 * t, 1900.0 * (1 - t) + 1800.0 * t, 2000.0 * (1 - t) + 3600.0 * t};
}

Cube cubeFrom(Rgb (*radiance)(D3), int size, int levels) {
    Cube c; c.size = size; c.levels = levels;
    c.rgb.assign(c.levelOffset(levels), 0.0);
    for (int f = 0; f < 6; ++f) for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const Rgb v = radiance(cube::dirOf(f, (x + 0.5) / size, (y + 0.5) / size));
        const std::size_t i = ((std::size_t(f) * size + y) * size + x) * 3;
        c.rgb[i] = v.r; c.rgb[i + 1] = v.g; c.rgb[i + 2] = v.b;
    }
    return c;
}

void prefilter(Cube& c, int samples) {
    for (int k = 1; k < c.levels; ++k) {
        const int n = cube::levelSize(c.size, k);
        const double a = pbr::alphaOf(double(k) / (c.levels - 1));
        const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
        parallelRows(6 * n, threads, [&](int row) {
            const int f = row / n, y = row % n;
            for (int x = 0; x < n; ++x) {
                const Rgb v = cube::prefilterTexel(c, cube::dirOf(f, (x + 0.5) / n, (y + 0.5) / n), a, samples);
                const std::size_t i = c.levelOffset(k) + ((std::size_t(f) * n + y) * n + x) * 3;
                c.rgb[i] = v.r; c.rgb[i + 1] = v.g; c.rgb[i + 2] = v.b;
            }
        });
    }
}

std::vector<Rgb> shProject(const Cube& c) {
    std::vector<Rgb> sh(9);
    const int n = c.size;
    for (int f = 0; f < 6; ++f) for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
        // The texel's exact solid angle: the corner function atan(xy / sqrt(x^2 + y^2 + 1)).
        const auto F = [](double a, double b) { return std::atan2(a * b, std::sqrt(a * a + b * b + 1.0)); };
        const double x0 = 2.0 * x / n - 1.0, x1 = 2.0 * (x + 1) / n - 1.0, y0 = 2.0 * y / n - 1.0, y1 = 2.0 * (y + 1) / n - 1.0;
        const double dw = F(x1, y1) - F(x0, y1) - F(x1, y0) + F(x0, y0);
        const D3 d = cube::dirOf(f, (x + 0.5) / n, (y + 0.5) / n);
        const Rgb L = c.texel(0, f, y, x);
        double Y[9];
        cube::shBasis(d, Y);
        for (int k = 0; k < 9; ++k) sh[k] = {sh[k].r + L.r * Y[k] * dw, sh[k].g + L.g * Y[k] * dw, sh[k].b + L.b * Y[k] * dw};
    }
    return sh;
}
Rgb shIrradiance(const std::vector<Rgb>& sh, D3 n) {
    double Y[9];
    cube::shBasis(n, Y);
    const double band[9] = {1.0, 2.0 / 3, 2.0 / 3, 2.0 / 3, 0.25, 0.25, 0.25, 0.25, 0.25};
    Rgb e;
    for (int k = 0; k < 9; ++k) { const double w = band[k] * Y[k]; e = {e.r + sh[k].r * w, e.g + sh[k].g * w, e.b + sh[k].b * w}; }
    return {std::max(e.r, 0.0), std::max(e.g, 0.0), std::max(e.b, 0.0)};
}

}  // namespace raw::lighting
