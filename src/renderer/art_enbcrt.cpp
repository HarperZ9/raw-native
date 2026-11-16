// The ENB port's CRT pipeline (Effect_CRTShader.fxh CRT_* and PS_CRTDisplay), presets and the
// RenderTarget128 downsample: see raw/renderer/art_enbcrt.hpp.
#include "art_enbcrt_internal.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::art::enbcrt {
namespace {
double fr(double x) { return x - std::floor(x); }
double tri(double phase, double centre) { return std::clamp(1.0 - std::abs(phase - centre) * 2.0, 0.0, 1.0); }
C3 mix(double a, const C3& m, double b) { return {a + (b - a) * m[0], a + (b - a) * m[1], a + (b - a) * m[2]}; }   // lerp(a, b, m)
C3 grille(double px, double intensity, double luma) {
    const double ph = fr(px / 3.0) * 3.0, ai = intensity * (1.0 - luma * 0.5);
    return mix(1.0 - ai, {tri(ph, 0.5), tri(ph, 1.5), tri(ph, 2.5)}, 1.0);
}
C3 slot(double px, double py, double intensity, double luma) {
    const double row = std::floor(py - 2.0 * std::floor(py / 2.0)) * 1.5, ph = fr((px + row) / 3.0) * 3.0;
    const double gap = smooth(0.4, 0.5, std::abs(fr(py / 2.0) - 0.5)), g = 1.0 + (0.7 - 1.0) * gap, ai = intensity * (1.0 - luma * 0.5);
    return mix(1.0 - ai * 0.8, {tri(ph, 0.5) * g, tri(ph, 1.5) * g, tri(ph, 2.5) * g}, 1.0);
}
C3 shadow(double px, double py, double intensity, double luma) {
    const double hx = px, hy = py * 0.866, row = std::floor(hy - 2.0 * std::floor(hy / 2.0)) * 0.5, ph = fr((hx + row) / 3.0) * 3.0;
    const double dx = fr(hx) - 0.5, dy = fr(hy) - 0.5, dot = smooth(0.45, 0.3, std::sqrt(dx * dx + dy * dy)), ai = intensity * (1.0 - luma * 0.5);
    return mix(1.0 - ai * 0.6, {ph < 1.0 ? dot : 0.0, (ph >= 1.0 && ph < 2.0) ? dot : 0.0, ph >= 2.0 ? dot : 0.0}, 1.0);
}
C3 profile(C3 c, int prof) {
    if (prof == 1) { c[0] *= 1.05; c[1] *= 1.02; c[2] *= 0.90; }
    else if (prof == 2) {
        c[0] *= 1.08; c[1] *= 1.04; c[2] *= 0.95;
        const double l = dot3(c, kLum);
        for (double& v : c) v = l + (v - l) * 1.15;
    } else if (prof == 3) { c[0] *= 0.92; c[1] *= 0.97; c[2] *= 1.12; }
    return c;
}
C3 bloom(const Image& s, double u, double v) {
    const double r = 1.5 / s.w, rv = 1.5 / s.h, dr = r * 0.7071, drv = rv * 0.7071;
    const double off[8][3] = {{r, 0, 1}, {-r, 0, 1}, {0, rv, 1}, {0, -rv, 1}, {dr, drv, 0.7}, {-dr, drv, 0.7}, {dr, -drv, 0.7}, {-dr, -drv, 0.7}};
    C3 b{0, 0, 0};
    for (const auto& o : off) { const C3 t = s.linear(u + o[0], v + o[1]); for (int k = 0; k < 3; ++k) b[k] += t[k] * o[2]; }
    for (double& x : b) x /= 6.8;
    return b;
}
C3 sample(const Ctx& c, double u, double v) {
    if (c.p.convergence <= 0.01) return c.src.linear(u, v);
    const double cx = u - 0.5, cy = v - 0.5, e = (cx * cx + cy * cy) * 4.0, ox = c.p.convergence / c.src.w * e, oy = c.p.convergence / c.src.h * e;
    return {c.src.linear(u + ox * -0.7, v + oy * -0.5)[0], c.src.linear(u, v)[1], c.src.linear(u + ox * 0.5, v + oy * 0.7)[2]};
}
C3 grade(const EnbCrtParams& p, C3 col, double& luma) {
    if (p.colourProfile > 0) col = profile(col, p.colourProfile);
    for (double& x : col) x = std::max((x * p.brightness - 0.5) * p.contrast + 0.5, 0.0);
    luma = dot3(col, kLum);
    for (double& x : col) x = std::max(luma + (x - luma) * p.saturation, 0.0);
    luma = dot3(col, kLum);
    return col;
}
}  // namespace

