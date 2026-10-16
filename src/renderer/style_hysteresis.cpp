// Hysteresis quantisation, float64 reference: see raw/renderer/style_quantise.hpp. Operations are
// in the order of web/shaders/hysteresis/hysteresis.mjs.
#include "raw/renderer/style_quantise.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace raw::style {
namespace {
double shoulder(double v, double g) { return 1 - std::exp(-std::max(0.0, v) * g); }
// hysteresis.mjs's own fromOklab: the cube by **3.
V3 fromOklab(double L, double a, double b) {
    const double l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3), m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3),
                 s = std::pow(L - 0.0894841775 * a - 1.291485548 * b, 3);
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s};
}
std::uint8_t enc8(double v) {
    const double c = std::min(1.0, std::max(0.0, v)), s = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
    return std::uint8_t(std::floor(s * 255 + 0.5));
}
double edge8(double v) {
    const double c = std::min(1.0, std::max(0.0, v)), s = (c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055) * 255;
    return std::abs(s - std::floor(s) - 0.5);
}
double dist3(const V3& a, const V3& b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); }
// The plain decision and the margin of its choice (a rounding edge, or the gap to the runner-up).
int plain(const HystPlan& p, const V3& lab, double& margin) {
    if (p.bands) {
        const double x = lab[0] * p.N;
        margin = std::abs(x - std::floor(x) - 0.5);
        return int(std::min(double(p.N), std::max(0.0, std::floor(x + 0.5))));
    }
    int best = 0;
    double bd = std::numeric_limits<double>::infinity(), second = bd;
    for (std::size_t k = 0; k < p.pal.size(); ++k) {
        const double d = dist3(lab, p.pal[k].lab);
        if (d < bd) { second = bd; bd = d; best = int(k); } else if (d < second) second = d;
    }
    margin = second - bd;
    return best;
}
bool keep(const HystPlan& p, const V3& lab, int prev, int pl, double& margin) {
    if (p.bands) {
        const double x = lab[0] * p.N, lo = prev - p.keepHalf - p.margin, hi = prev + p.keepHalf + p.margin;
        margin = std::min(std::abs(x - lo), std::abs(x - hi));
        return x >= lo && x <= hi;
    }
    const double dp = dist3(lab, p.pal[std::size_t(prev)].lab), db = dist3(lab, p.pal[std::size_t(pl)].lab);
    margin = std::abs(dp - (db + p.margin));
    return dp <= db + p.margin;
}
}  // namespace

HystPlan hystPreset(const std::string& name) {
    HystPlan p;
    if (name == "bands6") { p.bands = true; p.N = 6; p.margin = 0.3; }
    else if (name == "pico8") { p.bands = false; p.margin = 0.03; p.pal = labPalette("pico8"); }
    else throw std::invalid_argument("hysteresis: unknown preset " + name);
    p.persistSec = 0.2; p.fps = 24; p.gain = 2.4; p.depthTol = 0.03;
    p.K = std::max(1, int(std::floor(p.persistSec * p.fps + 0.5)));
    return p;
}

HystFrame hystStep(const HystPlan& p, int w, int h, const std::vector<float>& rgba, const std::vector<float>& mv, const std::vector<float>& dA,
                   const std::vector<float>& dist, HystState& state) {
    const std::size_t n = std::size_t(w) * h;
    HystFrame f{std::vector<std::int32_t>(n), std::vector<std::uint8_t>(n * 4), std::vector<double>(n, 1e30)};
    HystState next{std::vector<std::int32_t>(n), std::vector<std::int32_t>(n, 0), dist};
    const bool hasState = !state.idx.empty(), motion = !mv.empty();
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + std::size_t(x), o = i * 4;
        const V3 lab = linearToOklab(shoulder(rgba[o], p.gain), shoulder(rgba[o + 1], p.gain), shoulder(rgba[o + 2], p.gain));
        double m = 0;
        const int pl = plain(p, lab, m);
        double margin = m;
        int d = pl;
        if (hasState) {
            const double mx = motion ? mv[i * 2] : 0, my = motion ? mv[i * 2 + 1] : 0;
            const double px = std::floor(x + 0.5 - mx), py = std::floor(y + 0.5 - my);
            if (px >= 0 && py >= 0 && px < w && py < h) {
                const std::size_t j = std::size_t(py) * w + std::size_t(px);
                const double expect = motion ? dA[i] : dist[i], gap = std::abs(double(state.dist[j]) - expect), tol = p.depthTol * expect;
                margin = std::min(margin, std::abs(gap - tol));
                if (gap <= tol) {
                    const int prev = state.idx[j];
                    double km = 1e30;
                    if (prev != pl && keep(p, lab, prev, pl, km)) {
                        const int c = state.cnt[j] + 1;
                        if (c < p.K) { d = prev; next.cnt[i] = c; }
                    }
                    margin = std::min(margin, km);
                }
            }
        }
        f.idx[i] = next.idx[i] = d;
        V3 rgb;
        if (p.bands) {
            const double Lq = double(d) / p.N, s = lab[0] > 1e-9 ? Lq / lab[0] : 0;
            rgb = fromOklab(Lq, lab[1] * s, lab[2] * s);
        } else {
            const V3& q = p.pal[std::size_t(d)].lab;
            rgb = fromOklab(q[0], q[1], q[2]);
        }
        for (std::size_t k = 0; k < 3; ++k) { f.rgba[o + k] = enc8(rgb[k]); margin = std::min(margin, edge8(rgb[k]) / 255); }
        f.rgba[o + 3] = 255;
        f.margin[i] = margin;
    }
    state = std::move(next);
    return f;
}

}  // namespace raw::style
