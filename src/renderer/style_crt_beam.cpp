// The physical CRT's tube geometry, mask, beams and phosphor persistence, in the order of
// web/shaders/crt/geometry.mjs and beam.mjs, with erf and gaussMass of web/shaders/common.mjs.
// See raw/renderer/style_crt.hpp.
#include "raw/renderer/style_crt.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
double erfAS(double x) {
    const double s = x < 0 ? -1 : 1, a = std::abs(x), t = 1 / (1 + 0.3275911 * a);
    const double y = 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * std::exp(-a * a);
    return s * y;
}
double gaussMass(double a, double b, double mu, double sigma) {
    const double k = 1 / (sigma * 1.4142135623730951);
    return 0.5 * (erfAS((b - mu) * k) - erfAS((a - mu) * k));
}
std::array<double, 2> faceAt(const CrtPlan& plan, double px, double py) {
    const CrtParams& p = plan.p;
    const double X = (px - plan.outW / 2.0) * (plan.viewW / plan.outW) + p.centre[0];
    const double Y = (py - plan.outH / 2.0) * (plan.viewH / plan.outH) + p.centre[1];
    const double D = plan.distance, A = (X * X) / (2 * p.rx) + (Y * Y) / (2 * p.ry);
    const double t = (2 * D) / (D + std::sqrt(std::max(0.0, D * D - 4 * A * D)));
    return {t * X, t * Y};
}
double faceAlpha(const CrtPlan& plan, double sx, double sy, double hx, double hy) {
    const double r = plan.p.corner, qx = std::abs(sx) - (plan.faceW / 2 - r), qy = std::abs(sy) - (plan.faceH / 2 - r);
    const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    const double d = std::hypot(ox, oy) + std::min(std::max(qx, qy), 0.0) - r;
    return std::min(1.0, std::max(0.0, 0.5 - d / (2 * std::max(hx, hy))));
}
double tentCdf(double u, double a) {
    const double v = u / a;
    if (v <= -1) return 0;
    if (v <= 0) return 0.5 * (1 + v) * (1 + v);
    if (v < 1) return 1 - 0.5 * (1 - v) * (1 - v);
    return 1;
}
double pulseBox(double y0, double y1, double P, double w, double s) {
    const double n0 = std::floor((y0 - s) / P), a = y0 - s - n0 * P, b = y1 - s - n0 * P;
    auto F = [&](double x) { return std::floor(x / P) * w + std::min(std::max(x - std::floor(x / P) * P, 0.0), w); };
    return (F(b) - F(a)) / (y1 - y0);
}
double dampen(const CrtPlan& plan, double cov, double sy, double hy) {
    const CrtParams& m = plan.p;
    if (m.maskType != 1) return cov;
    for (double d : m.dampers) {
        const double dc = (d - 0.5) * plan.faceH, lo = std::max(sy - hy, dc - m.damperWidth / 2), hi = std::min(sy + hy, dc + m.damperWidth / 2);
        if (hi > lo) cov *= 1 - (hi - lo) / (2 * hy);
    }
    return cov;
}
double stripeMask(const CrtPlan& plan, double sx, double sy, double hx, double hy, int c) {
    const CrtParams& m = plan.p;
    const double P = m.pitch, w = (m.fill * P) / 3, s = (c * P) / 3 + ((1 - m.fill) * P) / 6, a = 2 * hx;
    const double m0 = std::floor((sx - a - s - w) / P), m1 = std::floor((sx + a - s) / P);
    if (m1 - m0 > 32) return crtMaskMean(m) * 1;
    double cov = 0;
    for (long long k = (long long)m0; k <= (long long)m1; ++k) {
        const double x0 = double(k) * P + s - sx, xw = tentCdf(x0 + w, a) - tentCdf(x0, a);
        if (xw <= 0) continue;
        double yw = 1;
        if (m.maskType == 2) { const double Py = m.slotHeight + m.slotGap; yw = pulseBox(sy - hy, sy + hy, Py, m.slotHeight, double(k & 1) * Py * 0.5); }
        cov += xw * yw;
    }
    return dampen(plan, cov, sy, hy);
}
std::array<double, 3> deltaMask(const CrtParams& m, double sx, double sy, double hx, double hy) {
    const double d = m.pitch / std::sqrt(3.0), r = (m.fill * d) / 2, rowH = (d * std::sqrt(3.0)) / 2;
    std::array<double, 3> out{0, 0, 0};
    const double aa = std::min(0.75 * (hx + hy), r);
    for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
        const double x = sx + ((i + 0.5) / 4 - 0.5) * 3 * hx, y = sy + ((j + 0.5) / 4 - 0.5) * 3 * hy;
        const long long lj = (long long)std::floor(y / rowH);
        for (int dj = 0; dj <= 1; ++dj) {
            const long long jj = lj + dj, li = (long long)std::floor(x / d - double(jj) / 2);
            for (int di = 0; di <= 1; ++di) {
                const long long ii = li + di;
                const double cx = (double(ii) + double(jj) / 2) * d, cy = double(jj) * rowH;
                const double e = std::min(1.0, std::max(0.0, 0.5 + (r - std::hypot(x - cx, y - cy)) / aa));
                if (e > 0) out[std::size_t((((ii - jj) % 3) + 3) % 3)] += e / 16;
            }
        }
    }
    const double t = std::min(1.0, std::max(0.0, (hx / m.pitch - 0.7) / 0.3)), b = t * t * (3 - 2 * t), mean = crtMaskMean(m);
    for (double& v : out) v = v + (mean - v) * b;
    return out;
}
double pixelStripeMask(const CrtPlan& plan, int px, int py, double sy, double hy, int c) {
    const CrtParams& m = plan.p;
    const double P = m.pitchPx, w = (m.fill * P) / 3, s = (c * P) / 3 + ((1 - m.fill) * P) / 6;
    const long long k0 = (long long)std::floor((px - s - w) / P), k1 = (long long)std::floor((px + 1 - s) / P);
    double cov = 0;
    for (long long k = k0; k <= k1; ++k) {
        const double x0 = std::max(double(px), double(k) * P + s), x1 = std::min(double(px) + 1, double(k) * P + s + w);
        if (x1 <= x0) continue;
        double yw = 1;
        if (m.maskType == 2) { const double u = P / m.pitch, Py = (m.slotHeight + m.slotGap) * u; yw = pulseBox(py, py + 1, Py, m.slotHeight * u, double(k & 1) * Py * 0.5); }
        cov += (x1 - x0) * yw;
    }
    return dampen(plan, cov, sy, hy);
}
double gunCurrent(const CrtPlan& plan, double v) {
    const double x = std::max(plan.p.contrast * v + plan.p.brightness, 0.0);
    return std::pow(x, plan.p.gamma);
}
double spotSigma(const CrtPlan& plan, double I) { const CrtParams& s = plan.p; return plan.lineMm * (s.spotLo + (s.spotHi - s.spotLo) * std::pow(std::min(I, 1.5), s.spotExp)); }
std::array<double, 2> convergence(const CrtPlan& plan, int c, double sx, double sy) {
    const CrtParams& cv = plan.p;
    const double k = cv.radial / (plan.faceW / 2);
    if (c == 0) return {cv.convR[0] + k * sx, cv.convR[1] + k * sy};
    if (c == 2) return {cv.convB[0] - k * sx, cv.convB[1] - k * sy};
    return {0, 0};
}
double drive(const CrtPlan& plan, const std::vector<float>& sig, int c, int j, int k) {
    const std::size_t i = (std::size_t(j) * plan.N + std::size_t(k)) * 4;
    if (plan.screen.mono) return 0.299 * sig[i] + 0.587 * sig[i + 1] + 0.114 * sig[i + 2];
    return sig[i + std::size_t(c)];
}
double deposit(const CrtPlan& plan, const std::vector<float>& sig, int c, double rx, double ry, double hx, double hy, int field) {
    const int N = plan.N, lines = plan.lines;
    const double lineMm = plan.lineMm, rasterW = plan.rasterW, rasterH = plan.rasterH, dx = rasterW / N;
    const double smax = spotSigma(plan, 1.5), reachY = 3 * smax + hy, reachX = 3 * smax + hx;
    const double yl = (ry + rasterH / 2) / lineMm - 0.5;
    const int j0 = std::max(0, int(std::floor(yl - reachY / lineMm))), j1 = std::min(lines - 1, int(std::ceil(yl + reachY / lineMm)));
    const double kc = (rx + rasterW / 2) / dx - 0.5;
    const int k0 = std::max(0, int(std::floor(kc - reachX / dx))), k1 = std::min(N - 1, int(std::ceil(kc + reachX / dx)));
    double e = 0;
    for (int j = j0; j <= j1; ++j) {
        if (plan.p.interlace && (j & 1) != (field & 1)) continue;
        const double yj = -rasterH / 2 + (j + 0.5) * lineMm;
        for (int k = k0; k <= k1; ++k) {
            const double I = gunCurrent(plan, drive(plan, sig, c, j, k));
            if (I <= 0) continue;
            const double s = spotSigma(plan, I), xk = -rasterW / 2 + (k + 0.5) * dx;
            e += I * gaussMass(rx - hx, rx + hx, xk, s) * gaussMass(ry - hy, ry + hy, yj, s);
        }
    }
    return (e * dx * lineMm) / (4 * hx * hy);
}
}  // namespace

