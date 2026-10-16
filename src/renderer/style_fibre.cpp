// Fibre-network watercolour, float64 reference: see raw/renderer/style_fibre.hpp. Operations are
// in the order of web/shaders/fibre/fibre.mjs.
#include "raw/renderer/style_fibre.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
struct Link { int dx, dy; double g; };
void links(double Kxx, double Kxy, double Kyy, Link* L) {
    const double ax = std::max(0.0, Kxx - std::abs(Kxy)), ay = std::max(0.0, Kyy - std::abs(Kxy)), dg = std::abs(Kxy) / 2;
    const bool pos = Kxy >= 0;
    L[0] = {1, 0, ax}; L[1] = {-1, 0, ax}; L[2] = {0, 1, ay}; L[3] = {0, -1, ay};
    L[4] = {1, 1, pos ? dg : 0}; L[5] = {-1, -1, pos ? dg : 0}; L[6] = {1, -1, pos ? 0 : dg}; L[7] = {-1, 1, pos ? 0 : dg};
}
}  // namespace

FibreParams fibrePreset(const std::string& name) {
    FibreParams p;
    if (name == "cold-press") return p;
    if (name == "hot-press") { p.edgeDry = 8; p.perCell = 3; p.len = 0.06; p.width = 1.0; p.k0 = 0.3; p.k1 = 0.5; p.rate = 0.12; p.floc = 0.3; p.steps = 32; p.paper = {0.97, 0.96, 0.93}; return p; }
    if (name == "rough") { p.edgeDry = 4; p.perCell = 9; p.len = 0.08; p.width = 1.4; p.md = 0.15; p.spread = 0.6; p.k0 = 0.2; p.k1 = 0.95; p.rate = 0.06; p.floc = 1.5; p.evap = 0.015; p.steps = 56; p.paper = {0.95, 0.92, 0.86}; return p; }
    throw std::invalid_argument("fibre: unknown preset " + name);
}

FibrePlan resolveFibre(const FibreParams& p, int w, int h) {
    FibrePlan plan;
    plan.p = p; plan.w = w; plan.h = h;
    plan.L = std::max(2.0, p.len * h);
    plan.C = std::max(2.0, std::ceil(plan.L));
    const double gmax = 2 * (2 * p.k0 + p.k1);
    plan.dt = 0.45 / gmax;
    return plan;
}

std::vector<double> fibrePaperField(const FibrePlan& plan) {
    const int w = plan.w, h = plan.h;
    const FibreParams& p = plan.p;
    const double C = plan.C;
    std::vector<double> F(static_cast<std::size_t>(w) * h * 5);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const double qx = x + 0.5, qy = y + 0.5;
        const int cx = int(std::floor(qx / C)), cy = int(std::floor(qy / C));
        double cov = 0, cov2 = 0, txx = 0, txy = 0, tyy = 0;
        for (int j = cy - 1; j <= cy + 1; ++j) for (int i = cx - 1; i <= cx + 1; ++i) for (int k = 0; k < p.perCell; ++k) {
            const std::uint32_t a = std::uint32_t(i + 8192), b = std::uint32_t(j + 8192), s = std::uint32_t(k * 97 + 13);
            const double fx = (i + hash3(a, b, s)) * C, fy = (j + hash3(a, b, s + 1)) * C, th = kPi * (p.md + p.spread * (hash3(a, b, s + 2) - 0.5));
            const double lf = plan.L * (0.6 + 0.8 * hash3(a, b, s + 3)), dx = std::cos(th), dy = std::sin(th), hl = lf / 2;
            const double rx = qx - fx, ry = qy - fy, t = std::max(-hl, std::min(hl, rx * dx + ry * dy)), ex = rx - t * dx, ey = ry - t * dy;
            const double c = std::max(0.0, std::min(1.0, 0.5 * p.width + 0.5 - std::sqrt(ex * ex + ey * ey)));
            if (c > 0) { cov += c; cov2 += c * c; txx += c * dx * dx; txy += c * dx * dy; tyy += c * dy * dy; }
        }
        double* o = &F[(std::size_t(y) * w + std::size_t(x)) * 5];
        o[0] = cov; o[1] = txx; o[2] = txy; o[3] = tyy; o[4] = 0.5 * (cov * cov - cov2);
    }
    return F;
}

