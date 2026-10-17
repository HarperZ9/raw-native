// The painterly shader's presets, noise and advected canvas: see raw/renderer/style_paint.hpp.
// Operations are in the order of web/shaders/paint/paint.mjs, noise.mjs and canvas.mjs.
#include "raw/renderer/style_paint.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTau = 0.35, kRampFrames = 8, kMinAge = 12, kDepthTol = 0.02, kQuant = 4096;
double fade(double t) { return t * t * (3 - 2 * t); }
double paperHeight(double x, double y) {
    const double c1 = 0.9397, s1 = 0.342, c2 = -0.342, s2 = 0.9397;
    const double f1 = paintVnoise((x * c1 + y * s1) / 0.9, (-x * s1 + y * c1) / 7, 31), f2 = paintVnoise((x * c2 + y * s2) / 0.9, (-x * s2 + y * c2) / 7, 37);
    return 0.45 * paintVnoise(x / 2.2, y / 2.2, 11) + 0.25 * paintVnoise(x / 9, y / 9, 13) + 0.15 * f1 + 0.15 * f2;
}
double bilerp(const std::vector<float>& C, int w, int h, double x, double y, int ch) {
    const double fx = std::min(double(w - 1), std::max(0.0, x)), fy = std::min(double(h - 1), std::max(0.0, y));
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy)), x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
    const double tx = fx - x0, ty = fy - y0;
    auto at = [&](int xx, int yy) { return double(C[(std::size_t(yy) * w + std::size_t(xx)) * 4 + std::size_t(ch)]); };
    return (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
}
double scaleDeparture(double a, double b, double c, double d) {
    const double E = a * a + b * b + c * c + d * d, det = std::abs(a * d - b * c), disc = std::sqrt(std::max(0.0, E * E - 4 * det * det));
    const double s1 = std::sqrt(std::max(1e-12, (E + disc) / 2)), s2 = std::sqrt(std::max(1e-12, (E - disc) / 2));
    return std::abs(std::log(s1)) + std::abs(std::log(s2));
}
void distortion(PaintCanvas& st) {
    const int w = st.w, h = st.h;
    const auto& C = st.C;
    for (int y = 1; y < h - 1; ++y) for (int x = 1; x < w - 1; ++x) {
        const std::size_t p = std::size_t(y) * w + std::size_t(x), W = std::size_t(w);
        for (int k = 0; k < 2; ++k) {
            const std::size_t o = std::size_t(2 * k);
            const double a = (double(C[(p + 1) * 4 + o]) - C[(p - 1) * 4 + o]) / 2, b = (double(C[(p + W) * 4 + o]) - C[(p - W) * 4 + o]) / 2;
            const double c = (double(C[(p + 1) * 4 + o + 1]) - C[(p - 1) * 4 + o + 1]) / 2, d = (double(C[(p + W) * 4 + o + 1]) - C[(p - W) * 4 + o + 1]) / 2;
            const double D = scaleDeparture(a, b, c, d);
            if (D <= 1) { st.statSum[std::size_t(k)] += std::floor(D * kQuant); st.statN[std::size_t(k)]++; }
        }
    }
}
}  // namespace

PaintParams paintPreset(const std::string& name) {
    PaintParams p;
    if (name == "oil-grotesque") { p.medium = 0; p.radius = 7; p.q = 8; p.strokeLen = 16; p.impasto = 1.1; p.gloss = 0.5; p.push = 0.35; p.broken = 0.45; p.valueBands = 4; p.lines = 0.9; p.lineSigma = 1.3; p.lineSharp = 45; p.warp = 5; p.exposure = 2.6; return p; }
    if (name == "oil") return p;
    if (name == "gouache") { p.medium = 1; p.radius = 6; p.q = 10; p.strokeLen = 8; p.impasto = 0.25; p.gloss = 0; p.push = 0.1; p.lines = 0.35; p.warp = 0.8; p.broken = 0.1; p.valueBands = 5; return p; }
    if (name == "watercolour") { p.medium = 2; p.radius = 5; p.q = 6; p.strokeLen = 6; p.impasto = 0; p.gloss = 0; p.push = 0.08; p.lines = 0.15; p.lineSigma = 0.9; p.lineSharp = 25; p.warp = 0.6; p.exposure = 1.9; p.granulation = 0.35; p.edge = 1.6; p.dilution = 0.88; p.broken = 0.08; return p; }
    throw std::invalid_argument("paint: unknown preset " + name);
}
std::vector<float> paintGaussWeights(double sigma) {
    const int r = std::max(1, int(std::ceil(2.5 * sigma)));
    std::vector<float> w(static_cast<std::size_t>(2 * r + 1));
    double s = 0;
    for (int i = -r; i <= r; ++i) { const double q = i / sigma; w[std::size_t(i + r)] = float(std::exp(-0.5 * (q * q))); s += w[std::size_t(i + r)]; }
    for (float& v : w) v = float(double(v) / s);
    return w;
}
PaintPlan resolvePaint(const PaintParams& p, int w, int h) { PaintPlan plan; plan.p = p; plan.w = w; plan.h = h; plan.tw = paintGaussWeights(p.tensorSigma); return plan; }

