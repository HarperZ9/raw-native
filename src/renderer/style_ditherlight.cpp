// Dither-quantised lighting, float64 reference: see raw/renderer/style_ditherlight.hpp.
// Operations are in the order of web/shaders/ditherlight/*.mjs so the two references agree to
// float64 rounding (the CPU link of evidence/cpp-port-style-bounds.json).
#include "raw/renderer/style_ditherlight.hpp"
#include "data/bluenoise64.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace raw::style {
namespace {
double sat(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
double smooth(double a, double b, double x) { const double t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }
double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 normalised(const V3& a) { const double l = std::hypot(a[0], a[1], a[2]); return {a[0] / l, a[1] / l, a[2] / l}; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double linearToSrgb(double l) { l = std::clamp(l, 0.0, 1.0); return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055; }
}  // namespace

double pixelAngle(double fov, int height) { return (2 * std::tan(fov / 2)) / height; }

DitherPlan ditherPreset(const std::string& name, int height, double fov) {
    DitherPlan p;
    if (name == "ink") p.levels = 2;
    else if (name == "tones4") p.levels = 4;
    else throw std::invalid_argument("ditherlight: unknown preset " + name);
    p.pa = pixelAngle(fov, height);
    return p;
}

DitherLight makeRadial(V3 pos, double radius, double core, double intensity, double wrap) {
    DitherLight L;
    L.kind = DitherLight::Kind::Radial; L.pos = pos; L.radius = radius; L.core = core; L.intensity = intensity; L.wrap = wrap;
    return L;
}
DitherLight makeBeam(V3 pos, V3 dir, double outerDeg, double innerDeg, double range, double intensity, double slats, double duty,
                     double phase, double wrap) {
    DitherLight L;
    L.kind = DitherLight::Kind::Beam; L.pos = pos;
    L.dir = normalised(dir);
    const V3 up = std::abs(L.dir[1]) < 0.95 ? V3{0, 1, 0} : V3{1, 0, 0};
    L.tan = normalised(cross(L.dir, up));
    L.bit = cross(L.dir, L.tan);
    L.outer = std::cos(outerDeg * std::numbers::pi / 180); L.inner = std::cos(innerDeg * std::numbers::pi / 180);
    L.range = range; L.intensity = intensity; L.slats = slats; L.duty = duty; L.phase = phase; L.wrap = wrap;
    return L;
}

int bayer8Rank(int x, int y) {
    x &= 7; y &= 7;
    const int v = x ^ y;
    return ((v & 1) << 5) | ((y & 1) << 4) | (((v >> 1) & 1) << 3) | (((y >> 1) & 1) << 2) | (((v >> 2) & 1) << 1) | ((y >> 2) & 1);
}
int blueRank(int x, int y) { return data::kBlueNoise64[((y & 63) << 6) | (x & 63)]; }
double thresholdAt(Source s, int cx, int cy) {
    return s == Source::Bayer8 ? (bayer8Rank(cx, cy) + 0.5) / 64 : (blueRank(cx, cy) + 0.5) / 4096;
}
int octaveOf(double cell, double px, double pa, double depth) {
    const double r = (px * pa * std::max(depth, 1e-6)) / cell;
    return r <= 1 ? 0 : int(std::ceil(std::log2(r)));
}

double maskAt(const DitherLight& L, const V3& P) {
    const V3 v{P[0] - L.pos[0], P[1] - L.pos[1], P[2] - L.pos[2]};
    const double r2 = dot(v, v);
    if (L.kind == DitherLight::Kind::Radial) {
        const double q = r2 / (L.radius * L.radius);
        return q >= 1 ? 0 : (L.intensity * (1 - q) * (1 - q)) / (1 + r2 / (L.core * L.core));
    }
    const double r = std::sqrt(r2);
    if (r < 1e-4 || r >= L.range) return 0;
    const double c = dot(v, L.dir) / r, cone = smooth(L.outer, L.inner, c);
    if (cone <= 0) return 0;
    double blade = 1;
    if (L.slats > 0) {
        const double phi = std::atan2(dot(v, L.bit), dot(v, L.tan)), c0 = std::cos(std::numbers::pi * L.duty);
        blade = smooth(c0 - 0.06, c0 + 0.06, std::cos(L.slats * phi + L.phase));
    }
    return L.intensity * cone * blade * (1 - r / L.range);
}
double surfaceTerm(const DitherLight& L, const V3& P, const V3& N) {
    const V3 v{L.pos[0] - P[0], L.pos[1] - P[1], L.pos[2] - P[2]};
    const double l = std::hypot(v[0], v[1], v[2]);
    if (l < 1e-6) return 1;
    return sat((dot(N, v) / l + L.wrap) / (1 + L.wrap));
}

double levelLinear(const DitherPlan& p, int k) { return std::pow(double(k) / (p.levels - 1), p.gamma); }
std::array<std::uint8_t, 3> levelRgb8(const DitherPlan& p, int k) {
    const double l = levelLinear(p, k);
    std::array<std::uint8_t, 3> o{};
    for (int c = 0; c < 3; ++c) o[std::size_t(c)] = std::uint8_t(std::floor(linearToSrgb(p.ink[std::size_t(c)] + (p.paper[std::size_t(c)] - p.ink[std::size_t(c)]) * l) * 255 + 0.5));
    return o;
}

double toneOf(const DitherPlan& p, const std::vector<DitherLight>& lights, const DitherPixel& px) {
    double Y = px.albedo;
    if (px.kind == 2) {
        double E = p.dark * (1 - p.hemi + p.hemi * (p.hemiConst + 0.5 * px.nrm[1]));
        for (std::size_t i = 0; i < lights.size() && i < 4; ++i) {
            const double v = px.vis[i];
            if (!(v > 0)) continue;
            const double m = maskAt(lights[i], px.wpos);
            if (m > 0) E += v * m * surfaceTerm(lights[i], px.wpos, px.nrm);
        }
        const double lit = std::min(1.0, std::max(0.0, px.albedo * E)), r = px.depth / p.fogDist, tf = p.fogMax * (1 - std::exp(-r * r));
        Y = lit * (1 - tf) + tf;
    }
    // in-scattered light along the view ray, screen-blended
    const double hz = px.haze > 0 ? std::min(1.0, px.haze) : 0;
    return hz > 0 ? 1 - (1 - Y) * (1 - hz) : Y;
}

std::array<int, 2> anchorCell(const DitherPlan& p, const DitherPixel& px) {
    if (p.anchor == Anchor::Screen) return {px.ix, px.iy};
    const V3& n = px.anrm;
    const double ax = std::abs(n[0]), ay = std::abs(n[1]), az = std::abs(n[2]);
    double u, v;
    if (ax >= ay && ax >= az) { u = px.apos[2]; v = px.apos[1]; }
    else if (ay >= az) { u = px.apos[0]; v = px.apos[2]; }
    else { u = px.apos[0]; v = px.apos[1]; }
    double size = p.cell;
    if (p.anchor == Anchor::WorldOctave) size *= std::pow(2.0, octaveOf(p.cell, p.px, p.pa, px.depth));
    return {int(std::floor(u / size)), int(std::floor(v / size))};
}

int quantise(const DitherPlan& p, double Y, double t, double* fraction) {
    const int N = p.levels;
    const double y = std::min(1.0, std::max(0.0, Y));
    int k = int(std::floor(std::pow(y, 1 / p.gamma) * (N - 1)));
    if (k > N - 2) k = N - 2;
    const double lo = levelLinear(p, k), hi = levelLinear(p, k + 1), f = (y - lo) / (hi - lo);
    if (fraction) *fraction = f;
    return f > t ? k + 1 : k;
}

DitherPixel DitherGBuffer::pixel(int x, int y) const {
    const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
    DitherPixel px;
    px.kind = kind[i]; px.albedo = albedo[i]; px.depth = depth[i]; px.haze = haze.empty() ? 0.0 : double(haze[i]); px.ix = x; px.iy = y;
    for (std::size_t c = 0; c < 3; ++c) { px.wpos[c] = wpos[i * 3 + c]; px.nrm[c] = nrm[i * 3 + c]; px.apos[c] = apos[i * 3 + c]; px.anrm[c] = anrm[i * 3 + c]; }
    for (std::size_t c = 0; c < 4; ++c) px.vis[c] = vis[i * 4 + c];
    return px;
}

DitherFrame runDitherlight(const DitherPlan& p, const std::vector<DitherLight>& lights, const DitherGBuffer& g, int cellShift) {
    if (p.levels < 2) throw std::invalid_argument("ditherlight: levels is an integer >= 2");
    DitherFrame f;
    f.w = g.w; f.h = g.h;
    const std::size_t n = std::size_t(g.w) * std::size_t(g.h);
    f.level.resize(n); f.rgba.resize(n * 4); f.tone.resize(n); f.fraction.resize(n); f.threshold.resize(n);
    std::vector<std::array<std::uint8_t, 3>> table;
    for (int k = 0; k < p.levels; ++k) table.push_back(levelRgb8(p, k));
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(g.w) + std::size_t(x);
            const DitherPixel px = g.pixel(x, y);
            const double Y = toneOf(p, lights, px);
            const std::array<int, 2> c = anchorCell(p, px);
            const double t = thresholdAt(p.source, c[0] + cellShift, c[1]);
            const int k = quantise(p, Y, t, &f.fraction[i]);
            f.tone[i] = Y; f.threshold[i] = t; f.level[i] = std::uint8_t(k);
            for (std::size_t q = 0; q < 3; ++q) f.rgba[i * 4 + q] = table[std::size_t(k)][q];
            f.rgba[i * 4 + 3] = 255;
        }
    return f;
}

}  // namespace raw::style
