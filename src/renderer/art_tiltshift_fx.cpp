// P23's lens and print effects: see raw/renderer/art_tiltshift.hpp. The GPU twin is art_ts_bright,
// art_ts_glow and art_ts_finish in art_tiltshift.wgsl.
#include "raw/renderer/art_tiltshift.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
constexpr double kPi = 3.141592653589793;
using C3 = std::array<double, 3>;
C3 bilinear(const std::vector<float>& a, int stride, int off, int w, int h, double fx, double fy) {
    // fx, fy in pixel-centre coordinates (pixel x's centre is x); clamped at the edges
    fx = std::clamp(fx, 0.0, double(w - 1)); fy = std::clamp(fy, 0.0, double(h - 1));
    const int x0 = std::min(int(std::floor(fx)), w - 1), y0 = std::min(int(std::floor(fy)), h - 1), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const double tx = fx - x0, ty = fy - y0;
    C3 o{};
    for (int c = 0; c < 3; ++c) {
        auto v = [&](int x, int y) { return double(a[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * std::size_t(stride) + std::size_t(off + c)]); };
        o[std::size_t(c)] = (v(x0, y0) * (1 - tx) + v(x1, y0) * tx) * (1 - ty) + (v(x0, y1) * (1 - tx) + v(x1, y1) * tx) * ty;
    }
    return o;
}
double luma(const C3& c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
// The share of the aperture (unit circle) left by the barrel's circle (radius rb) at offset e.
double catEye(double e, double rb) {
    if (e <= rb - 1) return 1;
    if (e >= rb + 1) return 0;
    const double a1 = std::acos(std::clamp((e * e + 1 - rb * rb) / (2 * e), -1.0, 1.0)), a2 = std::acos(std::clamp((e * e + rb * rb - 1) / (2 * e * rb), -1.0, 1.0));
    const double k = std::sqrt(std::max(0.0, (-e + 1 + rb) * (e + 1 - rb) * (e - 1 + rb) * (e + 1 + rb)));
    return (a1 + rb * rb * a2 - 0.5 * k) / kPi;
}
}  // namespace

const std::vector<float>& haloTable() {
    static const std::vector<float> t = [] {
        std::vector<float> o;
        for (int i = 0; i < kHaloTaps; ++i) {
            const double r = std::sqrt((i + 0.5) / kHaloTaps), a = i * 2.399963229728653 + 0.5;
            o.push_back(float(r * std::cos(a))); o.push_back(float(r * std::sin(a)));
        }
        return o;
    }();
    return t;
}

FxPlan planFx(const TiltLook& look, const FxFrame& fr, int w, int h, double fpx) {
    FxPlan P;
    P.w = w; P.h = h; P.qw = (w + 3) / 4; P.qh = (h + 3) / 4; P.fpx = fpx; P.look = look; P.fr = fr;
    const double t = fr.frame / 24.0, s = h / 1080.0;
    if (!fr.reducedMotion && look.weave > 0) {
        P.weaveX = look.weave * s * (0.35 * std::sin(2 * kPi * 0.7 * t + 1.3) + 0.25 * std::sin(2 * kPi * 1.9 * t + 0.4));
        P.weaveY = look.weave * s * (0.30 * std::sin(2 * kPi * 0.9 * t + 2.1) + 0.20 * std::sin(2 * kPi * 2.3 * t));
    }
    P.seed = fr.reducedMotion ? 7u : std::uint32_t(fr.frame) * 7919u + 13u;
    P.haloR = 0.012 * h / 4.0; P.starStep = 0.06 * h / 4.0 / 16.0; P.cell = std::max(3.0, 6.0 * s);
    for (int j = 0; j < 4; ++j) { const double a = (look.starRotDeg + 45.0 * j) * kPi / 180; P.dirs[std::size_t(2 * j)] = float(std::cos(a)); P.dirs[std::size_t(2 * j + 1)] = float(std::sin(a)); }
    return P;
}

std::vector<float> fxBright(const FxPlan& P, const std::vector<float>& img) {
    std::vector<float> o(std::size_t(P.qw) * std::size_t(P.qh) * 4, 0.0f);
    for (int y = 0; y < P.qh; ++y) for (int x = 0; x < P.qw; ++x) {
        C3 c{};
        for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
            const std::size_t q = std::size_t(std::min(4 * y + j, P.h - 1)) * std::size_t(P.w) + std::size_t(std::min(4 * x + i, P.w - 1));
            for (int k = 0; k < 3; ++k) c[std::size_t(k)] += img[q * 4 + std::size_t(k)] / 16.0;
        }
        const double L = luma(c), e = L > P.thr ? (L - P.thr) * (L - P.thr) / (L - P.thr + P.knee) : 0.0;
        const double warm = std::clamp(3 * (c[0] - c[2]) / (c[0] + c[1] + c[2] + 1e-4), 0.0, 1.0), g = e / std::max(L, 1e-4) * warm;
        const std::size_t o4 = (std::size_t(y) * std::size_t(P.qw) + std::size_t(x)) * 4;
        for (int k = 0; k < 3; ++k) o[o4 + std::size_t(k)] = float(c[std::size_t(k)] * g);
    }
    return o;
}

