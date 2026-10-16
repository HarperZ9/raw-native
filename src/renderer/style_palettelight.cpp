// Palette-locked lighting, float64 reference: see raw/renderer/style_quantise.hpp. Operations are in
// the order of web/shaders/palettelight/palette.mjs and palettelight.mjs.
#include "raw/renderer/style_quantise.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace raw::style {
namespace {
constexpr double kPi = std::numbers::pi, D2R = kPi / 180;
double toward(double h, double t, double f) {
    double d = t - h;
    while (d > kPi) d -= 2 * kPi;
    while (d < -kPi) d += 2 * kPi;
    return h + d * f;
}
int byte8(double v) { return int(std::floor(std::min(1.0, std::max(0.0, v)) * 255 + 0.5)); }
PaletteEntry entry(double L, double C, double h) {
    const V3 s = oklabToSrgb(L, C * std::cos(h), C * std::sin(h));
    const std::array<int, 3> rgb{byte8(s[0]), byte8(s[1]), byte8(s[2])};
    return {srgbToOklab(rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0), rgb};
}
constexpr double kBases[4][3] = {{0.34, 0.42, 0.24}, {0.62, 0.6, 0.58}, {0.42, 0.29, 0.18}, {0.2, 0.38, 0.22}};   // ground, stone, wood, foliage
}  // namespace

RampPalette buildRampPalette(int steps, int voidSteps) {
    RampPalette P;
    P.steps = steps; P.voidSteps = voidSteps;
    for (int c = 0; c < 4; ++c) {
        const V3 o = srgbToOklab(kBases[c][0], kBases[c][1], kBases[c][2]);
        const double L0 = o[0], C0 = std::hypot(o[1], o[2]), h0 = std::atan2(o[2], o[1]);
        for (int v = 0; v < 2; ++v)
            for (int s = 0; s < steps; ++s) {
                const double t = double(s) / (steps - 1);
                double L = std::min(0.97, L0 * (0.34 + 0.86 * std::pow(t, 0.9))), C = std::min(0.2, C0 * (0.6 + 0.9 * std::sin(kPi * (0.15 + 0.8 * t))));
                double h = toward(h0, 285 * D2R, 0.55 * std::pow(1 - t, 1.2));
                h = toward(h, 85 * D2R, 0.4 * std::pow(t, 1.6));
                if (v == 0) { h = toward(h, 250 * D2R, 0.3); L *= 0.97; }
                else { h = toward(h, 62 * D2R, 0.72 * (0.4 + 0.6 * t)); C = std::min(0.22, C * 1.3); L = std::min(0.97, L * 1.12); }
                P.ramp[c][v].push_back(int(P.entries.size()));
                P.entries.push_back(entry(L, C, h));
            }
    }
    for (int s = 0; s < voidSteps; ++s) {
        const double t = double(s) / voidSteps, L = 0.2 + 0.44 * std::pow(std::sin(kPi * t), 1.3) + (s == 3 ? 0.04 : 0), C = 0.07 + 0.1 * std::sin(kPi * t);
        const double h = (285 + 75 * std::sin(2 * kPi * t) - 40 * (s == 3 ? 1 : 0)) * D2R;
        P.voidRamp.push_back(int(P.entries.size()));
        P.entries.push_back(entry(L, C, h));
    }
    const double win[3][3] = {{0.7, 0.13, 70}, {0.83, 0.12, 85}, {0.95, 0.08, 95}}, lamp[2][3] = {{0.9, 0.1, 90}, {0.98, 0.05, 100}};
    for (const auto& e : win) { P.window.push_back(int(P.entries.size())); P.entries.push_back(entry(e[0], e[1], e[2] * D2R)); }
    for (const auto& e : lamp) { P.lamp.push_back(int(P.entries.size())); P.entries.push_back(entry(e[0], e[1], e[2] * D2R)); }
    P.night = int(P.entries.size());
    P.entries.push_back(entry(0.14, 0.04, 285 * D2R));
    return P;
}