double paintVnoise(double x, double y, std::uint32_t seed) {
    const double xi = std::floor(x), yi = std::floor(y), tx = fade(x - xi), ty = fade(y - yi);
    const std::uint32_t ix = std::uint32_t(std::int64_t(xi)), iy = std::uint32_t(std::int64_t(yi));
    const double a = hash3(ix, iy, seed), b = hash3(ix + 1u, iy, seed), c = hash3(ix, iy + 1u, seed), d = hash3(ix + 1u, iy + 1u, seed);
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}
double paintNoise(PaintNoise kind, double x, double y) {
    switch (kind) {
        case PaintNoise::Bristle: return 0.7 * paintVnoise(x / 1.3, y / 1.3, 53) + 0.3 * paintVnoise(x / 5, y / 5, 59);
        case PaintNoise::StrokeId: return paintVnoise(x / 6, y / 6, 67);
        case PaintNoise::Canvas: return 0.5 + 0.25 * std::sin((2 * kPi * x) / 3.2) * std::cos((2 * kPi * y) / 3.2) + 0.25 * (paintVnoise(x / 1.5, y / 1.5, 61) - 0.5);
        case PaintNoise::Paper: return paperHeight(x, y);
        case PaintNoise::PaperHalf: return paperHeight(x / 2, y / 2);
        case PaintNoise::WarpX: return 2 * paintVnoise(x / 23, y / 23, 71) - 1;
        default: return 2 * paintVnoise(x / 23, y / 23, 73) - 1;
    }
}

PaintCanvas::PaintCanvas(int w_, int h_) : w(w_), h(h_), C(std::size_t(w_) * h_ * 4, 0.0f), prevDist(std::size_t(w_) * h_, -1.0f) {}

void paintDecide(PaintCanvas& st) {
    auto& L = st.layer;
    for (int k = 0; k < 2; ++k) {
        L[std::size_t(k)].meanD = st.statN[std::size_t(k)] > 0 ? st.statSum[std::size_t(k)] / kQuant / st.statN[std::size_t(k)] : 0;
        if (L[std::size_t(k)].age >= 0) L[std::size_t(k)].age = std::min(1e6, L[std::size_t(k)].age + 1);
        L[std::size_t(k)].reset = false;
    }
    auto wake = [&](int k) {
        PaintLayer& l = L[std::size_t(k)];
        l.reset = true; l.age = 0; l.meanD = 0;
        l.seed = {hash3(std::uint32_t(st.frame), std::uint32_t(k), 101) * 997, hash3(std::uint32_t(st.frame), std::uint32_t(k), 103) * 997};
    };
    if (st.frame == 0) { wake(0); L[0].age = kRampFrames; }
    else for (int k = 0; k < 2; ++k) {
        const int o = 1 - k;
        if (L[std::size_t(k)].age >= 0 && L[std::size_t(k)].meanD > kTau && (L[std::size_t(o)].age < 0 || (L[std::size_t(o)].age >= kMinAge && L[std::size_t(o)].meanD > kTau * 0.5))) { wake(o); break; }
    }
    st.statSum = {0, 0}; st.statN = {0, 0};
}
std::array<double, 2> paintLayerWeights(const PaintCanvas& st) {
    std::array<double, 2> w{};
    for (int k = 0; k < 2; ++k) {
        const PaintLayer& l = st.layer[std::size_t(k)];
        w[std::size_t(k)] = l.age < 0 ? 0 : std::min(1.0, (l.age + 1) / kRampFrames) * std::max(0.02, 1 - l.meanD / kTau);
    }
    return w;
}
void paintAdvect(PaintCanvas& st, const std::vector<float>& mv, const std::vector<float>& dist, const std::vector<float>& distPrev) {
    const int w = st.w, h = st.h;
    std::vector<float> C(std::size_t(w) * h * 4, 0.0f);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t p = std::size_t(y) * w + std::size_t(x);
        const double px = x - double(mv[p * 2]), py = y - double(mv[p * 2 + 1]);
        const std::size_t q = std::size_t(std::min(h - 1, std::max(0, int(std::floor(py + 0.5))))) * w + std::size_t(std::min(w - 1, std::max(0, int(std::floor(px + 0.5)))));
        const bool inside = px >= 0 && py >= 0 && px <= w - 1 && py <= h - 1;
        const bool valid = inside && st.prevDist[q] > 0 && std::abs(double(st.prevDist[q]) - distPrev[p]) <= kDepthTol * distPrev[p];
        for (int k = 0; k < 2; ++k) {
            const PaintLayer& L = st.layer[std::size_t(k)];
            const bool fresh = L.reset || !valid;
            C[p * 4 + std::size_t(2 * k)] = float(fresh ? x + L.seed[0] : bilerp(st.C, w, h, px, py, 2 * k));
            C[p * 4 + std::size_t(2 * k + 1)] = float(fresh ? y + L.seed[1] : bilerp(st.C, w, h, px, py, 2 * k + 1));
        }
    }
    st.C = std::move(C); st.prevDist = dist; st.frame++;
    distortion(st);
}

std::uint8_t paintEncode8(double v) {
    const double l = std::min(1.0, std::max(0.0, v));
    return std::uint8_t(std::floor((l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055) * 255 + 0.5));
}

}  // namespace raw::style
