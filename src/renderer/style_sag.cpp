// Scan-causal EHT sag and the salience caricature warp: see raw/renderer/style_sag.hpp.
// Operations are in the order of web/shaders/sag/sag.mjs and caricature/caricature.mjs.
#include "raw/renderer/style_sag.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::style {
namespace {
std::size_t at4(int w, int x, int y) { return (std::size_t(y) * w + std::size_t(x)) * 4; }

void sampleLine(const SagPlan& plan, const std::vector<float>& src, int y, double xs, double sigma, double out[3]) {
    const int w = plan.w, R = plan.R, x0 = int(std::floor(xs));
    double ws = 0;
    out[0] = out[1] = out[2] = 0;
    for (int j = x0 - R; j <= x0 + R; ++j) {
        if (j < 0 || j >= w) continue;
        const double d = (j + 0.5 - xs) / sigma, wt = std::exp(-0.5 * d * d);
        const std::size_t o = at4(w, j, y);
        ws += wt;
        for (int c = 0; c < 3; ++c) out[c] += wt * std::pow(std::max(0.0, double(src[o + std::size_t(c)])), plan.p.gamma);
    }
    if (ws > 0) for (int c = 0; c < 3; ++c) out[c] /= ws;
}
}  // namespace

SagParams sagPreset(const std::string& name) {
    SagParams p;
    if (name == "consumer") return p;
    if (name == "thriller") { p.S = 0.08; p.tauFrame = 0.05; p.focus = 14; p.sigma0 = 0.5; p.bright = 1.5; return p; }
    throw std::invalid_argument("sag: unknown preset " + name);
}
SagPlan resolveSag(const SagParams& p, int w, int h) {
    SagPlan plan;
    plan.p = p; plan.w = w; plan.h = h;
    plan.blank = int(std::floor(h * p.blankFrac + 0.5));
    plan.tauLines = p.tauFrame * (h + plan.blank);
    plan.alpha = 1 - std::exp(-1 / plan.tauLines);
    plan.R = int(std::ceil(3 * (p.sigma0 + p.focus * p.S))) + 1;
    return plan;
}

std::vector<float> runSag(const SagPlan& plan, const std::vector<float>& src, SagScan& sc) {
    const int w = plan.w, h = plan.h;
    const SagParams& p = plan.p;
    sc.I.assign(std::size_t(h), 0.0);
    for (int y = 0; y < h; ++y) {
        double s = 0;
        for (int x = 0; x < w; ++x) {
            const std::size_t o = at4(w, x, y);
            s += (std::pow(std::max(0.0, double(src[o])), p.gamma) + std::pow(std::max(0.0, double(src[o + 1])), p.gamma) + std::pow(std::max(0.0, double(src[o + 2])), p.gamma)) / 3;
        }
        sc.I[std::size_t(y)] = s / w;
    }
    const double cy = h / 2.0, alpha = plan.alpha;
    sc.v.assign(std::size_t(h), 0.0); sc.k = sc.v; sc.Y = sc.v; sc.sig = sc.v;
    double s = p.initial;
    auto pass = [&] { for (int y = 0; y < h; ++y) { s = s + alpha * (p.S * sc.I[std::size_t(y)] - s); sc.v[std::size_t(y)] = s; } };
    pass();
    if (p.settle) { s = s * std::pow(1 - alpha, plan.blank); pass(); }
    for (int y = 0; y < h; ++y) {
        const std::size_t Y = std::size_t(y);
        sc.k[Y] = 1 / std::sqrt(1 - sc.v[Y]); sc.Y[Y] = cy + (y + 0.5 - cy) * sc.k[Y]; sc.sig[Y] = p.sigma0 + p.focus * sc.v[Y];
    }
    const double cx = w / 2.0;
    std::vector<float> out(std::size_t(w) * h * 4, 0.0f);
    double a[3], b[3];
    for (int yo = 0; yo < h; ++yo) {
        const double yc = yo + 0.5;
        if (yc < sc.Y[0] || yc > sc.Y[std::size_t(h - 1)]) { for (int x = 0; x < w; ++x) out[at4(w, x, yo) + 3] = 1; continue; }
        int lo = 0, hi = h - 1;
        while (hi - lo > 1) { const int m = (lo + hi) >> 1; if (sc.Y[std::size_t(m)] <= yc) lo = m; else hi = m; }
        const int y1 = std::min(h - 1, lo + 1);
        const double f = y1 == lo ? 0 : (yc - sc.Y[std::size_t(lo)]) / (sc.Y[std::size_t(y1)] - sc.Y[std::size_t(lo)]);
        for (int x = 0; x < w; ++x) {
            const double xc = x + 0.5;
            sampleLine(plan, src, lo, cx + (xc - cx) / sc.k[std::size_t(lo)], sc.sig[std::size_t(lo)], a);
            sampleLine(plan, src, y1, cx + (xc - cx) / sc.k[std::size_t(y1)], sc.sig[std::size_t(y1)], b);
            const double ga = std::pow(1 - sc.v[std::size_t(lo)], p.bright), gb = std::pow(1 - sc.v[std::size_t(y1)], p.bright);
            const std::size_t o = at4(w, x, yo);
            for (int c = 0; c < 3; ++c) out[o + std::size_t(c)] = float(std::pow((a[c] * ga) * (1 - f) + (b[c] * gb) * f, 1 / p.gamma));
            out[o + 3] = 1;
        }
    }
    return out;
}
std::uint8_t sagSignal8(double v) { return std::uint8_t(std::floor(std::min(1.0, std::max(0.0, v)) * 255 + 0.5)); }

