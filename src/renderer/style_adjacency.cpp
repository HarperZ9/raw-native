// Development adjacency, float64 reference: see raw/renderer/style_adjacency.hpp. Operations are in
// the order of web/shaders/adjacency/adjacency.mjs.
#include "raw/renderer/style_adjacency.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace raw::style {
namespace {
constexpr double kEps = 1e-6;
double developable(const AdjPlan& plan, double H) {
    if (H <= 0) return 0;
    const double q = std::pow(plan.p.H0 / H, plan.p.gamma);
    return 1 / (1 + q);
}
double coarseAt(const AdjPlan& plan, const std::vector<double>& F, int comp, int x, int y) {
    const int s = plan.s, cw = plan.cw, ch = plan.ch;
    const double u = (x + 0.5) / s - 0.5, v = (y + 0.5) / s - 0.5;
    const int x0 = std::max(0, std::min(cw - 1, int(std::floor(u)))), y0 = std::max(0, std::min(ch - 1, int(std::floor(v))));
    const int x1 = std::min(cw - 1, x0 + 1), y1 = std::min(ch - 1, y0 + 1);
    const double fx = std::max(0.0, std::min(1.0, u - x0)), fy = std::max(0.0, std::min(1.0, v - y0));
    auto g = [&](int i, int j) { return F[(std::size_t(j) * cw + std::size_t(i)) * 2 + std::size_t(comp)]; };
    return (g(x0, y0) * (1 - fx) + g(x1, y0) * fx) * (1 - fy) + (g(x0, y1) * (1 - fx) + g(x1, y1) * fx) * fy;
}
}  // namespace

AdjParams adjPreset(const std::string& name) {
    AdjParams p;
    if (name == "eberhard") return p;
    if (name == "mackie") { p.bromUm = 700; p.beta = 6; p.etaB = 0.8; p.rB = 0.6; return p; }
    if (name == "exhausted") { p.devUm = 800; p.bromUm = 350; p.eta = 0.7; p.r = 0.3; p.etaB = 0.4; p.gamma = 1.4; return p; }
    throw std::invalid_argument("adjacency: unknown preset " + name);
}

AdjPlan resolveAdjacency(const AdjParams& in, int w, int h) {
    AdjPlan plan;
    plan.p = in;
    if (plan.p.pitchUm <= 0) plan.p.pitchUm = 24900.0 / w;
    const AdjParams& p = plan.p;
    plan.w = w; plan.h = h;
    plan.s = std::max(1, int(std::floor(std::max(p.devUm, p.bromUm) / (p.pitchUm * 2.5))));
    plan.cw = int(std::ceil(double(w) / plan.s)); plan.ch = int(std::ceil(double(h) / plan.s));
    const double px = p.pitchUm * plan.s;
    plan.Dc = std::pow(p.devUm / px, 2) / p.T; plan.Db = std::pow(p.bromUm / px, 2) / p.T;
    plan.steps = std::max(16, int(std::ceil((p.T * std::max({plan.Dc, plan.Db, p.k})) / 0.2)));
    plan.dt = p.T / plan.steps;
    plan.decay = std::pow(1 - p.k * plan.dt, plan.steps);
    return plan;
}

std::vector<double> runAdjacency(const AdjPlan& plan, const std::vector<float>& scene) {
    const int w = plan.w, h = plan.h, s = plan.s, cw = plan.cw, ch = plan.ch;
    const AdjParams& p = plan.p;
    const std::size_t n = std::size_t(w) * h, m = std::size_t(cw) * ch;
    std::vector<double> A(n * 3), D(n * 3, 0.0), dsum(n), F(m * 2, 0.0), G(m * 2, 0.0);
    for (std::size_t i = 0; i < n; ++i) for (std::size_t k = 0; k < 3; ++k) A[i * 3 + k] = developable(plan, p.exposure * double(scene[i * 4 + k]));
    for (std::size_t i = 0; i < m; ++i) F[i * 2] = 1;
    const double dt = plan.dt;
    for (int it = 0; it < plan.steps; ++it) {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + std::size_t(x);
            const double c = coarseAt(plan, F, 0, x, y), b = coarseAt(plan, F, 1, x, y), rate = (p.k * c) / (1 + p.beta * b);
            double sum = 0;
            for (std::size_t k = 0; k < 3; ++k) { const double d = rate * (A[i * 3 + k] - D[i * 3 + k]) * dt; D[i * 3 + k] += d; sum += d; }
            dsum[i] = sum;
        }
        for (int j = 0; j < ch; ++j) for (int i = 0; i < cw; ++i) {
            double used = 0, cnt = 0;
            for (int yy = j * s; yy < std::min(h, j * s + s); ++yy) for (int xx = i * s; xx < std::min(w, i * s + s); ++xx) { used += dsum[std::size_t(yy) * w + std::size_t(xx)]; cnt++; }
            used /= cnt;
            auto at = [&](int ii, int jj, int c) { return F[(std::size_t(std::max(0, std::min(ch - 1, jj))) * cw + std::size_t(std::max(0, std::min(cw - 1, ii)))) * 2 + std::size_t(c)]; };
            const std::size_t o = (std::size_t(j) * cw + std::size_t(i)) * 2;
            const double c0 = F[o], b0 = F[o + 1];
            const double lc = at(i - 1, j, 0) + at(i + 1, j, 0) + at(i, j - 1, 0) + at(i, j + 1, 0) - plan.lapCentre * c0;
            const double lb = at(i - 1, j, 1) + at(i + 1, j, 1) + at(i, j - 1, 1) + at(i, j + 1, 1) - plan.lapCentre * b0;
            G[o] = c0 + plan.Dc * lc * dt - p.eta * used + p.r * (1 - c0) * dt;
            G[o + 1] = b0 + plan.Db * lb * dt + p.etaB * used - p.rB * b0 * dt;
        }
        std::swap(F, G);
    }
    std::vector<double> out(n * 4);
    const double free = 1 - plan.decay;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < 3; ++k) out[i * 4 + k] = double(scene[i * 4 + k]) * ((D[i * 3 + k] + kEps) / (A[i * 3 + k] * free + kEps));
        out[i * 4 + 3] = 1;
    }
    return out;
}

}  // namespace raw::style
