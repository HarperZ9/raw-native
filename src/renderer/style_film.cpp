// The spectral film shader's plan and passes: see raw/renderer/style_film.hpp. Operations are in
// the order of web/shaders/film/film.mjs, run.mjs and crt/glass.mjs; a float here is a
// Float32Array element there.
#include "raw/renderer/style_film.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kLn10 = 2.302585092994046;

double depthLayers(double rUm, double span, double sigmaD48, const std::vector<float>& table) {
    const double A = kPi * 24 * 24, a = 1 - std::pow(10, -span), f = (1 - 0.1) / a;
    const double ff = std::min(32.0, std::max(0.0, f * 32));
    const int i = std::min(31, int(std::floor(ff)));
    const double tab = double(table[std::size_t(i)]) + (double(table[std::size_t(i + 1)]) - double(table[std::size_t(i)])) * (ff - i);
    const double sigmaF = std::sqrt(tab * rUm * rUm / A), dDdf = a / (kLn10 * (1 - f * a));
    return std::max(1.0, std::pow((dDdf * sigmaF) / sigmaD48, 2));
}
std::size_t px4(int w, int x, int y) { return (std::size_t(y) * w + std::size_t(x)) * 4; }
// Bilinear read of an RGBA f32 grid at continuous coordinates, clamped (sampleScene, upsample).
T3 bilinear(const std::vector<float>& g, int W, int H, double fx, double fy) {
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy)), x1 = std::min(W - 1, x0 + 1), y1 = std::min(H - 1, y0 + 1);
    const double tx = fx - x0, ty = fy - y0;
    T3 o{};
    for (int c = 0; c < 3; ++c) {
        const std::size_t k = std::size_t(c);
        o[k] = (double(g[px4(W, x0, y0) + k]) * (1 - tx) + double(g[px4(W, x1, y0) + k]) * tx) * (1 - ty) + (double(g[px4(W, x0, y1) + k]) * (1 - tx) + double(g[px4(W, x1, y1) + k]) * tx) * ty;
    }
    return o;
}
double curve(double x, double dmin, double span, double k, double x0) { return dmin + span / (1 + std::exp(-k * (x - x0))); }

