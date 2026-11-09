// P23's painted defocus: see raw/renderer/art_tiltshift.hpp. The GPU twin is art_ts_weight,
// art_ts_prep, art_ts_tensor, art_ts_blur and art_ts_defocus in art_tiltshift.wgsl.
#include "raw/renderer/art_tiltshift.hpp"
#include "raw/core/parallel.hpp"
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_paint.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
constexpr double kPi = 3.141592653589793;
double smooth(double a, double b, double x) { const double t = std::clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t); }
std::size_t clampIdx(int x, int y, int w, int h) { return std::size_t(std::clamp(y, 0, h - 1)) * std::size_t(w) + std::size_t(std::clamp(x, 0, w - 1)); }
// The flow of a structure tensor (E, F, G): the unit direction along edges and the anisotropy.
std::array<double, 3> flowOf(double E, double F, double G) {
    const double tr = E + G, dsc = std::sqrt((E - G) * (E - G) + 4 * F * F), l1 = 0.5 * (tr + dsc), l2 = 0.5 * (tr - dsc);
    double gx = F, gy = l1 - E;
    if (gx * gx + gy * gy < (l1 - G) * (l1 - G) + F * F) { gx = l1 - G; gy = F; }
    const double m = std::sqrt(gx * gx + gy * gy);
    if (m <= 1e-12 || tr <= 1e-12) return {1.0, 0.0, 0.0};
    return {-gy / m, gx / m, (l1 - l2) / (l1 + l2)};
}
}  // namespace

const DefocusTables& defocusTables() {
    static const DefocusTables t = [] {
        DefocusTables o;
        const double zeta = 0.33, eta = (zeta + std::cos(kPi / 8)) / std::pow(std::sin(kPi / 8), 2);
        for (int i = 0; i < kTsSamples; ++i) {
            const double r = std::sqrt((i + 0.5) / kTsSamples), a = i * 2.399963229728653;
            const float u = float(r * std::cos(a)), v = float(r * std::sin(a));
            o.pos.push_back(u); o.pos.push_back(v);
            const double rr = double(u) * u + double(v) * v, g = std::exp(-2 * rr);
            for (int k = 0; k < 8; ++k) {
                const double ang = -2 * kPi * k / 8, xk = u * std::cos(ang) - v * std::sin(ang), yk = u * std::sin(ang) + v * std::cos(ang);
                const double z = xk + zeta - eta * yk * yk;
                o.wk.push_back(float(z > 0 ? z * z * g : 0.0));
            }
        }
        return o;
    }();
    return t;
}

std::vector<float> defocusWeight(const std::vector<float>& coc, int w, int h, const DefocusParams& p) {
    std::vector<float> o(std::size_t(w) * std::size_t(h), 0.0f);
    const int R = int(std::ceil(p.rim));
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
        const double c = coc[i * 2], z = coc[i * 2 + 1];
        double wt = smooth(p.c0, p.c1, c);
        for (int dy = -R; dy <= R && wt > 0; ++dy) for (int dx = -R; dx <= R; ++dx) {
            if (dx * dx + dy * dy > p.rim * p.rim) continue;
            const std::size_t q = clampIdx(x + dx, y + dy, w, h);
            if (coc[q * 2] <= p.c0 && coc[q * 2 + 1] < 0.98 * z) { wt = 0; break; }
        }
        o[i] = float(wt);
    }
    return o;
}

