// crt-classic reference: see raw/renderer/style_crt_classic.hpp. Operations are in the order of
// web/shaders/reference/retro-crt.mjs; a float here is a Float32Array element there.
#include "raw/renderer/style_crt_classic.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
std::uint8_t u8(int v) { return std::uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }
std::int32_t fixed1(float t) { return std::int32_t(double(t) * 256 + 0.5); }
int jsRound(double x) { return int(std::floor(x + 0.5)); }
int toSrgbCode(const ClassicTables& T, double l) { return T.toSrgb[l >= 1 ? 4096 : l <= 0 ? 0 : std::size_t(int(l * 4096 + 0.5))]; }

void phosphorPass(std::vector<std::uint8_t>& d, int w, int h, const ClassicOptions& o, int cell) {
    const bool scan = o.scanlines;
    const double s = clampd(o.scanStrength, 0, 1);
    const bool maskOn = o.mask != "none" && o.maskStrength > 0;
    if (!scan && !maskOn) return;
    const std::vector<std::int32_t> beam = classicBeamFixed(o), mt = classicMaskFixed(o);
    const bool flatCell = scan && cell < 2;
    for (int y = 0; y < h; ++y) {
        const int row = y % cell, ry = (y % 6) * 3, flat = flatCell ? int(((y & 1) == 0 ? 1 - s : 1.1) * 256) : 0;
        std::size_t i = std::size_t(y) * w * 4;
        for (int x = 0; x < w; ++x, i += 4) {
            const int r = d[i], g = d[i + 1], b = d[i + 2], p = (ry + x % 3) * 3;
            const int k = (r > g ? (r > b ? r : b) : (g > b ? g : b)) >> 4;
            const int f = flat > 0 ? flat : beam[std::size_t(k * cell + row)];
            d[i] = u8((r * f * mt[std::size_t(p)] + 32768) >> 16);
            d[i + 1] = u8((g * f * mt[std::size_t(p + 1)] + 32768) >> 16);
            d[i + 2] = u8((b * f * mt[std::size_t(p + 2)] + 32768) >> 16);
        }
    }
}

// One box-blur line with a running sum (float64) and clamped ends.
void boxLine(const std::vector<float>& src, std::vector<float>& dst, std::size_t base, std::size_t stride, int len, int r) {
    const double inv = 1.0 / (2 * r + 1), first = src[base], last = src[base + std::size_t(len - 1) * stride];
    double sum = first * (r + 1);
    for (int i = 1; i <= r; ++i) sum += i < len ? double(src[base + std::size_t(i) * stride]) : last;
    for (int i = 0; i < len; ++i) {
        dst[base + std::size_t(i) * stride] = float(sum * inv);
        const int a = i + r + 1, b = i - r;
        sum += (a < len ? double(src[base + std::size_t(a) * stride]) : last) - (b >= 0 ? double(src[base + std::size_t(b) * stride]) : first);
    }
}
void blur3(std::vector<float>& buf, int lw, int lh, double sigma) {
    const int r = classicBoxRadius(sigma);
    std::vector<float> tmp(buf.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < lh; ++y) for (int c = 0; c < 3; ++c) boxLine(buf, tmp, std::size_t(y) * lw * 3 + c, 3, lw, r);
        for (int x = 0; x < lw; ++x) for (int c = 0; c < 3; ++c) boxLine(tmp, buf, std::size_t(x) * 3 + c, std::size_t(lw) * 3, lh, r);
    }
}

