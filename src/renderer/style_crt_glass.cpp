// The physical CRT's faceplate: the halo, the phosphor colour, the room reflected in the glass,
// the bezel and the display encode, in the order of web/shaders/crt/glass.mjs and roomEnv of
// geometry.mjs. See raw/renderer/style_crt.hpp.
#include "raw/renderer/style_crt.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
std::array<int, 2> haloSize(const CrtPlan& plan) { const int q = plan.halo.q; return {(plan.outW + q - 1) / q, (plan.outH + q - 1) / q}; }
std::vector<float> haloDown(const CrtPlan& plan, const std::vector<float>& em) {
    const int q = plan.halo.q, W = plan.outW, H = plan.outH;
    const auto [gw, gh] = haloSize(plan);
    std::vector<float> g(std::size_t(gw) * gh * 4, 0.0f);
    for (int gy = 0; gy < gh; ++gy) for (int gx = 0; gx < gw; ++gx) {
        double r = 0, gg = 0, b = 0;
        int n = 0;
        for (int y = gy * q; y < std::min(H, gy * q + q); ++y) for (int x = gx * q; x < std::min(W, gx * q + q); ++x) {
            const std::size_t i = (std::size_t(y) * W + x) * 4;
            r += em[i]; gg += em[i + 1]; b += em[i + 2]; n++;
        }
        const std::size_t o = (std::size_t(gy) * gw + gx) * 4;
        g[o] = float(r / n); g[o + 1] = float(gg / n); g[o + 2] = float(b / n);
    }
    return g;
}
std::vector<float> haloConv(const CrtPlan& plan, const std::vector<float>& g) {
    const int R = plan.halo.R, S = 2 * R + 1;
    const auto [gw, gh] = haloSize(plan);
    std::vector<float> o(std::size_t(gw) * gh * 4, 0.0f);
    for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
        double r = 0, gg = 0, b = 0;
        for (int dy = -R; dy <= R; ++dy) {
            const int yy = y + dy;
            if (yy < 0 || yy >= gh) continue;
            for (int dx = -R; dx <= R; ++dx) {
                const int xx = x + dx;
                if (xx < 0 || xx >= gw) continue;
                const double w = plan.halo.weights[std::size_t((dy + R) * S + dx + R)];
                const std::size_t i = (std::size_t(yy) * gw + xx) * 4;
                r += w * g[i]; gg += w * g[i + 1]; b += w * g[i + 2];
            }
        }
        const std::size_t i = (std::size_t(y) * gw + x) * 4;
        o[i] = float(r); o[i + 1] = float(gg); o[i + 2] = float(b);
    }
    return o;
}
std::array<double, 3> haloAt(const CrtPlan& plan, const std::vector<float>& h, int px, int py) {
    const int q = plan.halo.q;
    const auto [gw, gh] = haloSize(plan);
    const double fx = std::min(double(gw - 1), std::max(0.0, (px + 0.5) / q - 0.5)), fy = std::min(double(gh - 1), std::max(0.0, (py + 0.5) / q - 0.5));
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy)), x1 = std::min(gw - 1, x0 + 1), y1 = std::min(gh - 1, y0 + 1);
    const double tx = fx - x0, ty = fy - y0;
    auto at = [&](int x, int y, int c) { return double(h[(std::size_t(y) * gw + x) * 4 + std::size_t(c)]); };
    std::array<double, 3> o{};
    for (int c = 0; c < 3; ++c) o[std::size_t(c)] = (at(x0, y0, c) * (1 - tx) + at(x1, y0, c) * tx) * (1 - ty) + (at(x0, y1, c) * (1 - tx) + at(x1, y1, c) * tx) * ty;
    return o;
}
double roomEnv(double dx, double dy, double dz) {
    const double wx = -0.38, wy = -0.33, wz = -0.864, c = dx * wx + dy * wy + dz * wz;
    const double t = std::min(1.0, std::max(0.0, (c - 0.975) / 0.02));
    return 0.04 + t * t * (3 - 2 * t);
}
double fresnelAir(double n, double theta) {
    const double s = std::sin(theta) / n, ci = std::cos(theta), ct = std::sqrt(1 - s * s);
    const double rs = (ci - n * ct) / (ci + n * ct), rp = (ct - n * ci) / (ct + n * ci);
    return 0.5 * (rs * rs + rp * rp);
}
double glassReflection(const CrtPlan& plan, double sx, double sy) {
    const double rx = plan.p.rx, ry = plan.p.ry, D = plan.distance, sz = (sx * sx) / (2 * rx) + (sy * sy) / (2 * ry);
    double ix = sx, iy = sy, iz = sz + D;
    const double il = std::hypot(ix, iy, iz);
    ix /= il; iy /= il; iz /= il;
    double nx = sx / rx, ny = sy / ry, nz = -1;
    const double nl = std::hypot(nx, ny, nz);
    nx /= nl; ny /= nl; nz /= nl;
    const double d = ix * nx + iy * ny + iz * nz;
    const double fr = fresnelAir(plan.p.glassN, std::acos(std::min(1.0, std::abs(d))));
    return fr * roomEnv(ix - 2 * d * nx, iy - 2 * d * ny, iz - 2 * d * nz);
}
std::array<double, 3> gamutFit(std::array<double, 3> rgb) {
    const double Y = std::max(0.0, 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]);
    const double m = std::min({rgb[0], rgb[1], rgb[2]});
    if (m >= 0) return rgb;
    const double t = Y / (Y - m);
    for (double& v : rgb) v = Y + t * (v - Y);
    return rgb;
}
}  // namespace