std::vector<float> expose(const FilmPlan& plan, const std::vector<float>& scene, int f) {
    const auto dxy = filmWeave(plan, f);
    std::vector<float> H(static_cast<std::size_t>(plan.outW) * plan.outH * 4, 0.0f);
    const double k = std::pow(2, plan.p.ev), sx = double(plan.inW) / plan.outW, sy = double(plan.inH) / plan.outH;
    const M3& E = plan.t.E;
    for (int y = 0; y < plan.outH; ++y) for (int x = 0; x < plan.outW; ++x) {
        const double fx = std::min(double(plan.inW - 1), std::max(0.0, (x + 0.5 - dxy[0]) * sx - 0.5)), fy = std::min(double(plan.inH - 1), std::max(0.0, (y + 0.5 - dxy[1]) * sy - 0.5));
        const T3 c = bilinear(scene, plan.inW, plan.inH, fx, fy);
        for (int l = 0; l < 3; ++l) H[px4(plan.outW, x, y) + std::size_t(l)] = float(k * (E[std::size_t(l * 3)] * c[0] + E[std::size_t(l * 3 + 1)] * c[1] + E[std::size_t(l * 3 + 2)] * c[2]));
    }
    return H;
}
std::vector<float> mtfPass(const FilmPlan& plan, const std::vector<float>& src, bool alongY) {
    const int r = int(plan.mtf.size() - 1) / 2, W = plan.outW, Hh = plan.outH;
    std::vector<float> o(src.size(), 0.0f);
    for (int y = 0; y < Hh; ++y) for (int x = 0; x < W; ++x) for (int c = 0; c < 3; ++c) {
        double s = 0;
        for (int k = -r; k <= r; ++k) {
            const int xx = alongY ? x : std::min(W - 1, std::max(0, x + k)), yy = alongY ? std::min(Hh - 1, std::max(0, y + k)) : y;
            s += double(plan.mtf[std::size_t(k + r)]) * double(src[px4(W, xx, yy) + std::size_t(c)]);
        }
        o[px4(W, x, y) + std::size_t(c)] = float(s);
    }
    return o;
}
std::vector<float> haloDown(const FilmPlan& plan, const std::vector<float>& em, int gw, int gh) {
    const int q = plan.haloQ, W = plan.outW, H = plan.outH;
    std::vector<float> g(static_cast<std::size_t>(gw) * gh * 4, 0.0f);
    for (int gy = 0; gy < gh; ++gy) for (int gx = 0; gx < gw; ++gx) {
        double r = 0, gg = 0, b = 0, n = 0;
        for (int y = gy * q; y < std::min(H, gy * q + q); ++y) for (int x = gx * q; x < std::min(W, gx * q + q); ++x) {
            const std::size_t i = px4(W, x, y);
            r += em[i]; gg += em[i + 1]; b += em[i + 2]; n++;
        }
        const std::size_t o = px4(gw, gx, gy);
        g[o] = float(r / n); g[o + 1] = float(gg / n); g[o + 2] = float(b / n);
    }
    return g;
}
std::vector<float> haloConv(const FilmPlan& plan, const std::vector<float>& g, int gw, int gh) {
    const int R = plan.haloR, S = 2 * R + 1;
    std::vector<float> o(g.size(), 0.0f);
    for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
        double r = 0, gg = 0, b = 0;
        for (int dy = -R; dy <= R; ++dy) {
            const int yy = y + dy;
            if (yy < 0 || yy >= gh) continue;
            for (int dx = -R; dx <= R; ++dx) {
                const int xx = x + dx;
                if (xx < 0 || xx >= gw) continue;
                const double w = plan.haloWeights[std::size_t((dy + R) * S + dx + R)];
                const std::size_t i = px4(gw, xx, yy);
                r += w * g[i]; gg += w * g[i + 1]; b += w * g[i + 2];
            }
        }
        const std::size_t i = px4(gw, x, y);
        o[i] = float(r); o[i + 1] = float(gg); o[i + 2] = float(b);
    }
    return o;
}
std::vector<float> develop(const FilmPlan& plan, const std::vector<float>& H, const std::vector<float>& halo, int gw, int gh) {
    const FilmStock& t = plan.t;
    const int q = plan.haloQ;
    std::vector<float> F(H.size(), 0.0f);
    FilmStock ti = t;
    ti.interimage = t.interimage * plan.p.interimageScale;
    for (int y = 0; y < plan.outH; ++y) for (int x = 0; x < plan.outW; ++x) {
        const std::size_t i = px4(plan.outW, x, y);
        const double fx = std::min(double(gw - 1), std::max(0.0, (x + 0.5) / q - 0.5)), fy = std::min(double(gh - 1), std::max(0.0, (y + 0.5) / q - 0.5));
        const T3 hl = bilinear(halo, gw, gh, fx, fy);
        T3 D{};
        for (int l = 0; l < 3; ++l) {
            const std::size_t L = std::size_t(l);
            const double h = std::max(1e-6, double(H[i + L]) + plan.haloStrength[L] * hl[L]);
            D[L] = curve(std::log10(h), t.negDmin[L], t.negSpan[L], t.negK[L], t.negX0[L]);
        }
        const T3 Di = filmInterimage(ti, D);
        for (int l = 0; l < 3; ++l) {
            const std::size_t L = std::size_t(l);
            const double span = t.negSpan[L], d = std::min(span, std::max(0.0, Di[L] - t.negDmin[L]));
            F[i + L] = float((1 - std::pow(10, -d)) / (1 - std::pow(10, -span)));
        }
    }
    return F;
}
}  // namespace

std::vector<float> filmMtfWeights(double sigmaPx) {
    if (!(sigmaPx >= 0.15)) return {1.0f};
    const int r = std::min(16, int(std::ceil(3 * sigmaPx)));
    std::vector<float> w(static_cast<std::size_t>(2 * r + 1));
    double s = 0;
    for (int i = -r; i <= r; ++i) { const double q = i / sigmaPx; w[std::size_t(i + r)] = float(std::exp(-0.5 * (q * q))); s += w[std::size_t(i + r)]; }
    for (float& v : w) v = float(double(v) / s);
    return w;
}

