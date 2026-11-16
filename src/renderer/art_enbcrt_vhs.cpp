// The ENB port's buffer reads, hash streams and VHS pipeline (Effect_CRTShader.fxh VHS_*), in the
// original's order: see raw/renderer/art_enbcrt.hpp.
#include "art_enbcrt_internal.hpp"
#include "raw/renderer/noise.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art::enbcrt {

C3 Image::at(int x, int y) const {
    const std::size_t i = (std::size_t(std::clamp(y, 0, h - 1)) * std::size_t(w) + std::size_t(std::clamp(x, 0, w - 1))) * 4;
    return {(*px)[i], (*px)[i + 1], (*px)[i + 2]};
}
C3 Image::linear(double u, double v) const {
    const double fx = std::clamp(u * w - 0.5, 0.0, double(w - 1)), fy = std::clamp(v * h - 0.5, 0.0, double(h - 1));
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const double tx = fx - x0, ty = fy - y0;
    const C3 a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    C3 o{};
    for (int k = 0; k < 3; ++k) o[k] = (a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
    return o;
}
C3 Image::point(double u, double v) const { return at(int(std::floor(u * w)), int(std::floor(v * h))); }

double unitHash(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t stream) {
    return double(noise::hash(a, b, c, stream) >> 8) * (1.0 / 16777216.0);
}
double smooth(double e0, double e1, double x) { const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3 - 2 * t); }
C3 toYiq(const C3& c) {
    return {c[0] * 0.2989 + c[1] * 0.5870 + c[2] * 0.1140, c[0] * 0.5959 - c[1] * 0.2744 - c[2] * 0.3216, c[0] * 0.2115 - c[1] * 0.5229 + c[2] * 0.3114};
}
C3 fromYiq(const C3& c) {
    return {c[0] + c[1] * 0.9563 + c[2] * 0.6210, c[0] - c[1] * 0.2721 - c[2] * 0.6474, c[0] - c[1] * 1.1070 + c[2] * 1.7046};
}
C3 sat(const C3& c) { return {std::clamp(c[0], 0.0, 1.0), std::clamp(c[1], 0.0, 1.0), std::clamp(c[2], 0.0, 1.0)}; }
double dot3(const C3& a, const C3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
int lineOf(int y, int h, int lines) { return int(((2LL * y + 1) * lines) / (2LL * h)); }

namespace {
C3 chromaBlur(const Ctx& c, double u, double v, double amount, double gen) {
    C3 yiq = toYiq(c.src.point(u, v));
    const double r = (2.0 + amount * 4.0) * gen;
    double ai = 0, aq = 0, tw = 0;
    for (int i = -8; i <= 8; ++i) {
        const double w = std::exp(-0.5 * double(i * i) / std::max(r * r, 0.01));
        const C3 s = toYiq(c.src.point(u + double(i) * 2.0 / c.src.w, v));
        ai += s[1] * w; aq += s[2] * w; tw += w;
    }
    yiq[1] = ai / tw; yiq[2] = aq / tw;
    return sat(fromYiq(yiq));
}
C3 chromaSmear(const Ctx& c, const C3& colour, double u, double v, double amount) {
    C3 yiq = toYiq(colour);
    double ai = 0, aq = 0, tw = 0;
    for (int i = 0; i < 8; ++i) {
        const double t = double(i) / 8.0, w = std::exp(-t * 3.0);
        const C3 s = toYiq(c.src.point(u + t * amount * 8.0 / c.src.w, v));
        ai += s[1] * w; aq += s[2] * w; tw += w;
    }
    yiq[1] = ai / tw; yiq[2] = aq / tw;
    return sat(fromYiq(yiq));
}
C3 headSwitch(const Ctx& c, const C3& colour, double u, double v, double intensity) {
    const double zone = smooth(0.92, 0.98, v);
    if (zone < 0.001) return colour;
    const std::uint32_t line = std::uint32_t(lineOf(c.y, c.src.h, 480));
    const double jitter = (unitHash(line, std::uint32_t(c.frame * 10 / 3), 0, 11) - 0.5) * 2.0;
    const double du = std::clamp(u + jitter * 0.05 * zone * intensity, 0.0, 1.0), dv = std::clamp(v, 0.0, 1.0);
    const double n = unitHash(std::uint32_t(c.x), std::uint32_t(c.y), std::uint32_t(c.frame), 12);
    C3 r = c.src.linear(du, dv), o{};
    for (int k = 0; k < 3; ++k) { r[k] = r[k] + (n - r[k]) * (zone * intensity * 0.5); o[k] = colour[k] + (r[k] - colour[k]) * zone; }
    return o;
}
C3 tapeNoise(const Ctx& c, C3 colour, double u, double rate, double snow) {
    const std::uint32_t line = std::uint32_t(lineOf(c.y, c.src.h, 480)), t = std::uint32_t(c.frame);
    const bool drop = unitHash(line, t, 0, 14) >= 1.0 - rate;
    const double bright = unitHash(line, t, 1, 14) >= 0.5 ? 1.0 : 0.0, width = unitHash(line, t, 2, 14) * 0.3 + 0.05, x0 = unitHash(line, t, 3, 14);
    const double in = (drop && u >= x0 && u <= x0 + width) ? 1.0 : 0.0;
    for (int k = 0; k < 3; ++k) colour[k] += (bright - colour[k]) * (in * 0.8);
    if (snow > 0.001) {
        const double s = unitHash(std::uint32_t(c.x), std::uint32_t(c.y), std::uint32_t(c.frame * 100 / 3), 15);
        for (int k = 0; k < 3; ++k) colour[k] += (s - colour[k]) * (snow * 0.15);
    }
    return colour;
}
}  // namespace

C3 vhs(const Ctx& c, double& u, double& v) {
    const EnbCrtParams& p = c.p;
    const double gen = 1.0 + p.generation * 0.4, qual = 1.0 + p.quality * 0.3;
    if (p.tracking > 0.01) {
        const int b = int(((2LL * c.y + 1) * 250 + std::int64_t(c.frame) * c.src.h) / (10LL * c.src.h));
        const double band = unitHash(std::uint32_t(b), 0, 0, 13) >= 0.98 ? 1.0 : 0.0;
        u += std::sin(c.time * 7.0 + v * 100.0) * (p.tracking * qual) * band * 0.03;
    }
    if (p.jitter > 0.01) {
        const double amount = p.jitter * qual * 0.5, line = lineOf(c.y, c.src.h, 480);
        u += std::sin(line * 0.7 + c.time * 50.0) * amount * (1.0 / c.src.w) * 2.0;
        u += (unitHash(std::uint32_t(line), std::uint32_t(c.frame * 2), 0, 16) - 0.5) * amount * (1.0 / c.src.w) * 3.0;
    }
    u = std::clamp(u, 0.0, 1.0); v = std::clamp(v, 0.0, 1.0);
    C3 colour = p.chromaBlur > 0.01 ? chromaBlur(c, u, v, p.chromaBlur * qual, gen) : c.src.linear(u, v);
    if (p.chromaSmear > 0.01) colour = chromaSmear(c, colour, u, v, p.chromaSmear * qual);
    if (p.headSwitch > 0.01) colour = headSwitch(c, colour, u, v, p.headSwitch);
    if (p.tapeNoise > 0.001 || p.snow > 0.001) colour = tapeNoise(c, colour, u, p.tapeNoise * gen, p.snow * gen);
    if (p.generation > 0) {
        const double loss = p.generation * 0.08;
        for (int k = 0; k < 3; ++k) colour[k] += (0.5 - colour[k]) * loss;
        const double l = dot3(colour, kLum);
        for (int k = 0; k < 3; ++k) colour[k] += (l - colour[k]) * (loss * 0.5);
    }
    return sat(colour);
}

}  // namespace raw::art::enbcrt