std::vector<float> crtGlass(const CrtPlan& plan, const std::vector<float>& em) {
    const int W = plan.outW, H = plan.outH;
    const std::vector<float> halo = haloConv(plan, haloDown(plan, em));
    const double T = plan.p.transmission;
    const auto& M = plan.screen.matrix;
    std::vector<float> img(std::size_t(W) * H * 4, 0.0f);
    for (int py = 0; py < H; ++py) for (int px = 0; px < W; ++px) {
        const std::size_t i = (std::size_t(py) * W + px) * 4;
        const double a = em[i + 3];
        const auto fp = crtFootprint(plan, px, py);
        const auto hl = haloAt(plan, halo, px, py);
        std::array<double, 3> gun{};
        for (int c = 0; c < 3; ++c) gun[std::size_t(c)] = plan.halo.direct * em[i + std::size_t(c)] + hl[std::size_t(c)];
        const auto rgb = gamutFit({M[0] * gun[0] + M[1] * gun[1] + M[2] * gun[2], M[3] * gun[0] + M[4] * gun[1] + M[5] * gun[2], M[6] * gun[0] + M[7] * gun[1] + M[8] * gun[2]});
        const double black = plan.p.ambient * T, spec = plan.p.reflection * glassReflection(plan, fp[0], fp[1]);
        for (int c = 0; c < 3; ++c) img[i + std::size_t(c)] = float(a * (rgb[std::size_t(c)] + black + spec) + (1 - a) * plan.p.bezel[c]);
        img[i + 3] = 1;
    }
    return img;
}

std::uint8_t crtEncode8(double v, double exposure) {
    double l = std::max(0.0, v * exposure);
    const double k = 0.8;
    if (l > k) l = k + (1 - k) * std::tanh((l - k) / (1 - k));
    const double s = (l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055) * 255;
    const double r = std::floor(s + 0.5);
    return std::uint8_t(std::min(255.0, std::max(0.0, r)));
}

CrtFrame crtFrame(const CrtPlan& plan, const std::vector<float>& src, std::vector<float>& hist, int f) {
    CrtFrame o;
    o.sig = crtSignal(plan, src, f);
    o.em = crtBeam(plan, o.sig, hist, f);
    o.img = crtGlass(plan, o.em);
    return o;
}

}  // namespace raw::style
