// CRT purity and degauss, float64 reference: see raw/renderer/style_purity.hpp. Operations are in
// the order of web/shaders/purity/purity.mjs.
#include "raw/renderer/style_purity.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace raw::style {
namespace {
constexpr double kCent[3] = {-1.0 / 3, 0, 1.0 / 3};
double overlap(double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); }
using Landing = std::array<std::array<double, 3>, 3>;
// f[k][j]: the fraction of gun k's light on stripe j for landing errors ek.
Landing landing(const PurityPlan& p, const double ek[3]) {
    Landing f{};
    for (int k = 0; k < 3; ++k) {
        const double a0 = kCent[k] + ek[k] - p.w / 2, a1 = kCent[k] + ek[k] + p.w / 2;
        for (int j = 0; j < 3; ++j)
            for (int m = -1; m <= 1; ++m) f[std::size_t(k)][std::size_t(j)] += overlap(a0, a1, kCent[j] + m - p.s / 2, kCent[j] + m + p.s / 2) / p.w;
    }
    return f;
}
double vnoise2(double x, double y, std::uint32_t seed) {
    const double xi = std::floor(x), yi = std::floor(y), u = fade5(x - xi), v = fade5(y - yi);
    auto h = [seed](double i, double j) { return hash3(std::uint32_t(std::int64_t(i) + 4096), std::uint32_t(std::int64_t(j) + 4096), seed); };
    return (h(xi, yi) * (1 - u) + h(xi + 1, yi) * u) * (1 - v) + (h(xi, yi + 1) * (1 - u) + h(xi + 1, yi + 1) * u) * v;
}
}  // namespace

PurityPlan purityPreset(const std::string& name) {
    PurityPlan p;
    if (name == "magnetised") { p.mag = 0.16; p.earth = 0; p.degauss = 0; p.wobble = 0; return p; }
    if (name == "earth-field") { p.mag = 0.02; p.earth = 0.12; p.degauss = 0; p.wobble = 0; return p; }
    if (name == "degauss") { p.mag = 0.015; p.earth = 0; p.degauss = 0.08; p.wobble = 3; return p; }
    throw std::invalid_argument("purity: unknown preset " + name);
}

std::vector<double> runPurity(const PurityPlan& p, int w, int h, const std::vector<float>& src, double t) {
    std::vector<double> out(std::size_t(w) * h * 4, 0.0);
    const double a = double(w) / h, sc = p.magScale * w;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const double u = ((x + 0.5) / w) * 2 - 1, v = ((y + 0.5) / h) * 2 - 1, r2 = (u * u * a * a + v * v) / (a * a + 1) * 2;
        const double m = p.mag * ((0.65 * vnoise2(x / sc, y / sc, 31) + 0.35 * vnoise2(x / (0.4 * sc), y / (0.4 * sc), 37)) - 0.5) * 2;
        const double stat = m + p.earth * u * r2;
        const double tl = t + (double(y) / h) * (1 - p.blank) / p.field;
        const double dg = p.degauss * std::exp(-tl / p.tau) * std::sin(2 * std::numbers::pi * p.mains * tl) * (p.radial + r2);
        const double e = stat + dg, ek[3] = {e * (1 - p.spread), e, e * (1 + p.spread)};
        const Landing f = landing(p, ek);
        const double xs = x - p.wobble * (dg / std::max(p.degauss, 1e-9)), x0 = std::floor(xs), fx = xs - x0;
        double I[3] = {0, 0, 0};
        const double taps[2][2] = {{x0, 1 - fx}, {x0 + 1, fx}};
        for (const auto& tap : taps) {
            const int xc = int(std::min(double(w - 1), std::max(0.0, tap[0])));
            const std::size_t o = (std::size_t(y) * w + std::size_t(xc)) * 4;
            for (std::size_t k = 0; k < 3; ++k) I[k] += tap[1] * double(src[o + k]);
        }
        const std::size_t o = (std::size_t(y) * w + std::size_t(x)) * 4;
        for (std::size_t j = 0; j < 3; ++j) out[o + j] = f[0][j] * I[0] + f[1][j] * I[1] + f[2][j] * I[2];
        out[o + 3] = 1;
    }
    return out;
}

}  // namespace raw::style