std::array<double, 2> filmWeave(const FilmPlan& plan, int f) {
    const FilmParams& p = plan.p;
    const std::uint32_t seed = std::uint32_t(p.seed);
    auto s = [&](std::uint32_t k) { return hash3(seed, k, 7) * 2 * kPi; };
    const double tt = f / 24.0;
    const double wx = p.weaveUm * (0.6 * std::sin(2 * kPi * 0.7 * tt + s(1)) + 0.4 * std::sin(2 * kPi * 2.3 * tt + s(2))) + p.jitterUm * (hash3(seed, std::uint32_t(f), 11) - 0.5);
    const double wy = p.weaveUm * (0.6 * std::sin(2 * kPi * 0.5 * tt + s(3)) + 0.4 * std::sin(2 * kPi * 1.9 * tt + s(4))) + p.jitterUm * (hash3(seed, std::uint32_t(f), 13) - 0.5);
    return {wx / plan.umPerPx, wy / plan.umPerPx};
}

FilmPlan resolveFilm(const FilmParams& p, int inW, int inH, int outW, int outH) {
    FilmPlan plan;
    plan.p = p; plan.inW = inW; plan.inH = inH; plan.outW = outW; plan.outH = outH;
    plan.t = buildFilmStock(p);
    plan.umPerPx = (p.gateWidthMm * 1000) / outW;
    plan.jTable = filmVarianceTable();
    const BaseGlass glass{0.125, 1.49, 1, 0.4, 0.02, p.pressurePlate};
    const HaloKernel hk = filmHaloKernel(glass, plan.umPerPx / 1000, 25);
    double sum = 0;
    for (float w : hk.weights) sum = sum + w;
    const double kappa = filmRadialKappa(glass, 0.001, 5);
    for (int l = 0; l < 3; ++l) {
        const std::size_t L = std::size_t(l);
        plan.grainContrast[L] = 1 / std::sqrt(depthLayers(plan.t.grainUm[L], plan.t.negSpan[L], plan.t.sigmaD48, plan.jTable));
        plan.haloStrength[L] = p.halationReach[L] * kappa * (p.remjet ? 0.03 : 1);
        plan.grainUm[L] = plan.t.grainUm[L] * p.grainScale;
    }
    plan.haloQ = hk.q; plan.haloR = hk.R; plan.ringMm = hk.ringMm;
    plan.haloWeights.resize(hk.weights.size());
    for (std::size_t i = 0; i < hk.weights.size(); ++i) plan.haloWeights[i] = float(sum > 0 ? double(hk.weights[i]) / sum : 0);
    plan.mtf = filmMtfWeights(p.emulsionSigmaUm / plan.umPerPx);
    return plan;
}

std::vector<float> filmFrame(const FilmPlan& plan, const std::vector<float>& scene, int f) {
    std::vector<float> H = expose(plan, scene, f);
    if (plan.mtf.size() > 1) H = mtfPass(plan, mtfPass(plan, H, false), true);
    const int gw = (plan.outW + plan.haloQ - 1) / plan.haloQ, gh = (plan.outH + plan.haloQ - 1) / plan.haloQ;
    const std::vector<float> halo = haloConv(plan, haloDown(plan, H, gw, gh), gw, gh);
    const std::vector<float> F = develop(plan, H, halo, gw, gh);
    const FilmStock& t = plan.t;
    std::vector<float> img(F.size(), 0.0f);
    for (int y = 0; y < plan.outH; ++y) for (int x = 0; x < plan.outW; ++x) {
        T3 D{};
        for (int l = 0; l < 3; ++l) {
            const double g = filmGrainAt(plan, x, y, l, F, f), span = t.negSpan[std::size_t(l)];
            D[std::size_t(l)] = t.negDmin[std::size_t(l)] - std::log10(std::max(1e-6, 1 - g * (1 - std::pow(10, -span))));
        }
        const T3 rgb = filmPrintPixel(t, D);
        const std::size_t i = px4(plan.outW, x, y);
        img[i] = float(rgb[0]); img[i + 1] = float(rgb[1]); img[i + 2] = float(rgb[2]); img[i + 3] = 1;
    }
    return img;
}

std::uint8_t filmEncode8(double v) {
    const double l = std::min(1.0, std::max(0.0, v));
    return std::uint8_t(std::floor((l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055) * 255 + 0.5));
}

}  // namespace raw::style
