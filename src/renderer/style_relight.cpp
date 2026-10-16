// Pigment-space lighting and interference glazes, float64 references: see
// raw/renderer/style_relight.hpp. Operations are in the order of web/shaders/lightpaint/lightpaint.mjs
// and web/shaders/glaze/glaze.mjs.
#include "raw/renderer/style_relight.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace raw::style {
namespace {
double lum(const LightpaintPlan& p, const V3& c) { return 0.2126 * c[0] + p.lumG * c[1] + 0.0722 * c[2]; }
V3 complementOf(const V3& lc) {
    const double mx = std::max({lc[0], lc[1], lc[2]}), mn = std::min({lc[0], lc[1], lc[2]});
    const V3 q{mx + mn - lc[0], mx + mn - lc[1], mx + mn - lc[2]};
    const double m = std::max({q[0], q[1], q[2], 1e-9});
    return {(0.3 * q[0]) / m, (0.3 * q[1]) / m, (0.3 * q[2]) / m};
}
V3 at(const std::vector<float>& a, std::size_t i) { return {a[i * 4], a[i * 4 + 1], a[i * 4 + 2]}; }
void fogMix(std::vector<double>& out, std::size_t i, const V3& c, double f, const V3& fog) {
    for (std::size_t k = 0; k < 3; ++k) out[i * 4 + k] = c[k] * (1 - f) + fog[k] * f;
    out[i * 4 + 3] = 1;
}
}  // namespace

LightpaintPlan lightpaintPreset(const std::string& name) {
    LightpaintPlan p;
    if (name == "disco") { p.shadow = {0, 0, 0.45, 0.55}; p.light = {0.8, 0.2, 0, 0}; p.Emid = 0.35; p.t0 = 0.5; p.strength = 0.5; p.shape = 1.5; return p; }
    if (name == "grotesque") { p.shadow = {0, 0.15, 0.6, 0.25}; p.light = {0.5, 0.45, 0, 0.05}; p.Emid = 0.3; p.t0 = 0.55; p.strength = 1; p.shape = 0.7; return p; }
    if (name == "complement") { p.shadow = {0, 0, 0, 1}; p.light = {0.85, 0.15, 0, 0}; p.Emid = 0.35; p.t0 = 0.5; p.strength = 0.6; p.shape = 1.2; p.complement = true; return p; }
    throw std::invalid_argument("lightpaint: unknown preset " + name);
}

V3 lightpaintShade(const LightpaintPlan& p, const V3& alb, const V3& E) {
    const double e = lum(p, E);
    if (!(e > 1e-12)) return {0, 0, 0};
    const V3 lc{E[0] / e, E[1] / e, E[2] / e};
    const double t = e / (e + p.Emid);
    const double ws = t < p.t0 ? p.strength * std::pow((p.t0 - t) / p.t0, p.shape) : 0;
    const double wl = t > p.t0 ? p.strength * std::pow((t - p.t0) / (1 - p.t0), p.shape) : 0;
    const Latent a = latentOf(alb);
    V4 cs = p.shadow;
    if (p.complement) { const V3 q = complementOf(lc); cs = lutConc({linearToSrgb(q[0]), linearToSrgb(q[1]), linearToSrgb(q[2])}); }
    const double wa = 1 - ws - wl;
    V4 c;
    for (std::size_t k = 0; k < 4; ++k) c[k] = wa * a.c[k] + ws * cs[k] + wl * p.light[k];
    const V3 d = kmDecode(c);
    V3 paint;
    for (std::size_t k = 0; k < 3; ++k) paint[k] = std::min(1.0, std::max(0.0, d[k] + wa * a.r[k]));
    const V3 tint{paint[0] * lc[0], paint[1] * lc[1], paint[2] * lc[2]};
    const double yt = lum(p, tint), target = lum(p, {alb[0] * E[0], alb[1] * E[1], alb[2] * E[2]});
    if (!(yt > 1e-9)) return {alb[0] * E[0], alb[1] * E[1], alb[2] * E[2]};
    const double s = target / yt;
    return {tint[0] * s, tint[1] * s, tint[2] * s};
}

std::vector<double> runLightpaint(const LightpaintPlan& p, const LitFrame& f) {
    const std::size_t n = std::size_t(f.w) * f.h;
    std::vector<double> out(n * 4, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const V3 E = at(f.light, i);
        const V3 c = f.kind[i] == 2 ? lightpaintShade(p, at(f.albedo, i), E) : E;
        fogMix(out, i, c, f.fogT[i], f.fogColor);
    }
    return out;
}

GlazePlan glazePreset(const std::string& name) {
    GlazePlan p;
    if (name == "beetle") { p.d0 = 330; p.dVar = 0; p.noiseScale = 0.4; p.coverage = 0.7; return p; }
    if (name == "oil-slick") { p.d0 = 140; p.dVar = 220; p.noiseScale = 0.45; p.coverage = 0.55; return p; }
    if (name == "bruise") { p.d0 = 230; p.dVar = 60; p.noiseScale = 0.18; p.coverage = 0.65; return p; }
    throw std::invalid_argument("glaze: unknown preset " + name);
}