CariParams cariPreset(const std::string& name) {
    CariParams p;
    if (name == "grotesque") { p.weights[5] = 1.0; p.weights[6] = 0.7; p.weights[4] = 0.45; p.weights[8] = 0.5; p.weights[7] = 0.15; return p; }
    if (name == "subtle") { p.weights[5] = 1.0; p.weights[6] = 0.6; p.weights[4] = 0.5; p.weights[8] = 0.4; p.weights[7] = 0.1; p.sigma = 0.09; p.gain = 0.4; p.steps = 1; return p; }
    throw std::invalid_argument("caricature: unknown preset " + name);
}
CariPlan resolveCaricature(const CariParams& p, int w, int h) {
    CariPlan plan;
    plan.p = p; plan.w = w; plan.h = h;
    const double sig = p.sigma * h;
    plan.R = int(std::ceil(3 * sig));
    plan.taps.assign(std::size_t(2 * plan.R + 1), 0.0);
    double s = 0;
    for (int i = -plan.R; i <= plan.R; ++i) { const double q = i / sig; plan.taps[std::size_t(i + plan.R)] = std::exp(-0.5 * (q * q)); s += plan.taps[std::size_t(i + plan.R)]; }
    for (double& t : plan.taps) t /= s;
    return plan;
}

std::vector<float> runCaricature(const CariPlan& plan, const std::vector<float>& src, const std::vector<std::int32_t>& mat, CariFields& F) {
    const int w = plan.w, h = plan.h, R = plan.R;
    const std::size_t n = std::size_t(w) * h;
    std::vector<double> sal(n), a(n);
    for (std::size_t i = 0; i < n; ++i) sal[i] = mat[i] >= 0 && mat[i] <= 9 ? plan.p.weights[std::size_t(mat[i])] : 0;
    auto cl = [&](int x, int y) { return std::size_t(std::min(h - 1, std::max(0, y))) * w + std::size_t(std::min(w - 1, std::max(0, x))); };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { double v = 0; for (int k = -R; k <= R; ++k) v += plan.taps[std::size_t(k + R)] * sal[std::size_t(y) * w + std::size_t(std::min(w - 1, std::max(0, x + k)))]; a[std::size_t(y) * w + std::size_t(x)] = v; }
    F.S.assign(n, 0.0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { double v = 0; for (int k = -R; k <= R; ++k) v += plan.taps[std::size_t(k + R)] * a[std::size_t(std::min(h - 1, std::max(0, y + k))) * w + std::size_t(x)]; F.S[std::size_t(y) * w + std::size_t(x)] = v; }
    F.g.assign(n * 2, 0.0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + std::size_t(x);
        F.g[i * 2] = 0.5 * (F.S[cl(x + 1, y)] - F.S[cl(x - 1, y)]); F.g[i * 2 + 1] = 0.5 * (F.S[cl(x, y + 1)] - F.S[cl(x, y - 1)]);
    }
    double m = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const double A0 = 0.5 * (F.g[cl(x + 1, y) * 2] - F.g[cl(x - 1, y) * 2]), A1 = 0.5 * (F.g[cl(x, y + 1) * 2] - F.g[cl(x, y - 1) * 2]);
        const double A2 = 0.5 * (F.g[cl(x + 1, y) * 2 + 1] - F.g[cl(x - 1, y) * 2 + 1]), A3 = 0.5 * (F.g[cl(x, y + 1) * 2 + 1] - F.g[cl(x, y - 1) * 2 + 1]);
        m = std::max(m, std::sqrt(A0 * A0 + A1 * A1 + A2 * A2 + A3 * A3));
    }
    const int steps = plan.p.steps > 0 ? plan.p.steps : 1;
    const double c = 1 - std::pow(0.04, 1.0 / (2 * steps));
    F.lambda = m > 0 ? std::min(plan.p.lambda, (c * plan.p.gain) / m) : 0;
    auto bil = [&](double fx, double fy, auto get, int nc, double* r) {
        const double u = fx - 0.5, v = fy - 0.5;
        const int x0 = std::max(0, std::min(w - 1, int(std::floor(u)))), y0 = std::max(0, std::min(h - 1, int(std::floor(v)))), x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
        const double tx = std::max(0.0, std::min(1.0, u - x0)), ty = std::max(0.0, std::min(1.0, v - y0));
        for (int k = 0; k < nc; ++k) r[k] = (get(x0, y0, k) * (1 - tx) + get(x1, y0, k) * tx) * (1 - ty) + (get(x0, y1, k) * (1 - tx) + get(x1, y1, k) * tx) * ty;
    };
    const auto gGet = [&](int x, int y, int k) { return F.g[(std::size_t(y) * w + std::size_t(x)) * 2 + std::size_t(k)]; };
    const auto sGet = [&](int x, int y, int k) { return double(src[(std::size_t(y) * w + std::size_t(x)) * 4 + std::size_t(k)]); };
    std::vector<float> out(n * 4, 0.0f);
    const double lam = F.lambda;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + std::size_t(x);
        double fx = x + 0.5 + lam * F.g[i * 2], fy = y + 0.5 + lam * F.g[i * 2 + 1];
        for (int k = 1; k < steps; ++k) { double d[2]; bil(fx, fy, gGet, 2, d); fx += lam * d[0]; fy += lam * d[1]; }
        double col[3];
        bil(fx, fy, sGet, 3, col);
        for (int k = 0; k < 3; ++k) out[i * 4 + std::size_t(k)] = float(col[k]);
        out[i * 4 + 3] = 1;
    }
    return out;
}

}  // namespace raw::style
