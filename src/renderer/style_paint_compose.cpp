// The painterly shader's relief and compose passes and the canvas sampler: see
// raw/renderer/style_paint.hpp. Operations are in the order of web/shaders/paint/compose.mjs and
// paint.mjs (offsetCanvas, advectedCanvas) and canvas.mjs (blendNoise).
#include "raw/renderer/style_image.hpp"
#include "raw/renderer/style_paint.hpp"
#include "data/pigments.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
double smooth(double a, double b, double x) { const double t = clampd((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
// Bilinear read of ch channels of a stride-wide f32 buffer at a continuous pixel position.
void sample(const std::vector<float>& buf, int ch, int w, int h, double x, double y, int stride, double* o) {
    const double fx = clampd(x, 0, w - 1), fy = clampd(y, 0, h - 1);
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy)), x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
    const double tx = fx - x0, ty = fy - y0;
    for (int c = 0; c < ch; ++c) {
        auto at = [&](int xx, int yy) { return double(buf[(std::size_t(yy) * w + std::size_t(xx)) * std::size_t(stride) + std::size_t(c)]); };
        o[c] = (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
    }
}
V3 glaze(const double c[4], double X, double Rg) {
    V3 out{0, 0, 0};
    for (int wl = 0; wl < kWavelengths; ++wl) {
        double K = 0, S = 0;
        for (int i = 0; i < 4; ++i) { K += c[i] * data::kPigmentK[wl][i]; S += c[i] * data::kPigmentS[wl][i]; }
        S = std::max(S, 1e-6);
        const double a = 1 + K / S, b = std::sqrt(a * a - 1), z = std::min(b * S * X, 30.0), sh = std::sinh(z), chh = std::cosh(z), den = a * sh + b * chh;
        const double R = sh / den, T = b / den, Rt = R + (T * T * Rg) / (1 - R * Rg);
        for (int k = 0; k < 3; ++k) out[std::size_t(k)] += data::kPigmentW[wl][k] * Rt;
    }
    return out;
}
double xdog(const PaintPlan& plan, const std::vector<float>& s, int x, int y) {
    const int w = plan.w, h = plan.h;
    const double sg = plan.p.lineSigma, k = 1.6;
    const int R = int(std::ceil(3 * sg * k));
    double a = 0, b = 0, wa = 0, wb = 0;
    for (int dy = -R; dy <= R; ++dy) for (int dx = -R; dx <= R; ++dx) {
        const int xx = int(clampd(x + dx, 0, w - 1)), yy = int(clampd(y + dy, 0, h - 1));
        const std::size_t i = (std::size_t(yy) * w + std::size_t(xx)) * 4;
        const double l = 0.2126 * s[i] + 0.7152 * s[i + 1] + 0.0722 * s[i + 2], r2 = dx * dx + dy * dy;
        const double g1 = std::exp(-r2 / (2 * sg * sg)), g2 = std::exp(-r2 / (2 * sg * sg * k * k));
        a += g1 * l; wa += g1; b += g2 * l; wb += g2;
    }
    const double D = a / wa - plan.p.xdogWeight * (b / wb);
    return D >= -0.004 ? 1 : 1 + std::tanh(plan.p.lineSharp * (D + 0.004));
}
}  // namespace

double paintCanvasNoise(const PaintPlan& plan, const PaintCanvas* cv, PaintNoise kind, double x, double y, double m) {
    if (!cv) return paintNoise(kind, x + plan.p.canvasOffset[0], y + plan.p.canvasOffset[1]);
    const auto wts = paintLayerWeights(*cv);
    const double fx = std::min(double(cv->w - 1), std::max(0.0, x)), fy = std::min(double(cv->h - 1), std::max(0.0, y));
    double c[4];
    sample(cv->C, 4, cv->w, cv->h, fx, fy, 4, c);
    if (wts[1] == 0) return paintNoise(kind, c[0], c[1]);
    if (wts[0] == 0) return paintNoise(kind, c[2], c[3]);
    const double n1 = paintNoise(kind, c[0], c[1]) - m, n2 = paintNoise(kind, c[2], c[3]) - m;
    return m + (wts[0] * n1 + wts[1] * n2) / std::sqrt(wts[0] * wts[0] + wts[1] * wts[1]);
}

std::vector<float> paintRelief(const PaintPlan& plan, const std::vector<float>& akS, const std::vector<float>& H, const std::vector<float>& T, const PaintCanvas* cv) {
    const int w = plan.w, h = plan.h;
    const PaintParams& p = plan.p;
    std::vector<float> out(std::size_t(w) * h * 4, 0.0f);
    const double lc[3] = {-0.5, -0.6, 0.62}, ll = std::hypot(lc[0], lc[1], lc[2]), L[3] = {lc[0] / ll, lc[1] / ll, lc[2] / ll};
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + std::size_t(x);
        const double line = p.lines > 0 ? xdog(plan, akS, x, y) : 1;
        auto hs = [&](int dx, int dy) {
            const int xx = int(clampd(x + dx, 0, w - 1)), yy = int(clampd(y + dy, 0, h - 1));
            return p.impasto * H[(std::size_t(yy) * w + std::size_t(xx)) * 2] + 0.12 * paintCanvasNoise(plan, cv, PaintNoise::Canvas, xx, yy, 0.5);
        };
        const double nx = -(hs(1, 0) - hs(-1, 0)) * 2.5, ny = -(hs(0, 1) - hs(0, -1)) * 2.5, nl = std::hypot(nx, ny, 1.0);
        const double n[3] = {nx / nl, ny / nl, 1 / nl};
        const double ndl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2], rz = 2 * ndl * n[2] - L[2];
        out[i * 4] = float(line); out[i * 4 + 1] = float(1 + 0.9 * (ndl - L[2]));
        out[i * 4 + 2] = float(p.gloss * std::pow(std::max(0.0, rz), 24) * 0.35);
        out[i * 4 + 3] = float(smooth(0.012, 0.05, std::sqrt(double(T[i * 4]) + T[i * 4 + 2])));
    }
    return out;
}