std::vector<float> glowField(const std::vector<std::uint8_t>& d, int w, int h, int q, int lw, int lh, double bloom, double halation) {
    const ClassicTables& T = classicTables();
    std::vector<float> lin(static_cast<std::size_t>(lw) * lh * 3, 0.0f), bright(lin.size(), 0.0f), glow(lin.size(), 0.0f);
    for (int y = 0; y < h; ++y) {
        const std::size_t lrow = std::size_t(y / q) * lw;
        std::size_t i = std::size_t(y) * w * 4;
        for (int x = 0; x < w; ++x, i += 4) {
            const std::size_t p = (lrow + std::size_t(x / q)) * 3;
            for (std::size_t c = 0; c < 3; ++c) lin[p + c] = float(double(lin[p + c]) + double(T.toLin[d[i + c]]));
        }
    }
    for (int ly = 0; ly < lh; ++ly) {
        const int cy = std::min(q, h - ly * q);
        for (int lx = 0; lx < lw; ++lx) {
            const std::size_t p = (std::size_t(ly) * lw + std::size_t(lx)) * 3;
            const double n = 1.0 / double(cy * std::min(q, w - lx * q));
            const double r = lin[p] * n, g = lin[p + 1] * n, b = lin[p + 2] * n;
            lin[p] = float(r); lin[p + 1] = float(g); lin[p + 2] = float(b);
            const double t = clampd((0.2126 * r + 0.7152 * g + 0.0722 * b - 0.18) / 0.3, 0, 1), k = t * t * (3 - 2 * t);
            bright[p] = float(r * k); bright[p + 1] = float(g * k); bright[p + 2] = float(b * k);
        }
    }
    if (bloom > 0) { blur3(bright, lw, lh, 8.0 / q); for (std::size_t i = 0; i < glow.size(); ++i) glow[i] = float(double(glow[i]) + double(bright[i]) * bloom * 0.9); }
    if (halation > 0) { blur3(lin, lw, lh, 28.0 / q); for (std::size_t i = 0; i < glow.size(); ++i) glow[i] = float(double(glow[i]) + double(lin[i]) * halation * 0.3); }
    return glow;
}

void glowPass(std::vector<std::uint8_t>& d, int w, int h, double bloom, double halation) {
    const ClassicTables& T = classicTables();
    const int q = std::min(w, h) < 320 ? 2 : 4, lw = (w + q - 1) / q, lh = (h + q - 1) / q;
    const std::vector<float> glow = glowField(d, w, h, q, lw, lh, bloom, halation);
    std::vector<int> X0(static_cast<std::size_t>(w)), X1(static_cast<std::size_t>(w));
    std::vector<float> TX(static_cast<std::size_t>(w));
    for (int x = 0; x < w; ++x) {
        const double fx = clampd((x + 0.5) / q - 0.5, 0, lw - 1);
        X0[std::size_t(x)] = int(fx); X1[std::size_t(x)] = std::min(lw - 1, X0[std::size_t(x)] + 1); TX[std::size_t(x)] = float(fx - X0[std::size_t(x)]);
    }
    for (int y = 0; y < h; ++y) {
        const double fy = clampd((y + 0.5) / q - 0.5, 0, lh - 1);
        const int y0 = int(fy), y1 = std::min(lh - 1, y0 + 1);
        const double ty = fy - y0;
        const std::size_t ra = std::size_t(y0) * lw, rc = std::size_t(y1) * lw;
        std::size_t i = std::size_t(y) * w * 4;
        for (int x = 0; x < w; ++x, i += 4) {
            const double tx = TX[std::size_t(x)];
            const std::size_t a = (ra + std::size_t(X0[std::size_t(x)])) * 3, b = (ra + std::size_t(X1[std::size_t(x)])) * 3;
            const std::size_t c = (rc + std::size_t(X0[std::size_t(x)])) * 3, e = (rc + std::size_t(X1[std::size_t(x)])) * 3;
            const double w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
            double g3[3];
            for (std::size_t k = 0; k < 3; ++k) g3[k] = double(glow[a + k]) * w00 + double(glow[b + k]) * w10 + double(glow[c + k]) * w01 + double(glow[e + k]) * w11;
            if (g3[0] + g3[1] + g3[2] < 0.0015) continue;
            for (std::size_t k = 0; k < 3; ++k) d[i + k] = u8(toSrgbCode(T, double(T.toLin[d[i + k]]) + g3[k]));
        }
    }
}

