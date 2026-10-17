// The painterly shader's abstraction passes and the frame: see raw/renderer/style_paint.hpp.
// Operations are in the order of web/shaders/paint/abstract.mjs and paint.mjs; a float here is a
// Float32Array element there.
#include "raw/renderer/style_paint.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
std::vector<float> paintRelief(const PaintPlan& plan, const std::vector<float>& akS, const std::vector<float>& H, const std::vector<float>& T, const PaintCanvas* cv);   // style_paint_compose.cpp
std::vector<float> paintCompose(const PaintPlan& plan, const std::vector<float>& akLat, const std::vector<float>& H, const std::vector<float>& Rl, const PaintCanvas* cv);
double paintCanvasNoise(const PaintPlan& plan, const PaintCanvas* cv, PaintNoise kind, double x, double y, double m);
namespace {
constexpr double kPi = 3.141592653589793;
std::array<double, 3> majorVec(double e, double f, double g) {
    const double tr = e + g, l1 = (tr + std::sqrt((e - g) * (e - g) + 4 * f * f)) / 2;
    const double ax = f, ay = l1 - e, bx = l1 - g, by = f, na = std::hypot(ax, ay), nb = std::hypot(bx, by);
    return na >= nb ? std::array<double, 3>{ax, ay, na} : std::array<double, 3>{bx, by, nb};
}
std::array<double, 3> flow(double E, double F, double G) {
    const double tr = E + G, dsc = std::sqrt((E - G) * (E - G) + 4 * F * F), l1 = (tr + dsc) / 2, l2 = (tr - dsc) / 2;
    const double sharp = std::min(1.0, std::max(0.0, (tr - 1e-7) / 9e-7)), A = tr > 1e-12 ? ((l1 - l2) / (l1 + l2)) * sharp * sharp * (3 - 2 * sharp) : 0;
    const double t = std::min(1.0, std::max(0.0, (A - 0.02) / 0.08)), k = t * t * (3 - 2 * t), inv = tr > 1e-12 ? 1 / tr : 0;
    const double e = k * E * inv + (1 - k) * 0.5, f = k * F * inv - (1 - k) * 0.5, g = k * G * inv + (1 - k) * 0.5;
    const auto m = majorVec(e, f, g);
    if (m[2] <= 1e-12) return {0.7071067811865476, 0.7071067811865476, A};
    return {-m[1] / m[2], m[0] / m[2], A};
}
std::array<double, 3> shoulderRGB(double x0, double x1, double x2, double k) {
    const double r = std::max(0.0, x0), g = std::max(0.0, x1), b = std::max(0.0, x2), m = std::max({r, g, b}), t = m * k;
    const double gain = t < 1e-3 ? k * (1 - t / 2 + (t * t) / 6) : (1 - std::exp(-t)) / m;
    return {r * gain, g * gain, b * gain};
}
std::vector<float> blur4(const PaintPlan& plan, const std::vector<float>& a, bool alongY) {
    const int w = plan.w, h = plan.h, r = int(plan.tw.size() - 1) / 2;
    std::vector<float> o(a.size(), 0.0f);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 4; ++c) {
        double s = 0;
        for (int k = -r; k <= r; ++k) {
            const int xx = alongY ? x : std::min(w - 1, std::max(0, x + k)), yy = alongY ? std::min(h - 1, std::max(0, y + k)) : y;
            s += double(plan.tw[std::size_t(k + r)]) * a[(std::size_t(yy) * w + std::size_t(xx)) * 4 + std::size_t(c)];
        }
        o[(std::size_t(y) * w + std::size_t(x)) * 4 + std::size_t(c)] = float(s);
    }
    return o;
}
}  // namespace