std::vector<float> paintCompose(const PaintPlan& plan, const std::vector<float>& akLat, const std::vector<float>& H, const std::vector<float>& Rl, const PaintCanvas* cv) {
    const int w = plan.w, h = plan.h;
    const PaintParams& p = plan.p;
    std::vector<float> img(std::size_t(w) * h * 4, 0.0f);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        double wx = 0, wy = 0;
        if (p.warp > 0) { wx = paintCanvasNoise(plan, cv, PaintNoise::WarpX, x, y, 0); wy = paintCanvasNoise(plan, cv, PaintNoise::WarpY, x, y, 0); }
        const double sx = x + wx * p.warp, sy = y + wy * p.warp;
        double lat[7];
        sample(akLat, 7, w, h, sx, sy, 8, lat);
        double c[4] = {lat[0], lat[1], lat[2], lat[3]};
        const double res[3] = {lat[4], lat[5], lat[6]};
        const V3 dec0 = kmDecode({c[0], c[1], c[2], c[3]});
        double D[3];
        for (int k = 0; k < 3; ++k) D[k] = std::max(0.0, dec0[std::size_t(k)] + res[k]);
        const double v = clampd(0.2126 * D[0] + 0.7152 * D[1] + 0.0722 * D[2], 0, 1);
        const double mx = std::max({D[0], D[1], D[2]}), mn = std::min({D[0], D[1], D[2]});
        const double keep = smooth(0.15, 0.6, (mx - mn) / std::max(mx, 1e-3));
        const double pk = p.push * (1 - 0.85 * keep);
        c[3] += pk * (1 - v) * (1 - v); c[2] += 0.6 * pk * (1 - v) * (1 - v); c[1] += pk * 0.08 * v * v;
        double hh[2], rel[4];
        sample(H, 2, w, h, sx, sy, 2, hh);
        sample(Rl, 4, w, h, sx, sy, 4, rel);
        const double jit = (hh[1] - 0.5) * 2;
        const double bk = p.broken * (1 - 0.85 * v * v) * (1 - 0.7 * keep);
        c[1] += bk * std::max(jit, 0.0); c[3] += bk * std::max(-jit, 0.0); c[2] += 0.5 * bk * std::abs(jit) * (1 - v);
        const double cs = c[0] + c[1] + c[2] + c[3];
        for (double& ci : c) ci /= cs;
        const V3 dec1 = kmDecode({c[0], c[1], c[2], c[3]});
        double rgb[3];
        for (int k = 0; k < 3; ++k) rgb[k] = D[k] * std::min(4.0, (dec1[std::size_t(k)] + 0.01) / (dec0[std::size_t(k)] + 0.01));
        if (p.valueBands > 0) {
            const double vy = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2], f = std::min(vy, 1.0) * p.valueBands, fl = std::floor(f);
            const double vq = std::max(std::min(vy, 0.02), (fl + smooth(0.3, 0.7, f - fl)) / p.valueBands);
            const double g = 1 + (vq / std::max(vy, 1e-6) - 1) * (1 - 0.6 * keep);
            for (double& q : rgb) q = q * g;
        }
        const double paper = paintCanvasNoise(plan, cv, PaintNoise::Paper, x, y, 0.5);
        if (p.medium == 2) {
            const double valley = paintCanvasNoise(plan, cv, PaintNoise::PaperHalf, x, y, 0.5), Rg = 0.86 * (0.93 + 0.07 * paper);
            const double X0 = 1.5, X = X0 * (1 + p.granulation * (0.5 - valley) * 2) * (1 + p.edge * rel[3]);
            const V3 g0 = glaze(c, X0, 0.86), g1 = glaze(c, X, Rg);
            auto lum = [](const V3& q) { return 0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2]; };
            const double sr = lum(g1) / std::max(lum(g0), 1e-4);
            for (int k = 0; k < 3; ++k) {
                const double ratio = clampd(sr + (g1[std::size_t(k)] / std::max(g0[std::size_t(k)], 1e-4) - sr) * keep, 0.5, 2);
                rgb[k] = std::pow(clampd(rgb[k] * ratio, 0, 1), p.dilution);
            }
        } else {
            for (double& q : rgb) q = q * rel[1] + rel[2];
        }
        const double t = 1 - p.lines * (1 - rel[0]);
        const std::size_t o = (std::size_t(y) * w + std::size_t(x)) * 4;
        for (int k = 0; k < 3; ++k) img[o + std::size_t(k)] = float(std::max(0.0, rgb[k] * t));
        img[o + 3] = 1;
    }
    return img;
}

}  // namespace raw::style
