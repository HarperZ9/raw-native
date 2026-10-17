// Palette quantisation and dither, float64 reference: see raw/renderer/style_dither.hpp. Operations
// are in the order of web/shaders/dither/dither.mjs, mix.mjs and reference/retro-dither.mjs.
#include "raw/renderer/style_dither.hpp"
#include "raw/renderer/style_ditherlight.hpp"   // bayer8Rank, blueRank
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace raw::style {
namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
double clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
// The runner-up gap tracker: best and second-best of a minimised score.
struct Best { double best{kInf}, second{kInf}; int k{-1}; void see(double d, int i) { if (d < best) { second = best; best = d; k = i; } else if (d < second) second = d; } };
int nearest(double L, double a, double b, const DitherPalette& pal, double& gap) {
    Best m;
    for (std::size_t i = 0; i < pal.entries.size(); ++i) {
        const V3& p = pal.entries[i].lab;
        const double dL = L - p[0], da = a - p[1], db = b - p[2];
        m.see(dL * dL + da * da + db * db, int(i));
    }
    gap = std::min(gap, m.second - m.best);
    return m.k;
}
int farSide(double L, double a, double b, int i, const DitherPalette& pal, double& gap) {
    const V3& p = pal.entries[std::size_t(i)].lab;
    const double dL = L - p[0], da = a - p[1], db = b - p[2], d2 = dL * dL + da * da + db * db;
    Best m;
    for (std::size_t j = 0; j < pal.entries.size(); ++j) {
        if (int(j) == i) continue;
        const V3& q = pal.entries[j].lab;
        const double sL = q[0] - p[0], sa = q[1] - p[1], sb = q[2] - p[2], len2 = sL * sL + sa * sa + sb * sb;
        if (len2 < 1e-9) continue;
        const double u = (dL * sL + da * sa + db * sb) / len2;
        gap = std::min(gap, std::abs(u));
        if (u <= 0) continue;
        const double uc = std::min(u, 1.0);
        m.see(d2 - uc * uc * len2 + pairPenalty() * len2 * (std::abs(uc - 0.5) + 0.5), int(j));
    }
    gap = std::min(gap, m.second - m.best);
    return m.k;
}
int orderedPick(double L, double a, double b, const DitherPalette& pal, double t, double strength, double& gap) {
    const int i = nearest(L, a, b, pal, gap);
    if (pal.entries.size() < 2 || strength <= 0) return i;
    const int j = farSide(L, a, b, i, pal, gap);
    if (j < 0) return i;
    const V3 &p = pal.entries[std::size_t(i)].lab, &q = pal.entries[std::size_t(j)].lab;
    const double sL = q[0] - p[0], sa = q[1] - p[1], sb = q[2] - p[2], len2 = sL * sL + sa * sa + sb * sb;
    if (len2 < 1e-9) return i;
    double f = ((L - p[0]) * sL + (a - p[1]) * sa + (b - p[2]) * sb) / len2;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    const double th = 0.5 + (t - 0.5) * strength;
    gap = std::min(gap, std::abs(f - th));
    return f > th ? j : i;
}
// mix.mjs mixPlan: 16 entries sorted by OKLab L (ties by index).
int mixPick(double r, double g, double b, const DitherPalette& pal, double t, double strength, double& gap) {
    if (strength <= 0 || pal.entries.size() < 2) { const V3 o = linearToOklab(r, g, b); return nearest(o[0], o[1], o[2], pal, gap); }
    const double th = 0.5 + (t - 0.5) * strength;
    int plan[16];
    double sr = 0, sg = 0, sb = 0;
    auto before = [&](int x, int y) { const double lx = pal.entries[std::size_t(x)].lab[0], ly = pal.entries[std::size_t(y)].lab[0]; return lx < ly || (lx == ly && x < y); };
    for (int i = 0; i < 16; ++i) {
        const double n = i + 1;
        const V3 T = linearToOklab(n * r, n * g, n * b);
        Best m;
        for (std::size_t j = 0; j < pal.lin.size(); ++j) {
            const V3 C = linearToOklab(sr + pal.lin[j][0], sg + pal.lin[j][1], sb + pal.lin[j][2]);
            m.see(std::pow(C[0] - T[0], 2) + std::pow(C[1] - T[1], 2) + std::pow(C[2] - T[2], 2), int(j));
        }
        gap = std::min(gap, m.second - m.best);
        const int k = m.k;
        int q = i;
        while (q > 0 && before(k, plan[q - 1])) { plan[q] = plan[q - 1]; --q; }
        plan[q] = k;
        sr += pal.lin[std::size_t(k)][0]; sg += pal.lin[std::size_t(k)][1]; sb += pal.lin[std::size_t(k)][2];
    }
    const double x = th * 16;
    gap = std::min(gap, std::abs(x - std::floor(x + 0.5)) / 16);
    return plan[std::min(15, int(std::floor(x)))];
}
int bayerRank(int n, int x, int y) {
    static const int B2[2][2] = {{0, 2}, {3, 1}};
    static const int B4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    if (n == 2) return B2[y % 2][x % 2];
    if (n == 4) return B4[y % 4][x % 4];
    return bayer8Rank(x, y);
}
}  // namespace