std::vector<float> fxGlow(const FxPlan& P, const std::vector<float>& bright) {
    std::vector<float> o(std::size_t(P.qw) * std::size_t(P.qh) * 8, 0.0f);
    const std::vector<float>& ht = haloTable();
    for (int y = 0; y < P.qh; ++y) for (int x = 0; x < P.qw; ++x) {
        C3 halo{}, star{};
        for (int i = 0; i < kHaloTaps; ++i) {
            const C3 b = bilinear(bright, 4, 0, P.qw, P.qh, x + P.haloR * ht[std::size_t(2 * i)], y + P.haloR * ht[std::size_t(2 * i + 1)]);
            for (int k = 0; k < 3; ++k) halo[std::size_t(k)] += b[std::size_t(k)] / kHaloTaps;
        }
        for (int j = 0; j < 4; ++j) for (int sg = -1; sg <= 1; sg += 2) for (int m = 1; m <= 16; ++m) {
            const double f = 1 - m / 17.0, wgt = f * f / 8.0, d = sg * m * P.starStep;
            const C3 b = bilinear(bright, 4, 0, P.qw, P.qh, x + d * P.dirs[std::size_t(2 * j)], y + d * P.dirs[std::size_t(2 * j + 1)]);
            for (int k = 0; k < 3; ++k) star[std::size_t(k)] += wgt * b[std::size_t(k)];
        }
        const std::size_t o8 = (std::size_t(y) * std::size_t(P.qw) + std::size_t(x)) * 8;
        for (int k = 0; k < 3; ++k) { o[o8 + std::size_t(k)] = float(halo[std::size_t(k)]); o[o8 + 4 + std::size_t(k)] = float(star[std::size_t(k)]); }
    }
    return o;
}

std::vector<float> fxFinish(const FxPlan& P, const std::vector<float>& img, const std::vector<float>& glow) {
    const TiltLook& L = P.look;
    const int w = P.w, h = P.h;
    std::vector<float> o(std::size_t(w) * std::size_t(h) * 4, 1.0f);
    const double cx = 0.5 * w, cy = 0.5 * h, R2 = cx * cx + cy * cy, R = std::sqrt(R2), gain = P.fr.reduceFlashes ? 0.5 : 1.0;
    const double mx = L.misreg * h / 1080.0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const double px = x + 0.5, py = y + 0.5, dx = px - cx, dy = py - cy, rn2 = (dx * dx + dy * dy) / R2;
        const double sx = L.ca * (h / 1080.0) * rn2 * dx / R, sy = L.ca * (h / 1080.0) * rn2 * dy / R;
        const double bx = x + P.weaveX, by = y + P.weaveY;
        C3 c{bilinear(img, 4, 0, w, h, bx + sx + mx, by + sy + 0.5 * mx)[0], bilinear(img, 4, 0, w, h, bx, by)[1],
             bilinear(img, 4, 0, w, h, bx - sx - mx, by - sy - 0.3 * mx)[2]};
        const double gx = px / 4.0 - 0.5, gy = py / 4.0 - 0.5;
        const C3 halo = bilinear(glow, 8, 0, P.qw, P.qh, gx, gy), star = bilinear(glow, 8, 4, P.qw, P.qh, gx, gy);
        const C3 th{1.0, 0.55, 0.22}, ts{1.0, 0.82, 0.5}, lift{0.05, 0.04, 0.035}, paper{0.95, 0.90, 0.80};
        for (int k = 0; k < 3; ++k) {
            const std::size_t i = std::size_t(k);
            c[i] += gain * (L.halation * halo[i] * th[i] + L.star * star[i] * ts[i]);
            c[i] = c[i] + L.print * (lift[i] + (paper[i] - lift[i]) * c[i] - c[i]);
        }
        if (L.hueDeg != 0 || L.sat != 1) {
            // hue rotation about the grey axis, then saturation toward luminance
            const double hc = std::cos(L.hueDeg * kPi / 180), hs = std::sin(L.hueDeg * kPi / 180), k1 = (1 - hc) / 3, k2 = hs / std::sqrt(3.0);
            const C3 r{c[0] * (hc + k1) + c[1] * (k1 - k2) + c[2] * (k1 + k2), c[0] * (k1 + k2) + c[1] * (hc + k1) + c[2] * (k1 - k2),
                       c[0] * (k1 - k2) + c[1] * (k1 + k2) + c[2] * (hc + k1)};
            const double y0 = luma(r);
            for (int k = 0; k < 3; ++k) c[std::size_t(k)] = y0 + L.sat * (r[std::size_t(k)] - y0);
        }
        if (L.halftone > 0) {
            const double u = (px + py) * 0.7071067811865476 / P.cell, v = (py - px) * 0.7071067811865476 / P.cell;
            const double fu = u - std::floor(u) - 0.5, fv = v - std::floor(v) - 0.5, d = std::sqrt(fu * fu + fv * fv);
            const double rad = 0.7071067811865476 * std::sqrt(std::clamp(1 - luma(c), 0.0, 1.0)), ink = std::clamp((rad - d) * P.cell + 0.5, 0.0, 1.0);
            const C3 pp{0.90, 0.87, 0.80}, ik{0.08, 0.08, 0.10};
            for (int k = 0; k < 3; ++k) { const std::size_t i = std::size_t(k); c[i] += L.halftone * (pp[i] + (ik[i] - pp[i]) * ink - c[i]); }
        }
        const double tan2 = (dx * dx + dy * dy) / (P.fpx * P.fpx), vnat = 1 / ((1 + tan2) * (1 + tan2));
        const double vig = (1 + L.vignette * (vnat - 1)) * (1 + L.mech * (catEye(std::sqrt(rn2), 1.15) - 1));
        const double n = style::hash3(std::uint32_t(x), std::uint32_t(y), P.seed) - 0.5, lum = std::clamp(luma(c), 0.0, 1.0);
        for (int k = 0; k < 3; ++k) {
            const std::size_t i = std::size_t(k), q = (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + i;
            o[q] = float(std::max(0.0, c[i] * vig + L.grain * n * (0.25 + lum)));
        }
    }
    return o;
}

std::vector<float> fxStack(const FxPlan& P, const std::vector<float>& img) { return fxFinish(P, img, fxGlow(P, fxBright(P, img))); }

}  // namespace raw::art