std::array<double, 4> crtFootprint(const CrtPlan& plan, int px, int py) {
    const auto c = faceAt(plan, px + 0.5, py + 0.5), xp = faceAt(plan, px + 1, py + 0.5), xm = faceAt(plan, px, py + 0.5);
    const auto yp = faceAt(plan, px + 0.5, py + 1), ym = faceAt(plan, px + 0.5, py);
    const double hx = 0.5 * (std::abs(xp[0] - xm[0]) + std::abs(yp[0] - ym[0])), hy = 0.5 * (std::abs(xp[1] - xm[1]) + std::abs(yp[1] - ym[1]));
    return {c[0], c[1], std::max(hx, 1e-4), std::max(hy, 1e-4)};
}

std::array<double, 3> crtMaskCover(const CrtPlan& plan, double sx, double sy, double hx, double hy, int px, int py) {
    const CrtParams& m = plan.p;
    if (m.maskType == 0 || plan.screen.mono) return {1, 1, 1};
    const double mean = crtMaskMean(m), mix = m.mix;
    const bool locked = m.pixelLock && m.maskType != 3;
    std::array<double, 3> cov{};
    if (m.maskType == 3) cov = deltaMask(m, sx, sy, hx, hy);
    else for (int c = 0; c < 3; ++c) cov[std::size_t(c)] = locked ? pixelStripeMask(plan, px, py, sy, hy, c) : stripeMask(plan, sx, sy, hx, hy, c);
    for (double& v : cov) v = 1 - mix + (mix * v) / mean;
    return cov;
}

