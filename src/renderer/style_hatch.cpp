// Curvature hatching, float64 reference: see raw/renderer/style_hatch.hpp. Operations are in the
// order of web/shaders/hatch/hatch.mjs and web/shaders/fixtures/stilllife.mjs.
#include "raw/renderer/style_hatch.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::style {
namespace {
V3 add(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 mul(const V3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double len(const V3& a) { return std::sqrt(dot(a, a)); }
V3 norm(const V3& a) { return mul(a, 1 / len(a)); }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double len3(double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); }

// STILL of stilllife.mjs.
constexpr V3 kSphereC{-0.9, 0.55, 0.2}; constexpr double kSphereR = 0.55;
constexpr V3 kTorusC{0.75, 0.18, 0.45}; constexpr double kTorusR = 0.5, kTorusr = 0.18;
constexpr V3 kCapA{0.05, 0.25, -0.95}, kCapB{0.05, 1.55, -0.95}; constexpr double kCapR = 0.25;
constexpr V3 kBoxC{1.65, 0.3, -0.6}, kBoxB{0.22, 0.22, 0.22}; constexpr double kBoxR = 0.08;
constexpr V3 kEye{3.2, 2.3, 3.7}, kTarget{0.25, 0.5, -0.15}, kLight{-0.5, 0.8, 0.45};
constexpr double kTan = 0.42, kE = 0.01;
constexpr V3 kSlant{0.9578262852211514, 0, 0.28734788556634544}, kSlant2{-0.28734788556634544, 0, 0.9578262852211514};
constexpr V3 kCross{0.6246950475544243, 0.4685212856658182, -0.6246950475544243};

double sdSphere(const V3& p) { return len3(p[0] - kSphereC[0], p[1] - kSphereC[1], p[2] - kSphereC[2]) - kSphereR; }
double sdTorus(const V3& p) {
    const double dx = p[0] - kTorusC[0], dz = p[2] - kTorusC[2];
    const double qx = std::sqrt(dx * dx + dz * dz) - kTorusR, qy = p[1] - kTorusC[1];
    return std::sqrt(qx * qx + qy * qy) - kTorusr;
}
double sdCapsule(const V3& p) {
    const V3 pa = sub(p, kCapA), ba = sub(kCapB, kCapA);
    const double h = std::min(1.0, std::max(0.0, dot(pa, ba) / dot(ba, ba)));
    return len3(pa[0] - ba[0] * h, pa[1] - ba[1] * h, pa[2] - ba[2] * h) - kCapR;
}
double sdRoundBox(const V3& p) {
    const V3 q{std::abs(p[0] - kBoxC[0]) - kBoxB[0], std::abs(p[1] - kBoxC[1]) - kBoxB[1], std::abs(p[2] - kBoxC[2]) - kBoxB[2]};
    return len3(std::max(q[0], 0.0), std::max(q[1], 0.0), std::max(q[2], 0.0)) + std::min(std::max({q[0], q[1], q[2]}), 0.0) - kBoxR;
}
double march(const V3& ro, const V3& rd) {
    double t = 0;
    for (int i = 0; i < 160; ++i) {
        const double d = stillSdf(add(ro, mul(rd, t)));
        if (d < 1e-4 * (1 + t)) return t;
        t += d;
        if (t > 30) break;
    }
    return -1;
}
double lineCov(double s, double freq, double halfW, double px) {
    const double u = s * freq, fr = u - std::floor(u), dist = std::min(fr, 1 - fr) / freq;
    return std::max(0.0, std::min(1.0, (halfW - dist) / px + 0.5)) * std::min(1.0, (2 * halfW) / px);
}
double hatchLines(double s, double freq, double px, double halfW) {
    const double lev = std::max(0.0, std::log2((px * freq) / 0.25)), l0 = std::floor(lev), t = lev - l0, f0 = freq / std::pow(2.0, l0);
    return lineCov(s, f0, halfW, px) * (1 - t) + lineCov(s, f0 / 2, halfW, px) * t;
}
}  // namespace

HatchPlan hatchPreset(const std::string& name) {
    HatchPlan p;
    if (name == "engraving") { p.freq = 34; p.width = 0.55; p.layers = 1; p.contour = 0; p.ink = {0.12, 0.1, 0.11}; p.paper = {0.95, 0.93, 0.88}; return p; }
    if (name == "crosshatch") { p.freq = 26; p.width = 0.6; p.layers = 2; p.contour = 0; p.ink = {0.1, 0.09, 0.12}; p.paper = {0.96, 0.94, 0.9}; return p; }
    if (name == "ink") { p.freq = 22; p.width = 0.8; p.layers = 2; p.contour = 0.25; p.ink = {0.05, 0.05, 0.07}; p.paper = {0.93, 0.9, 0.84}; return p; }
    throw std::invalid_argument("hatch: unknown preset " + name);
}

double stillSdf(const V3& p) { return std::min({p[1], sdSphere(p), sdTorus(p), sdCapsule(p), sdRoundBox(p)}); }

Curvature curvature(const V3& p, double umb) {
    const V3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const double f0 = stillSdf(p);
    V3 g;
    for (std::size_t i = 0; i < 3; ++i) g[i] = (stillSdf(add(p, mul(ax[i], kE))) - stillSdf(sub(p, mul(ax[i], kE)))) / (2 * kE);
    const double gl = len(g);
    const V3 n = mul(g, 1 / gl);
    double H[3][3] = {};
    for (std::size_t i = 0; i < 3; ++i) H[i][i] = (stillSdf(add(p, mul(ax[i], kE))) - 2 * f0 + stillSdf(sub(p, mul(ax[i], kE)))) / (kE * kE);
    for (std::size_t i = 0; i < 3; ++i) for (std::size_t j = i + 1; j < 3; ++j) {
        const V3 ei = mul(ax[i], kE), ej = mul(ax[j], kE);
        H[i][j] = H[j][i] = (stillSdf(add(add(p, ei), ej)) - stillSdf(sub(add(p, ei), ej)) - stillSdf(add(sub(p, ei), ej)) + stillSdf(sub(sub(p, ei), ej))) / (4 * kE * kE);
    }
    const V3 t1 = norm(cross(n, std::abs(n[1]) < 0.9 ? V3{0, 1, 0} : V3{1, 0, 0})), t2 = cross(n, t1);
    auto Hv = [&H](const V3& v) { return V3{H[0][0] * v[0] + H[0][1] * v[1] + H[0][2] * v[2], H[1][0] * v[0] + H[1][1] * v[1] + H[1][2] * v[2],
                                            H[2][0] * v[0] + H[2][1] * v[1] + H[2][2] * v[2]}; };
    const double a = dot(t1, Hv(t1)) / gl, b = dot(t1, Hv(t2)) / gl, d = dot(t2, Hv(t2)) / gl;
    const double m = 0.5 * (a + d), r = std::sqrt(0.25 * ((a - d) * (a - d)) + b * b), k1 = m + r, k2 = m - r;
    const double kmax = std::abs(k1) >= std::abs(k2) ? k1 : k2;
    double ex = b, ey = kmax - a;
    if (std::abs(kmax - d) > std::abs(kmax - a)) { ex = kmax - d; ey = b; }
    const double el = std::hypot(ex, ey);
    Curvature c;
    c.n = n;
    c.umbilic = r < umb * std::max({std::abs(k1), std::abs(k2), 0.05}) || el < 1e-12;
    c.dir = el > 0 ? norm(add(mul(t1, ex / el), mul(t2, ey / el))) : t1;
    if (c.umbilic) { const V3 up = sub({0, 1, 0}, mul(n, n[1])); c.dir = len(up) > 1e-6 ? norm(up) : t1; }
    c.other = cross(n, c.dir);
    return c;
}

HatchFrame runHatch(const HatchPlan& p, int w, int h) {
    HatchFrame out{std::vector<double>(std::size_t(w) * h * 4, 0.0), std::vector<std::uint8_t>(std::size_t(w) * h, 0), std::vector<std::uint8_t>(std::size_t(w) * h, 0)};
    const V3 L = norm(kLight), fwd = norm(sub(kTarget, kEye)), right = norm({-fwd[2], 0, fwd[0]}), up = cross(right, fwd);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + std::size_t(x);
        const double sx = ((x + 0.5) / w - 0.5) * 2 * (double(w) / h) * kTan, sy = (0.5 - (y + 0.5) / h) * 2 * kTan;
        const V3 rd = norm(add(add(fwd, mul(right, sx)), mul(up, sy)));
        const double t = march(kEye, rd);
        V3 col = p.paper;
        if (t > 0) {
            const V3 q = add(kEye, mul(rd, t));
            const Curvature cv = curvature(q, p.umb);
            const double tone = p.toneFloor + 0.88 * std::max(0.0, dot(cv.n, L));
            out.hit[i] = 1; out.fallback[i] = cv.umbilic ? 1 : 0;
            const double px = (t * 2 * kTan) / h, halfW = 0.5 * p.width * px, dark = 1 - tone, second = dot(q, kCross);
            double s1 = dot(q, cv.other), s2 = second;
            if (cv.umbilic && std::abs(cv.n[1]) > 0.95) { s1 = dot(q, kSlant); s2 = dot(q, kSlant2); }
            else if (cv.umbilic) s1 = q[1];
            double ink = hatchLines(s1, p.freq, px, halfW * (0.4 + 1.6 * dark));
            if (p.layers > 1 && tone < 0.5) ink = 1 - (1 - ink) * (1 - hatchLines(s2, p.freq, px, halfW * (0.4 + 2 * (0.5 - tone))));
            if (p.contour > 0) {
                const double nv = std::abs(dot(cv.n, rd));
                if (nv < p.contour) ink = std::max(ink, std::min(1.0, (p.contour - nv) / (0.5 * p.contour)));
            }
            for (std::size_t k = 0; k < 3; ++k) col[k] = p.paper[k] * (1 - ink) + p.ink[k] * ink;
        }
        for (std::size_t k = 0; k < 3; ++k) out.rgba[i * 4 + k] = col[k];
        out.rgba[i * 4 + 3] = 1;
    }
    return out;
}

}  // namespace raw::style