namespace {
// The Kuwahara (or the gaussian control) over one frame of unlined colour u (RGBA) and its CoC
// (c, z a pixel), radii and reach scaled by `scale` (0.5 at half resolution). Pixels with no weight
// copy u. The GPU twin is art_ts_prep, art_ts_tensor, art_ts_blur and art_ts_defocus.
std::vector<float> coarse(const std::vector<float>& u, const std::vector<float>& coc, int w, int h, const DefocusParams& p, DefocusMode mode,
                          double scale, int threads) {
    const std::size_t n = std::size_t(w) * std::size_t(h);
    std::vector<float> sb(n * 4, 0.0f), lat(n * 8, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const style::V3 col{u[i * 4], u[i * 4 + 1], u[i * 4 + 2]};
        const style::V3 e{style::linearToSrgb(col[0]), style::linearToSrgb(col[1]), style::linearToSrgb(col[2])};
        const style::V4 c = style::lutConc(e);
        const style::V3 dec = style::kmDecode(c);
        for (int k = 0; k < 3; ++k) sb[i * 4 + std::size_t(k)] = float(e[std::size_t(k)]);
        for (int k = 0; k < 4; ++k) lat[i * 8 + std::size_t(k)] = float(c[std::size_t(k)]);
        for (int k = 0; k < 3; ++k) lat[i * 8 + 4 + std::size_t(k)] = float(col[std::size_t(k)] - dec[std::size_t(k)]);
    }
    std::vector<float> T(n * 4, 0.0f), T2(n * 4, 0.0f);
    auto at = [&](int x, int y, int c) { return double(sb[clampIdx(x, y, w, h) * 4 + std::size_t(c)]); };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        double E = 0, F = 0, G = 0;
        for (int c = 0; c < 3; ++c) {
            const double gx = (at(x + 1, y - 1, c) + 2 * at(x + 1, y, c) + at(x + 1, y + 1, c) - at(x - 1, y - 1, c) - 2 * at(x - 1, y, c) - at(x - 1, y + 1, c)) / 4;
            const double gy = (at(x - 1, y + 1, c) + 2 * at(x, y + 1, c) + at(x + 1, y + 1, c) - at(x - 1, y - 1, c) - 2 * at(x, y - 1, c) - at(x + 1, y - 1, c)) / 4;
            E += gx * gx; F += gx * gy; G += gy * gy;
        }
        const std::size_t o = (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4;
        T[o] = float(E); T[o + 1] = float(F); T[o + 2] = float(G);
    }
    const std::vector<float> tw = style::paintGaussWeights(2.0);
    const int r = int(tw.size() / 2);
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<float>& a = pass == 0 ? T : T2;
        std::vector<float>& o = pass == 0 ? T2 : T;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 4; ++c) {
            double s = 0;
            for (int k = -r; k <= r; ++k) s += double(tw[std::size_t(k + r)]) * a[(pass == 0 ? clampIdx(x + k, y, w, h) : clampIdx(x, y + k, w, h)) * 4 + std::size_t(c)];
            o[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + std::size_t(c)] = float(s);
        }
    }
    const DefocusTables& tb = defocusTables();
    std::vector<float> out(u);
    parallelRows(h, threads, [&](int y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            const double c = coc[i * 2], zc = coc[i * 2 + 1], rad = 0.5 * c * scale;
            if (smooth(p.c0, p.c1, c) <= 0) continue;
            double res[3] = {0, 0, 0};
            if (mode == DefocusMode::Gaussian) {
                const double sg = std::max(0.5 * rad, 1e-3);
                const int R = int(std::ceil(3 * sg));
                double ws = 0;
                for (int dy = -R; dy <= R; ++dy) for (int dx = -R; dx <= R; ++dx) {
                    const std::size_t q = clampIdx(x + dx, y + dy, w, h);
                    const double d = std::sqrt(double(dx * dx + dy * dy));
                    double wq = std::exp(-0.5 * d * d / (sg * sg));
                    if (coc[q * 2 + 1] < zc) wq *= std::clamp(0.5 * coc[q * 2] * scale - d + 1, 0.0, 1.0);
                    ws += wq;
                    for (int k = 0; k < 3; ++k) res[k] += wq * u[q * 4 + std::size_t(k)];
                }
                for (int k = 0; k < 3; ++k) res[k] /= ws;
            } else {
                const auto fl = flowOf(T[i * 4], T[i * 4 + 1], T[i * 4 + 2]);
                const double tx = fl[0], ty = fl[1], A = fl[2], a = rad * (1 + A), b = rad / (1 + A);
                std::size_t qs[kTsSamples];
                double wo[kTsSamples], W[8] = {}, M[8][3] = {}, S2[8][3] = {};
                for (int s = 0; s < kTsSamples; ++s) {
                    const double uu = tb.pos[std::size_t(2 * s)], vv = tb.pos[std::size_t(2 * s + 1)];
                    const double ox = tx * a * uu - ty * b * vv, oy = ty * a * uu + tx * b * vv;
                    const std::size_t q = clampIdx(int(std::floor(x + ox + 0.5)), int(std::floor(y + oy + 0.5)), w, h);
                    double o = 1;
                    if (coc[q * 2 + 1] < zc) o = std::clamp(0.5 * coc[q * 2] * scale - std::sqrt(ox * ox + oy * oy) + 1, 0.0, 1.0);
                    qs[s] = q; wo[s] = o;
                    for (int k = 0; k < 8; ++k) {
                        const double g = double(tb.wk[std::size_t(s * 8 + k)]) * o;
                        W[k] += g;
                        for (int ch = 0; ch < 3; ++ch) { const double sv = sb[q * 4 + std::size_t(ch)]; M[k][ch] += g * sv; S2[k][ch] += g * sv * sv; }
                    }
                }
                double alpha[8] = {};
                for (int k = 0; k < 8; ++k) {
                    if (W[k] <= 0) continue;
                    double v2 = 0;
                    for (int ch = 0; ch < 3; ++ch) { const double m = M[k][ch] / W[k]; v2 += std::max(0.0, S2[k][ch] / W[k] - m * m); }
                    const double sd = std::sqrt(v2) * 255;
                    alpha[k] = 1 / (1 + (sd > 1e-6 ? std::pow(sd, p.q) : 0.0)) / W[k];
                }
                double L[7] = {}, ws = 0;
                for (int s = 0; s < kTsSamples; ++s) {
                    double cw = 0;
                    for (int k = 0; k < 8; ++k) cw += alpha[k] * tb.wk[std::size_t(s * 8 + k)];
                    cw *= wo[s];
                    ws += cw;
                    for (int k = 0; k < 7; ++k) L[k] += cw * lat[qs[s] * 8 + std::size_t(k)];
                }
                if (ws <= 0) continue;
                const style::V3 dec = style::kmDecode({L[0] / ws, L[1] / ws, L[2] / ws, L[3] / ws});
                for (int k = 0; k < 3; ++k) res[k] = dec[std::size_t(k)] + L[4 + k] / ws;
            }
            for (int k = 0; k < 3; ++k) out[i * 4 + std::size_t(k)] = float(res[k]);
        }
    });
    return out;
}
}  // namespace