std::vector<double> fibreTensors(const FibrePlan& plan, const std::vector<double>& F) {
    const int w = plan.w, h = plan.h, R = plan.tensorRadius;
    const FibreParams& p = plan.p;
    std::vector<double> K(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        double c = 0, a = 0, b = 0, d = 0, n = 0;
        for (int yy = std::max(0, y - R); yy <= std::min(h - 1, y + R); ++yy) for (int xx = std::max(0, x - R); xx <= std::min(w - 1, x + R); ++xx) {
            const double* o = &F[(std::size_t(yy) * w + std::size_t(xx)) * 5];
            c += o[0]; a += o[1]; b += o[2]; d += o[3]; n++;
        }
        const double cm = c / n, s = c > 1e-12 ? 1 / c : 0, g = p.k1 * std::min(1.0, cm);
        double* k = &K[(std::size_t(y) * w + std::size_t(x)) * 3];
        k[0] = p.k0 + g * a * s; k[1] = g * b * s; k[2] = p.k0 + g * d * s;
    }
    return K;
}

std::vector<double> runFibre(const FibrePlan& plan, const std::vector<float>& src) {
    const int w = plan.w, h = plan.h;
    const FibreParams& p = plan.p;
    const double dt = plan.dt;
    const std::size_t n = std::size_t(w) * h;
    const std::vector<double> F = fibrePaperField(plan), K = fibreTensors(plan, F);
    std::vector<double> P(n * 3), Pn(n * 3), D(n * 3, 0.0), Wt(n), Wn(n);
    auto lum = [&](int x, int y) {
        const std::size_t o = (std::size_t(std::min(h - 1, std::max(0, y))) * w + std::size_t(std::min(w - 1, std::max(0, x)))) * 4;
        return 0.2126 * src[o] + 0.7152 * src[o + 1] + 0.0722 * src[o + 2];
    };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const double gx = 0.5 * (lum(x + 1, y) - lum(x - 1, y)), gy = 0.5 * (lum(x, y + 1) - lum(x, y - 1));
        Wt[std::size_t(y) * w + std::size_t(x)] = std::min(1.0, std::max(0.05, 1 - p.edgeDry * std::sqrt(gx * gx + gy * gy)));
    }
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t c = 0; c < 3; ++c) P[i * 3 + c] = p.strength * -std::log(std::max(0.02, std::min(1.0, double(src[i * 4 + c]) / p.paper[c])));
    std::vector<Link> LK(n * 8);
    for (std::size_t i = 0; i < n; ++i) links(K[i * 3], K[i * 3 + 1], K[i * 3 + 2], &LK[i * 8]);
    for (int it = 0; it < p.steps; ++it) {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + std::size_t(x);
            const double dep = p.rate * (1 + p.floc * F[i * 5 + 4]) * dt;
            double flux[3] = {0, 0, 0};
            for (int l = 0; l < 8; ++l) {
                const Link& Li = LK[i * 8 + std::size_t(l)];
                const int xx = x + Li.dx, yy = y + Li.dy;
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                const std::size_t j = std::size_t(yy) * w + std::size_t(xx);
                const double gj = LK[j * 8 + std::size_t(l ^ 1)].g, g = 0.5 * (Li.g + gj) * std::min(Wt[i], Wt[j]) * dt;
                for (std::size_t c = 0; c < 3; ++c) flux[c] += g * (P[j * 3 + c] - P[i * 3 + c]);
            }
            for (std::size_t c = 0; c < 3; ++c) { const double pc = P[i * 3 + c] + flux[c], dd = std::min(pc, dep * pc); Pn[i * 3 + c] = pc - dd; D[i * 3 + c] += dd; }
            Wn[i] = std::max(0.0, Wt[i] - p.evap);
        }
        std::swap(P, Pn); std::swap(Wt, Wn);
    }
    std::vector<double> out(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t c = 0; c < 3; ++c) out[i * 4 + c] = p.paper[c] * std::exp(-(P[i * 3 + c] + D[i * 3 + c]));
        out[i * 4 + 3] = 1;
    }
    return out;
}

}  // namespace raw::style