double pairPenalty() { return 0.12; }
double ign(int x, int y) {
    const double f = 0.06711056 * x + 0.00583715 * y, v = 52.9829189 * (f - std::floor(f));
    return v - std::floor(v);
}
DitherMode ditherMode(const std::string& m) {
    const char* names[] = {"none", "bayer2", "bayer4", "bayer8", "noise", "blue", "mix-bayer4", "mix-bayer8", "mix-blue"};
    for (std::uint32_t k = 0; k < 9; ++k) if (m == names[k]) return DitherMode(k);
    throw std::invalid_argument("dither: unknown mode " + m);
}
DitherPalette ditherPalette(const std::string& name) {
    DitherPalette p;
    p.entries = labPalette(name);
    for (const PaletteEntry& e : p.entries) p.lin.push_back({srgb8ToLinear(e.rgb[0]), srgb8ToLinear(e.rgb[1]), srgb8ToLinear(e.rgb[2])});
    return p;
}

std::vector<std::uint8_t> ditherPlate(const std::vector<std::uint8_t>& rgba, int w, int h, const DitherPalette& pal, const DitherOptions& o,
                                      std::vector<double>* margin) {
    const std::size_t n = std::size_t(w) * h;
    std::vector<std::uint8_t> out(n);
    if (margin) margin->assign(n, kInf);
    const double s = clamp01(o.strength);
    const auto mode = std::uint32_t(o.mode);
    const bool mix = mode >= 6;
    if (mix && o.brightness != 0) throw std::invalid_argument("dither: the mix modes take brightness 0 only");
    const int N = o.mode == DitherMode::Bayer2 ? 2 : o.mode == DitherMode::Bayer4 || o.mode == DitherMode::MixBayer4 ? 4
                : o.mode == DitherMode::Bayer8 || o.mode == DitherMode::MixBayer8 ? 8 : 0;
    const bool blue = o.mode == DitherMode::Blue || o.mode == DitherMode::MixBlue, noise = o.mode == DitherMode::Noise;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t p = std::size_t(y) * w + std::size_t(x), i = p * 4;
        const double r = srgb8ToLinear(rgba[i]), g = srgb8ToLinear(rgba[i + 1]), b = srgb8ToLinear(rgba[i + 2]);
        double gap = kInf;
        const double t = blue ? (blueRank(x, y) + 0.5) / 4096 : noise ? ign(x, y) : N ? (bayerRank(N, x, y) + 0.5) / (N * N) : 0;
        int k;
        if (mix) k = mixPick(r, g, b, pal, t, s, gap);
        else {
            const V3 lab = linearToOklab(r, g, b);
            const double L = clamp01(lab[0] + o.brightness);
            const bool patterned = (N > 0 || noise || blue) && s > 0;
            k = patterned ? orderedPick(L, lab[1], lab[2], pal, t, s, gap) : nearest(L, lab[1], lab[2], pal, gap);
        }
        out[p] = std::uint8_t(k);
        if (margin) (*margin)[p] = gap;
    }
    return out;
}

}  // namespace raw::style