PalettelightPlan palettelightPlan(const V3& lampPos, int steps, int voidSteps) {
    PalettelightPlan p;
    p.steps = steps; p.voidSteps = voidSteps;
    const double l = std::hypot(-0.5, 0.68, 0.5);
    p.moonDir = {-0.5 / l, 0.68 / l, 0.5 / l};
    p.lamp = makeRadial(lampPos, 11, 1.6, 6, 0.25);
    const double windows[2][3] = {{-6.0, 2.4, 0.32}, {-3.0, 2.4, 0.32}};
    for (int k = 0; k < 2; ++k) p.glows[k] = makeRadial({windows[k][0], windows[k][1] - 0.2, windows[k][2] + 0.9}, 6, 1.0, 1.3, 0.5);
    p.palette = buildRampPalette(steps, voidSteps);
    return p;
}

PalettelightFrame runPalettelight(const PalettelightPlan& p, const ChapelGBuffer& g, double time) {
    const std::size_t n = std::size_t(g.w) * g.h;
    PalettelightFrame f{std::vector<std::int32_t>(n), std::vector<std::uint8_t>(n * 4), std::vector<double>(n, 1e30)};
    const RampPalette& pal = p.palette;
    const int K = p.steps, vs = p.voidSteps;
    for (int y = 0; y < g.h; ++y) for (int x = 0; x < g.w; ++x) {
        const std::size_t i = std::size_t(y) * g.w + std::size_t(x);
        const double T1 = thresholdAt(Source::Bayer8, x, y), T2 = thresholdAt(Source::Bayer8, x + 11, y + 7);
        const V3 P{g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]}, N{g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2]};
        const double a = g.a[i];
        int k = pal.night;
        double margin = 1e30;
        if (g.hit[i]) {
            const int cls = g.cls[i];
            if (cls == 4) {
                const double pos = std::fmod(g.phase[i] * vs + time * p.cycleRate * vs, double(vs)), lo = std::floor(pos), fr = pos - lo;
                margin = std::abs(fr - T1);
                k = pal.voidRamp[std::size_t((fr > T1 ? int(lo) + 1 : int(lo)) % vs)];
            } else if (cls == 5) {
                const double s = 1.4 + 0.6 * std::sin(time * 5 + P[0] * 3) + 0.4 * (a - 0.5), tone = std::min(2.0, std::max(0.0, s));
                const int lo = std::min(1, int(std::floor(tone)));
                margin = std::abs(tone - lo - T1);
                k = pal.window[std::size_t(tone - lo > T1 ? lo + 1 : lo)];
            } else if (cls == 6) {
                const double s = 0.5 + 0.2 * std::sin(time * 7);
                margin = std::abs(s - T1);
                k = pal.lamp[s > T1 ? 1 : 0];
            } else {
                const V3& md = p.moonDir;
                const double nl = std::max(0.0, N[0] * md[0] + N[1] * md[1] + N[2] * md[2]);
                const double cool = p.ambient * (p.ambientBase + 0.3 * (0.5 + 0.5 * N[1])) + p.moon * nl * g.vis[i * 3];
                double warm = 0;
                const double m = maskAt(p.lamp, P);
                if (m > 0) warm += g.vis[i * 3 + 1] * m * surfaceTerm(p.lamp, P, N);
                for (const DitherLight& L : p.glows) { const double q = maskAt(L, P); if (q > 0) warm += g.vis[i * 3 + 2] * q * surfaceTerm(L, P, N); }
                const double E = cool + warm, tone = 1 - std::exp(-p.gain * E * a), s = std::min(double(K - 1), std::max(0.0, tone * (K - 1)));
                const double w = E > 1e-9 ? warm / E : 0;
                const int lo = std::min(K - 2, int(std::floor(s))), step = s - lo > T1 ? lo + 1 : lo;
                const int variant = w >= 0.7 ? 1 : w <= 0.3 ? 0 : (w - 0.3) / 0.4 > T2 ? 1 : 0;
                margin = std::min({std::abs(s - lo - T1), std::abs(w - 0.7), std::abs(w - 0.3), std::abs((w - 0.3) / 0.4 - T2)});
                k = pal.ramp[cls][variant][std::size_t(step)];
            }
        }
        f.idx[i] = k; f.margin[i] = margin;
        for (std::size_t c = 0; c < 3; ++c) f.rgba[i * 4 + c] = std::uint8_t(pal.entries[std::size_t(k)].rgb[c]);
        f.rgba[i * 4 + 3] = 255;
    }
    return f;
}

}  // namespace raw::style