C3 crt(const Ctx& c, double u, double v) {
    const EnbCrtParams& p = c.p;
    if (p.curvature > 0.001) {
        const double x = u * 2 - 1, y = v * 2 - 1, k = 1.0 + (x * x + y * y) * p.curvature;
        u = x * k * 0.5 + 0.5; v = y * k * 0.5 + 0.5;
    }
    u = (u - 0.5) * (1.0 + p.overscan * 2.0) + 0.5; v = (v - 0.5) * (1.0 + p.overscan * 2.0) + 0.5;
    if (std::min(u * (1 - u), v * (1 - v)) < 0.0) return {0, 0, 0};
    double luma = 0;
    C3 col = grade(p, sample(c, u, v), luma);
    if (p.scanIntensity > 0.01) {
        const double d = fr(v * c.src.h / p.scanWidth) - 0.5, sig = 0.35 + (0.7 - 0.35) * (luma * p.beamWidth), shp = 2.0 + (6.0 - 2.0) * (luma * p.beamWidth);
        const double beam = std::exp(-0.5 * std::pow(std::abs(d) / std::max(sig, 0.001), shp)), m = 1.0 + (beam - 1.0) * p.scanIntensity;
        for (double& x : col) x *= m;
    }
    if (p.maskIntensity > 0.01) {
        const double px = u * c.src.w / p.maskScale, py = v * c.src.h / p.maskScale;
        const C3 m = p.maskType == 0 ? grille(px, p.maskIntensity, luma) : p.maskType == 1 ? slot(px, py, p.maskIntensity, luma) : shadow(px, py, p.maskIntensity, luma);
        for (int k = 0; k < 3; ++k) col[k] *= m[k];
    }
    if (p.bloom > 0.01) { const C3 b = bloom(c.src, u, v); for (int k = 0; k < 3; ++k) col[k] += (std::max(col[k], b[k]) - col[k]) * p.bloom; }
    if (p.halation > 0.01) {
        const C3 hal = c.down.linear(u, v), warm{1.15, 1.0, 0.85};
        for (int k = 0; k < 3; ++k) col[k] += hal[k] * warm[k] * p.halation * 0.3;
    }
    if (p.signalNoise > 0.01) {
        const double n = unitHash(std::uint32_t(c.x), std::uint32_t(c.y), std::uint32_t(c.frame * 100 / 3), 17) - 0.5;
        for (double& x : col) x += n * p.signalNoise * 0.15;
    }
    if (p.cornerRadius > 0.001) {
        const double e = smooth(0.0, p.cornerRadius, u) * smooth(0.0, p.cornerRadius, 1 - u) * smooth(0.0, p.cornerRadius, v) * smooth(0.0, p.cornerRadius, 1 - v);
        const double m = std::pow(e, 0.3);
        for (double& x : col) x *= m;
    }
    return sat(col);
}

}  // namespace raw::art::enbcrt

namespace raw::art {
using namespace enbcrt;

std::vector<float> enbCrtDown(const std::vector<float>& img, int w, int h) {
    std::vector<double> acc(128 * 128 * 4, 0.0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t t = (std::size_t(lineOf(y, h, 128)) * 128 + std::size_t(lineOf(x, w, 128))) * 4, s = (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4;
        for (int k = 0; k < 3; ++k) acc[t + std::size_t(k)] += img[s + std::size_t(k)];
        acc[t + 3] += 1.0;
    }
    std::vector<float> out(128 * 128 * 4, 0.0f);
    for (std::size_t t = 0; t < 128 * 128; ++t) {
        const double n = std::max(acc[t * 4 + 3], 1.0);
        for (int k = 0; k < 3; ++k) out[t * 4 + std::size_t(k)] = float(acc[t * 4 + std::size_t(k)] / n);
        out[t * 4 + 3] = 1.0f;
    }
    return out;
}

std::vector<float> runEnbCrt(const EnbCrtParams& p, const std::vector<float>& img, int w, int h, int frame) {
    const std::vector<float> down = enbCrtDown(img, w, h);
    std::vector<float> out(img.size(), 1.0f);
    parallelRows(h, 1, [&](int y) {
        for (int x = 0; x < w; ++x) {
            Ctx c{p, {&img, w, h}, {&down, 128, 128}, frame, x, y, frame / 30.0};
            double u = (x + 0.5) / w, v = (y + 0.5) / h;
            C3 col = c.src.linear(u, v);
            if (p.vhs) col = vhs(c, u, v);
            if (p.crt) col = crt(c, u, v);
            const std::size_t i = (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4;
            for (int k = 0; k < 3; ++k) out[i + std::size_t(k)] = float(col[std::size_t(k)]);
        }
    });
    return out;
}

EnbCrtParams enbCrtPreset(const std::string& name) {
    EnbCrtParams p;
    if (name == "off") return p;
    if (name == "crt") { p.crt = true; return p; }
    if (name == "crt-slot") { p.crt = true; p.maskType = 1; p.colourProfile = 1; p.convergence = 1.5; return p; }
    if (name == "crt-shadow") { p.crt = true; p.maskType = 2; p.colourProfile = 2; return p; }
    if (name == "vhs") { p.vhs = true; return p; }
    if (name == "vhs-crt") { p.vhs = true; p.crt = true; return p; }
    if (name == "vhs-ep") { p.vhs = true; p.crt = true; p.quality = 2; p.generation = 3; p.tracking = 0.6; return p; }
    if (name == "crt-zero") {
        p.crt = true; p.curvature = p.overscan = p.scanIntensity = p.maskIntensity = p.bloom = p.halation = p.cornerRadius = p.signalNoise = p.convergence = 0;
        p.brightness = p.contrast = p.saturation = 1;
        return p;
    }
    throw std::invalid_argument("enbcrt: unknown preset " + name);
}

}  // namespace raw::art