std::vector<float> runPaint(const PaintPlan& plan, const std::vector<float>& scene, const PaintCanvas* cv) {
    const int w = plan.w, h = plan.h;
    const PaintParams& p = plan.p;
    const std::size_t n = std::size_t(w) * h;
    // prep
    std::vector<float> s(n * 4), lat(n * 8, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const auto D = shoulderRGB(scene[i * 4], scene[i * 4 + 1], scene[i * 4 + 2], p.exposure);
        const V3 enc{linearToSrgb(D[0]), linearToSrgb(D[1]), linearToSrgb(D[2])};
        const V4 c = lutConc(enc);
        const V3 dec = kmDecode(c);
        s[i * 4] = float(enc[0]); s[i * 4 + 1] = float(enc[1]); s[i * 4 + 2] = float(enc[2]); s[i * 4 + 3] = float(0.2126 * D[0] + 0.7152 * D[1] + 0.0722 * D[2]);
        for (int k = 0; k < 4; ++k) lat[i * 8 + std::size_t(k)] = float(c[std::size_t(k)]);
        for (int k = 0; k < 3; ++k) lat[i * 8 + 4 + std::size_t(k)] = float(D[std::size_t(k)] - dec[std::size_t(k)]);
    }
    // tensor
    std::vector<float> T(n * 4, 0.0f);
    auto at = [&](int x, int y, int c) { return double(s[(std::size_t(std::min(h - 1, std::max(0, y))) * w + std::size_t(std::min(w - 1, std::max(0, x)))) * 4 + std::size_t(c)]); };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        double E = 0, F = 0, G = 0;
        for (int c = 0; c < 3; ++c) {
            const double gx = (at(x + 1, y - 1, c) + 2 * at(x + 1, y, c) + at(x + 1, y + 1, c) - at(x - 1, y - 1, c) - 2 * at(x - 1, y, c) - at(x - 1, y + 1, c)) / 4;
            const double gy = (at(x - 1, y + 1, c) + 2 * at(x, y + 1, c) + at(x + 1, y + 1, c) - at(x - 1, y - 1, c) - 2 * at(x, y - 1, c) - at(x + 1, y - 1, c)) / 4;
            E += gx * gx; F += gx * gy; G += gy * gy;
        }
        const std::size_t o = (std::size_t(y) * w + std::size_t(x)) * 4;
        T[o] = float(E); T[o + 1] = float(F); T[o + 2] = float(G);
    }
    T = blur4(plan, blur4(plan, T, false), true);
    // anisotropic Kuwahara
    const int N = 8;
    const double r = p.radius, q = p.q, zeta = p.zeta, eta = (zeta + std::cos(kPi / N)) / std::pow(std::sin(kPi / N), 2);
    std::vector<float> akLat(n * 8, 0.0f), akS(n * 4, 0.0f);
    std::vector<double> acc(std::size_t(N) * 14);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t pi = std::size_t(y) * w + std::size_t(x);
        const auto fl = flow(T[pi * 4], T[pi * 4 + 1], T[pi * 4 + 2]);
        const double tx = fl[0], ty = fl[1], A = fl[2];
        const double a = r * (1 + A), b = r / (1 + A);
        const int ex = int(std::ceil(std::sqrt(a * a * tx * tx + b * b * ty * ty))), ey = int(std::ceil(std::sqrt(a * a * ty * ty + b * b * tx * tx)));
        std::fill(acc.begin(), acc.end(), 0.0);
        for (int dy = -ey; dy <= ey; ++dy) for (int dx = -ex; dx <= ex; ++dx) {
            const double u = (tx * dx + ty * dy) / a, v = (-ty * dx + tx * dy) / b, rr = u * u + v * v;
            if (rr > 1) continue;
            const std::size_t qi = std::size_t(std::min(h - 1, std::max(0, y + dy))) * w + std::size_t(std::min(w - 1, std::max(0, x + dx)));
            const double g = std::exp(-2 * rr);
            for (int k = 0; k < N; ++k) {
                const double ang = (-2 * kPi * k) / N, xk = u * std::cos(ang) - v * std::sin(ang), yk = u * std::sin(ang) + v * std::cos(ang);
                const double z = xk + zeta - eta * yk * yk;
                if (z <= 0) continue;
                const double wk = z * z * g;
                const std::size_t o = std::size_t(k) * 14;
                acc[o] += wk;
                for (int c = 0; c < 7; ++c) acc[o + 1 + std::size_t(c)] += wk * lat[qi * 8 + std::size_t(c)];
                for (int c = 0; c < 3; ++c) { const double sv = s[qi * 4 + std::size_t(c)]; acc[o + 8 + std::size_t(c)] += wk * sv; acc[o + 11 + std::size_t(c)] += wk * sv * sv; }
            }
        }
        double wsum = 0, L[7] = {0, 0, 0, 0, 0, 0, 0}, S3[3] = {0, 0, 0};
        for (int k = 0; k < N; ++k) {
            const std::size_t o = std::size_t(k) * 14;
            const double Wk = acc[o];
            if (Wk <= 0) continue;
            double v2 = 0;
            for (int c = 0; c < 3; ++c) { const double m = acc[o + 8 + std::size_t(c)] / Wk; v2 += std::max(0.0, acc[o + 11 + std::size_t(c)] / Wk - m * m); }
            const double sd = std::sqrt(v2) * 255, alpha = 1 / (1 + std::pow(sd, q));
            wsum += alpha;
            for (int c = 0; c < 7; ++c) L[c] += (alpha * acc[o + 1 + std::size_t(c)]) / Wk;
            for (int c = 0; c < 3; ++c) S3[c] += (alpha * acc[o + 8 + std::size_t(c)]) / Wk;
        }
        for (int c = 0; c < 7; ++c) akLat[pi * 8 + std::size_t(c)] = float(L[c] / wsum);
        for (int c = 0; c < 3; ++c) akS[pi * 4 + std::size_t(c)] = float(S3[c] / wsum);
        akS[pi * 4 + 3] = float(A);
    }
    // stroke
    const int SL = int(p.strokeLen);
    std::vector<float> H(n * 2, 0.0f);
    auto dirAt = [&](double px, double py) {
        const double fx = std::min(double(w - 1), std::max(0.0, px)), fy = std::min(double(h - 1), std::max(0.0, py));
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy)), x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
        const double tx = fx - x0, ty = fy - y0;
        double t[3];
        for (int c = 0; c < 3; ++c) {
            auto TT = [&](int xx, int yy) { return double(T[(std::size_t(yy) * w + std::size_t(xx)) * 4 + std::size_t(c)]); };
            t[c] = (TT(x0, y0) * (1 - tx) + TT(x1, y0) * tx) * (1 - ty) + (TT(x0, y1) * (1 - tx) + TT(x1, y1) * tx) * ty;
        }
        return flow(t[0], t[1], t[2]);
    };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        double acc1 = paintCanvasNoise(plan, cv, PaintNoise::Bristle, x, y, 0.5) * (SL + 1), ids = paintCanvasNoise(plan, cv, PaintNoise::StrokeId, x, y, 0.5) * (SL + 1), ws = SL + 1;
        for (int sg = 0; sg < 2; ++sg) {
            const double sgn = sg == 0 ? 1 : -1;
            double px = x, py = y;
            const auto d0 = dirAt(x, y);
            double tx = d0[0], ty = d0[1], cont = 1;
            tx *= sgn; ty *= sgn;
            for (int k = 1; k <= SL; ++k) {
                px += tx; py += ty;
                const auto nd = dirAt(px, py);
                const double d = nd[0] * tx + nd[1] * ty;
                tx = d < 0 ? -nd[0] : nd[0]; ty = d < 0 ? -nd[1] : nd[1];
                const double a = std::min(1.0, std::max(0.0, (std::abs(d) - 0.05) / 0.25));
                cont *= a * a * (3 - 2 * a);
                const double wk = (SL + 1 - k) * cont;
                acc1 += wk * paintCanvasNoise(plan, cv, PaintNoise::Bristle, px, py, 0.5); ids += wk * paintCanvasNoise(plan, cv, PaintNoise::StrokeId, px, py, 0.5); ws += wk;
            }
        }
        H[(std::size_t(y) * w + std::size_t(x)) * 2] = float(acc1 / ws); H[(std::size_t(y) * w + std::size_t(x)) * 2 + 1] = float(ids / ws);
    }
    const std::vector<float> Rl = paintRelief(plan, akS, H, T, cv);
    return paintCompose(plan, akLat, H, Rl, cv);
}

}  // namespace raw::style