std::vector<float> crtBeam(const CrtPlan& plan, const std::vector<float>& sig, std::vector<float>& hist, int field) {
    const int W = plan.outW, H = plan.outH;
    std::vector<float> em(std::size_t(W) * H * 4, 0.0f);
    const double T = plan.field;
    for (int py = 0; py < H; ++py) for (int px = 0; px < W; ++px) {
        const auto fp = crtFootprint(plan, px, py);
        const double sx = fp[0], sy = fp[1], hx = fp[2], hy = fp[3];
        const std::size_t i = (std::size_t(py) * W + px) * 4, h = (std::size_t(py) * W + px) * 8;
        const double alpha = faceAlpha(plan, sx, sy, hx, hy);
        em[i + 3] = float(alpha);
        const auto cover = crtMaskCover(plan, sx, sy, hx, hy, px, py);
        const double t0 = std::min(1.0, std::max(0.0, (sy + plan.rasterH / 2) / plan.rasterH)) * T;
        const bool lit = alpha > 0 && std::abs(sx) < plan.rasterW / 2 + 3 * hx && std::abs(sy) < plan.rasterH / 2 + 3 * hy;
        const double mono = plan.screen.mono && lit ? deposit(plan, sig, 0, sx, sy, hx, hy, field) : 0;
        for (int c = 0; c < 3; ++c) {
            double E = 0;
            if (lit && plan.screen.mono) E = mono * cover[std::size_t(c)];
            else if (lit) {
                const auto o = convergence(plan, c, sx, sy);
                E = deposit(plan, sig, c, sx - o[0], sy - o[1], hx, hy, field) * cover[std::size_t(c)];
            }
            const auto& d = plan.decay[std::size_t(c)];
            const double w = d[0], e1 = d[1], e2 = d[2], t1 = d[3], t2 = d[4];
            const double n1 = std::exp(-(T - t0) / t1), n2 = std::exp(-(T - t0) / t2);
            const double R1 = hist[h + std::size_t(c)], R2 = hist[h + 4 + std::size_t(c)];
            em[i + std::size_t(c)] = float(R1 * (1 - e1) + R2 * (1 - e2) + E * (w * (1 - n1) + (1 - w) * (1 - n2)));
            hist[h + std::size_t(c)] = float(R1 * e1 + E * w * n1);
            hist[h + 4 + std::size_t(c)] = float(R2 * e2 + E * (1 - w) * n2);
        }
    }
    return em;
}

}  // namespace raw::style