std::vector<float> unlinedHalf(const std::vector<float>& img, const std::vector<float>& coc, int w, int h, std::vector<float>& hcoc) {
    const int hw = (w + 1) / 2, hh = (h + 1) / 2;
    std::vector<float> hu(std::size_t(hw) * std::size_t(hh) * 4, 1.0f);
    hcoc.assign(std::size_t(hw) * std::size_t(hh) * 2, 0.0f);
    for (int y = 0; y < hh; ++y) for (int x = 0; x < hw; ++x) {
        double c[3] = {}, cc = 0, zz = 0;
        for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) {
            const std::size_t q = clampIdx(2 * x + i, 2 * y + j, w, h);
            const double t = std::max(double(img[q * 4 + 3]), 0.05);
            for (int k = 0; k < 3; ++k) c[k] += img[q * 4 + std::size_t(k)] / t / 4;
            cc += coc[q * 2] / 4; zz += coc[q * 2 + 1] / 4;
        }
        const std::size_t o = std::size_t(y) * std::size_t(hw) + std::size_t(x);
        for (int k = 0; k < 3; ++k) hu[o * 4 + std::size_t(k)] = float(c[k]);
        hcoc[o * 2] = float(cc); hcoc[o * 2 + 1] = float(zz);
    }
    return hu;
}

std::vector<float> paintDefocus(const std::vector<float>& img, const std::vector<float>& coc, int w, int h, const DefocusParams& p,
                                DefocusMode mode, int threads) {
    const int hw = (w + 1) / 2, hh = (h + 1) / 2;
    std::vector<float> hcoc;
    const std::vector<float> hu = unlinedHalf(img, coc, w, h, hcoc);
    const std::vector<float> hc = coarse(hu, hcoc, hw, hh, p, mode, 0.5, threads);
    const std::vector<float> wt = defocusWeight(coc, w, h, p);
    std::vector<float> out(img);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
        const double wi = wt[i];
        if (wi <= 0) continue;
        // bilinear in the half frame at this pixel's centre (art_ts_compose)
        const double fx = std::clamp((x + 0.5) / 2 - 0.5, 0.0, double(hw - 1)), fy = std::clamp((y + 0.5) / 2 - 0.5, 0.0, double(hh - 1));
        const int x0 = std::min(int(std::floor(fx)), hw - 1), y0 = std::min(int(std::floor(fy)), hh - 1), x1 = std::min(x0 + 1, hw - 1), y1 = std::min(y0 + 1, hh - 1);
        const double tx = fx - x0, ty = fy - y0;
        for (int k = 0; k < 3; ++k) {
            auto v = [&](int xx, int yy) { return double(hc[(std::size_t(yy) * std::size_t(hw) + std::size_t(xx)) * 4 + std::size_t(k)]); };
            const double r = (v(x0, y0) * (1 - tx) + v(x1, y0) * tx) * (1 - ty) + (v(x0, y1) * (1 - tx) + v(x1, y1) * tx) * ty;
            out[i * 4 + std::size_t(k)] = float(img[i * 4 + std::size_t(k)] * (1 - wi) + r * wi);
        }
        out[i * 4 + 3] = float(img[i * 4 + 3] * (1 - wi) + wi);
    }
    return out;
}

}  // namespace raw::art