int tap(const std::vector<std::uint8_t>& src, int w, int h, double su, double sv, int c) {
    double sx = (su + 1) * (w - 1) * 0.5, sy = (sv + 1) * (h - 1) * 0.5;
    if (sx < 0) sx = 0; else if (sx > w - 1) sx = w - 1;
    if (sy < 0) sy = 0; else if (sy > h - 1) sy = h - 1;
    const int x0 = int(sx), y0 = int(sy), tx = int((sx - x0) * 256), ty = int((sy - y0) * 256);
    const std::size_t p = (std::size_t(y0) * w + std::size_t(x0)) * 4 + std::size_t(c), dx = x0 + 1 < w ? 4 : 0, dy = y0 + 1 < h ? std::size_t(w) * 4 : 0;
    const int top = src[p] * (256 - tx) + src[p + dx] * tx, bot = src[p + dy] * (256 - tx) + src[p + dy + dx] * tx;
    return (top * (256 - ty) + bot * ty) >> 16;
}
double bezelAlpha(double asu, double asv, double rc, double feather) {
    if (rc <= 0) return asu <= 1 && asv <= 1 ? 1 : 0;
    const double qx = asu - (1 - rc), qy = asv - (1 - rc), mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
    const double inner = qx > qy ? qx : qy;
    const double dist = std::sqrt(mx * mx + my * my) + (inner < 0 ? inner : 0) - rc;
    return clampd(-dist / feather, 0, 1);
}

void tubePass(std::vector<std::uint8_t>& d, int w, int h, double curvature, double aberration, double vignette) {
    const double W2 = double(w - 1) * (w - 1), H2 = double(h - 1) * (h - 1), nv = 1 / (W2 + H2);
    std::vector<float> U(static_cast<std::size_t>(w)), VX(static_cast<std::size_t>(w));
    for (int x = 0; x < w; ++x) { const double u = (double(x) / (w - 1)) * 2 - 1; U[std::size_t(x)] = float(u); VX[std::size_t(x)] = float(u * u * W2 * nv); }
    const double vg = vignette * 256;
    if (curvature <= 0 && aberration <= 0) {
        if (vignette <= 0) return;
        for (int y = 0; y < h; ++y) {
            const double v = (double(y) / (h - 1)) * 2 - 1, vy = v * v * H2 * nv;
            std::size_t i = std::size_t(y) * w * 4;
            for (int x = 0; x < w; ++x, i += 4) {
                const int f = int(256 - vg * (double(VX[std::size_t(x)]) + vy));
                for (std::size_t k = 0; k < 3; ++k) d[i + k] = u8((d[i + k] * f) >> 8);
            }
        }
        return;
    }
    const std::vector<std::uint8_t> src = d;
    const double k = curvature * 0.35, dab = aberration * 0.012;
    const double rc = curvature > 0 ? 0.02 + 0.08 * std::min(1.0, curvature) : 0, feather = 1.5 / ((w + h) / 4.0);
    const double safe = 1 - rc - feather * 2, fr = 1 - dab, fb = 1 + dab;
    for (int y = 0; y < h; ++y) {
        const double v = (double(y) / (h - 1)) * 2 - 1, vy = v * v * H2 * nv;
        std::size_t i = std::size_t(y) * w * 4;
        for (int x = 0; x < w; ++x, i += 4) {
            const double u = U[std::size_t(x)], f = 1 + k * (u * u + v * v), su = u * f, sv = v * f;
            const double asu = su < 0 ? -su : su, asv = sv < 0 ? -sv : sv;
            const double alpha = asu <= safe && asv <= safe ? 1 : bezelAlpha(asu, asv, rc, feather);
            if (alpha <= 0) { d[i] = 0; d[i + 1] = 0; d[i + 2] = 0; continue; }
            const int vf = int(alpha * (256 - vg * (double(VX[std::size_t(x)]) + vy)));
            d[i] = u8((tap(src, w, h, su * fr, sv * fr, 0) * vf) >> 8);
            d[i + 1] = u8((tap(src, w, h, su, sv, 1) * vf) >> 8);
            d[i + 2] = u8((tap(src, w, h, su * fb, sv * fb, 2) * vf) >> 8);
        }
    }
}
}  // namespace