double filmReflectance(const GlazePlan& p, double lam, double cosAir, double d) {
    const double l = lam / 1000;
    const double n0 = p.n0, n1 = p.dispersion ? p.A + p.B / (l * l) : p.filmN, n2 = p.n2, sa = std::sqrt(std::max(0.0, 1 - cosAir * cosAir));
    const double s0 = sa / n0, c0 = std::sqrt(1 - s0 * s0), s1 = sa / n1, c1 = std::sqrt(1 - s1 * s1), s2 = sa / n2, c2 = std::sqrt(1 - s2 * s2);
    const double cd = std::cos((4 * std::numbers::pi * n1 * d * c1) / lam);
    auto airy = [cd](double r01, double r12) { return (r01 * r01 + r12 * r12 + 2 * r01 * r12 * cd) / (1 + r01 * r01 * r12 * r12 + 2 * r01 * r12 * cd); };
    const double rs = airy((n0 * c0 - n1 * c1) / (n0 * c0 + n1 * c1), (n1 * c1 - n2 * c2) / (n1 * c1 + n2 * c2));
    const double rp = airy((n1 * c0 - n0 * c1) / (n1 * c0 + n0 * c1), (n2 * c1 - n1 * c2) / (n2 * c1 + n1 * c2));
    return 0.5 * (rs + rp);
}

namespace {
double vnoise3(double x, double y, double z, std::uint32_t seed) {
    const double xi = std::floor(x), yi = std::floor(y), zi = std::floor(z), u = fade5(x - xi), v = fade5(y - yi), w = fade5(z - zi);
    auto h = [seed](double i, double j, double k) {
        return hash3(std::uint32_t(std::int64_t(i) + 4096), std::uint32_t(std::int64_t(j) + 4096), std::uint32_t((std::int64_t(k) + 4096) * 131 + seed));
    };
    auto lx = [&](double j, double k) { return h(xi, j, k) * (1 - u) + h(xi + 1, j, k) * u; };
    auto ly = [&](double k) { return lx(yi, k) * (1 - v) + lx(yi + 1, k) * v; };
    return ly(zi) * (1 - w) + ly(zi + 1) * w;
}
}  // namespace
double glazeThickness(const GlazePlan& p, const V3& q) {
    const double s = p.noiseScale, s2 = 0.37 * p.noiseScale;
    return p.d0 + p.dVar * ((0.65 * vnoise3(q[0] / s, q[1] / s, q[2] / s, 17) + 0.35 * vnoise3(q[0] / s2, q[1] / s2, q[2] / s2, 29)) - 0.5) * 2;
}
V3 glazeColour(const GlazePlan& p, const V3& alb, double cosAir, double d) {
    const Latent a = latentOf(alb);
    V3 out = alb;
    for (int w = 0; w < kWavelengths; ++w) {
        const double Rb = bodyReflectance(a.c, w);
        const double Rf = p.coverage * filmReflectance(p, 400 + 10 * w, cosAir, d), Rt = Rf + ((1 - Rf) * (1 - Rf) * Rb) / (1 - Rf * Rb);
        for (int k = 0; k < 3; ++k) out[std::size_t(k)] += tableW(w, k) * (Rt - Rb);
    }
    return out;
}

std::vector<double> runGlaze(const GlazePlan& p, const LitFrame& f) {
    const LitCamera& cam = f.camera;
    std::vector<double> out(std::size_t(f.w) * f.h * 4, 0.0);
    for (int y = 0; y < f.h; ++y) for (int x = 0; x < f.w; ++x) {
        const std::size_t i = std::size_t(y) * f.w + std::size_t(x);
        const V3 E = at(f.light, i);
        V3 c = E;
        if (f.kind[i] == 2) {
            const double sx = ((x + 0.5) / f.w - 0.5) * 2 * cam.aspect * cam.tan, sy = (0.5 - (y + 0.5) / f.h) * 2 * cam.tan;
            V3 r;
            for (std::size_t k = 0; k < 3; ++k) r[k] = cam.fwd[k] + cam.right[k] * sx + cam.up[k] * sy;
            const V3 n = at(f.normals, i);
            const double rl = std::hypot(r[0], r[1], r[2]), cosAir = std::min(1.0, std::abs(n[0] * r[0] + n[1] * r[1] + n[2] * r[2]) / rl);
            const double t = f.depth[i] / rl;
            const V3 q{cam.eye[0] + r[0] * t, cam.eye[1] + r[1] * t, cam.eye[2] + r[2] * t};
            const V3 g = glazeColour(p, at(f.albedo, i), cosAir, glazeThickness(p, q));
            c = {g[0] * E[0], g[1] * E[1], g[2] * E[2]};
        }
        fogMix(out, i, c, f.fogT[i], f.fogColor);
    }
    return out;
}

}  // namespace raw::style