const ClassicTables& classicTables() {
    static const ClassicTables T = [] {
        ClassicTables t;
        t.toLin.resize(256); t.toSrgb.resize(4097);
        for (int i = 0; i < 256; ++i) { const double c = i / 255.0; t.toLin[std::size_t(i)] = float(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4)); }
        for (int i = 0; i <= 4096; ++i) {
            const double l = i / 4096.0;
            t.toSrgb[std::size_t(i)] = u8(jsRound((l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055) * 255));
        }
        return t;
    }();
    return T;
}

std::vector<float> classicBeamTable(int cell, double strength, double beam) {
    const double s = clampd(strength, 0, 1), mean = 1.05 - s / 2;
    const int up = std::max(1, cell), centre = up / 2;
    std::vector<float> out(static_cast<std::size_t>(16 * up)), g(static_cast<std::size_t>(up));
    for (int k = 0; k < 16; ++k) {
        const double sigma = 0.17 + clampd(beam, 0, 1) * 0.2 * (k / 15.0);
        double sum = 0;
        for (int r = 0; r < up; ++r) { const double v = double(r - centre) / up; g[std::size_t(r)] = float(std::exp(-0.5 * (v / sigma) * (v / sigma))); sum += g[std::size_t(r)]; }
        for (int r = 0; r < up; ++r) out[std::size_t(k * up + r)] = float(std::min(1.4, mean * (1 - s + s * (double(g[std::size_t(r)]) * up / sum))));
    }
    return out;
}

std::vector<float> classicMaskTable(const std::string& mode, double strength) {
    const double m = clampd(strength, 0, 1);
    std::vector<float> out(54);
    for (int y = 0; y < 6; ++y) for (int x = 0; x < 3; ++x) {
        int chan = x % 3;
        double rowDark = 1;
        if (mode == "slot") { if ((y + (x / 3) * 3) % 6 == 5) rowDark = 1 - m * 0.75; }
        else if (mode == "dot") { chan = (x + ((y / 3) & 1) * 2) % 3; if (y % 3 == 2) rowDark = 1 - m * 0.5; }
        else if (mode != "grille") chan = -1;
        const double gain = chan < 0 ? 1 : (1 + m * 0.45) * rowDark, off = chan < 0 ? 1 : (1 - m) * gain;
        const std::size_t p = std::size_t(y * 3 + x) * 3;
        for (int c = 0; c < 3; ++c) out[p + std::size_t(c)] = float(chan == c ? gain : off);
    }
    return out;
}

std::vector<std::int32_t> classicBeamFixed(const ClassicOptions& o) {
    const int cell = std::max(1, o.cell);
    const std::vector<float> t = o.scanlines && cell >= 2 ? classicBeamTable(cell, o.scanStrength, o.beam) : std::vector<float>(std::size_t(16 * cell), 1.0f);
    std::vector<std::int32_t> f(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) f[i] = fixed1(t[i]);
    return f;
}
std::vector<std::int32_t> classicMaskFixed(const ClassicOptions& o) {
    const bool maskOn = o.mask != "none" && o.maskStrength > 0;
    const std::vector<float> t = maskOn ? classicMaskTable(o.mask, o.maskStrength) : std::vector<float>(54, 1.0f);
    std::vector<std::int32_t> f(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) f[i] = fixed1(t[i]);
    return f;
}
int classicBoxRadius(double sigma) { return std::max(1, jsRound(std::sqrt(sigma * sigma * 4 + 1) / 2 - 0.5)); }

void crtClassic(std::vector<std::uint8_t>& d, int w, int h, const ClassicOptions& o) {
    phosphorPass(d, w, h, o, std::max(1, o.cell));
    const double bloom = std::max(0.0, o.bloom), halation = std::max(0.0, o.halation);
    if (bloom > 0 || halation > 0) glowPass(d, w, h, bloom, halation);
    tubePass(d, w, h, std::max(0.0, o.curvature), std::max(0.0, o.aberration), std::max(0.0, o.vignette));
}

}  // namespace raw::style
