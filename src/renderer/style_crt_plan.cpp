// The physical CRT's plan: presets, phosphor screens, the faceplate halo kernel and the parameter
// block, in the order of web/shaders/crt/params.mjs, phosphors.mjs, halo.mjs and layout.mjs, with
// the filter builders of web/shaders/common.mjs. See raw/renderer/style_crt.hpp.
#include "raw/renderer/style_crt.hpp"
#include "raw/renderer/style_spectral.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
using Spd = std::function<double(double)>;
std::vector<double> lowpassTaps(double cutoff, int half) {
    const int n = 2 * half + 1;
    std::vector<double> h(static_cast<std::size_t>(n));
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        const int m = i - half;
        const double x = 2 * cutoff * m;
        const double sinc = m == 0 ? 2 * cutoff : std::sin(kPi * x) / (kPi * m);
        const double w = 0.42 - 0.5 * std::cos((2 * kPi * i) / (n - 1)) + 0.08 * std::cos((4 * kPi * i) / (n - 1));
        h[std::size_t(i)] = sinc * w; sum += h[std::size_t(i)];
    }
    for (double& v : h) v /= sum;
    return h;
}
std::vector<double> notchTaps(double centre, double halfWidth, int half) {
    const std::vector<double> hi = lowpassTaps(std::min(0.499, centre + halfWidth), half), lo = lowpassTaps(std::max(0.001, centre - halfWidth), half);
    std::vector<double> h(hi.size());
    for (std::size_t i = 0; i < h.size(); ++i) h[i] = (int(i) == half ? 1 : 0) - (hi[i] - lo[i]);
    return h;
}
Spd energyBand(double peak, double fwhm) {
    const double k0 = 1e7 / peak, kw = (1e7 / (peak - fwhm / 2) - 1e7 / (peak + fwhm / 2)) / 2.3548;
    return [k0, kw](double wl) { const double d = (1e7 / wl - k0) / kw; return std::exp(-0.5 * d * d); };
}
Spd lineSpectrum(std::vector<std::array<double, 2>> lines, double fwhm = 3) {
    const double s = fwhm / 2.3548;
    return [lines, s](double wl) { double p = 0; for (const auto& l : lines) { const double d = (wl - l[0]) / s; p += l[1] * std::exp(-0.5 * d * d); } return p; };
}
spectral::T3 spdToXYZ(const Spd& spd) {
    double X = 0, Y = 0, Z = 0;
    for (int wl = 380; wl <= 780; ++wl) { const double p = spd(wl); const spectral::T3 c = spectral::cmf(wl); X += p * c[0] * 1; Y += p * c[1] * 1; Z += p * c[2] * 1; }
    return {X, Y, Z};
}
double energyOf(const Spd& spd) { double e = 0; for (int wl = 380; wl <= 780; ++wl) e += spd(wl); return e; }
struct Part { double share; Spd spd; std::array<double, 3> decay; };

double fresnelUnpolarised(double n, double th) {
    const double s = n * std::sin(th);
    if (s >= 1) return 1;
    const double ci = std::cos(th), ct = std::sqrt(1 - s * s);
    const double rs = (n * ci - ct) / (n * ci + ct), rp = (n * ct - ci) / (n * ct + ci);
    return 0.5 * (rs * rs + rp * rp);
}
std::vector<double> convolve(const std::vector<double>& a, const std::vector<double>& b, int R) {
    const int S = 2 * R + 1;
    std::vector<double> o(std::size_t(S) * S, 0.0);
    for (int ay = 0; ay < S; ++ay) for (int ax = 0; ax < S; ++ax) {
        const double va = a[std::size_t(ay * S + ax)];
        if (va == 0) continue;
        for (int by = 0; by < S; ++by) {
            const int y = ay + by - R;
            if (y < 0 || y >= S) continue;
            for (int bx = 0; bx < S; ++bx) { const int x = ax + bx - R; if (x >= 0 && x < S) o[std::size_t(y * S + x)] += va * b[std::size_t(by * S + bx)]; }
        }
    }
    return o;
}
double jsRound(double v) { return std::floor(v + 0.5); }
void putTaps(CrtPlan& plan, CrtTap t, const std::vector<double>& h) {
    plan.tapInfo[std::size_t(t)] = {int(plan.taps.size()), int(h.size() - 1) / 2};
    for (double v : h) { plan.taps.push_back(float(v)); plan.taps64.push_back(v); }
}
}  // namespace

CrtParams crtPreset(const std::string& name) {
    CrtParams p;
    auto glass = [&](double t, double tr) { p.glassThickness = t; p.transmission = tr; };
    if (name == "pvm-20") { p.pitch = 0.30; p.spotLo = 0.12; p.spotHi = 0.27; p.rx = 1600; p.ry = 1e9; glass(11, 0.7); return p; }
    if (name == "pvm-20-soft") { p.pitch = 0.30; p.pixelLock = true; p.pitchPx = 3; p.mix = 0.5; p.spotLo = 0.25; p.spotHi = 0.42; p.rgbMHz = 3.5; p.rx = 1600; p.ry = 1e9; glass(11, 0.7); return p; }
    if (name == "trinitron-tv") { p.diagonalIn = 27; p.overscan = 0.06; p.mode = 2; p.comb = true; p.pitch = 0.70; p.spotLo = 0.18; p.spotHi = 0.38; p.rx = 1700; p.ry = 1e9; glass(13, 0.56); return p; }
    if (name == "slot-tv") { p.overscan = 0.07; p.corner = 22; p.mode = 2; p.comb = false; p.maskType = 2; p.pitch = 0.75; p.slotHeight = 0.85; p.slotGap = 0.18; p.spotLo = 0.2; p.spotHi = 0.42; p.rx = 810; p.ry = 810; glass(12, 0.46); return p; }
    if (name == "vga-14") { p.diagonalIn = 14; p.maskType = 3; p.pitch = 0.28; p.fill = 0.8; p.spotLo = 0.18; p.spotHi = 0.32; p.rx = 700; p.ry = 700; glass(9, 0.6); return p; }
    if (name == "bw-tv") { p.diagonalIn = 17; p.overscan = 0.06; p.corner = 30; p.mode = 2; p.comb = false; p.maskType = 0; p.spotLo = 0.2; p.spotHi = 0.4; p.rx = 650; p.ry = 650; p.screen = "p4"; return p; }
    if (name == "radar-p7") { p.diagonalIn = 12; p.aspect = 1; p.corner = 60; p.maskType = 0; p.spotLo = 0.25; p.spotHi = 0.45; p.rx = 900; p.ry = 900; p.screen = "p7"; p.transmission = 0.8; return p; }
    if (name == "scope-p31") { p.diagonalIn = 8; p.maskType = 0; p.spotLo = 0.25; p.spotHi = 0.4; p.rx = 1e9; p.ry = 1e9; p.screen = "p31"; p.transmission = 0.85; return p; }
    throw std::runtime_error("unknown crt preset " + name);
}

CrtScreen crtScreen(const std::string& name) {
    std::vector<Part> parts;
    bool colour = false;
    if (name == "p22") {
        colour = true;
        parts = {{0, lineSpectrum({{611, 1}, {588, 0.1}, {595, 0.1}, {630, 0.15}, {707, 0.05}}), {1.0, 0.35e-3, 1e-3}},
                 {0, energyBand(535, 80), {0.9, 20e-6, 0.5e-3}}, {0, energyBand(450, 60), {0.95, 5e-6, 0.2e-3}}};
    } else if (name == "p4") {
        parts = {{0.45, energyBand(450, 55), {1.0, 5e-6, 5e-6}}, {0.55, energyBand(565, 110), {0.9, 20e-6, 0.5e-3}}, {0, energyBand(565, 110), {1, 1e-6, 1e-6}}};
    } else if (name == "p7") {
        parts = {{0.6, energyBand(440, 50), {1.0, 10e-6, 10e-6}}, {0.4, energyBand(558, 90), {0.5, 30e-3, 0.4}}, {0, energyBand(558, 90), {1, 1e-6, 1e-6}}};
    } else if (name == "p31") { parts = {{1, energyBand(531, 70), {0.9, 15e-6, 1e-3}}};
    } else if (name == "p39") { parts = {{1, energyBand(525, 40), {0.8, 40e-3, 150e-3}}};
    } else if (name == "p1") { parts = {{1, energyBand(525, 40), {0.8, 10e-3, 40e-3}}};
    } else throw std::runtime_error("unknown phosphor screen " + name);
    while (parts.size() < 3) parts.push_back({0, [](double) { return 0.0; }, {1, 1e-6, 1e-6}});
    const double smpteC[3][2] = {{0.630, 0.340}, {0.310, 0.595}, {0.155, 0.070}};
    std::array<spectral::T3, 3> xyz{};
    for (int k = 0; k < 3; ++k) {
        const Part& pt = parts[std::size_t(k)];
        if (colour) { xyz[std::size_t(k)] = spectral::xyToXYZ(smpteC[k][0], smpteC[k][1]); continue; }
        const spectral::T3 v = spdToXYZ(pt.spd);
        const double e = energyOf(pt.spd);
        xyz[std::size_t(k)] = e > 0 ? spectral::T3{v[0] / e, v[1] / e, v[2] / e} : spectral::T3{0, 0, 0};
    }
    spectral::T3 gain{};
    if (colour) {
        const spectral::M3 M = {xyz[0][0], xyz[1][0], xyz[2][0], xyz[0][1], xyz[1][1], xyz[2][1], xyz[0][2], xyz[1][2], xyz[2][2]};
        gain = spectral::apply3(spectral::inv3(M), spectral::xyToXYZ(0.3127, 0.329));
    } else {
        double Y = 0;
        for (int k = 0; k < 3; ++k) Y += parts[std::size_t(k)].share * xyz[std::size_t(k)][1];
        for (int k = 0; k < 3; ++k) gain[std::size_t(k)] = parts[std::size_t(k)].share / Y;
    }
    const spectral::M3 toOut = spectral::xyzToRec709();
    std::array<spectral::T3, 3> cols{};
    for (int k = 0; k < 3; ++k) {
        const spectral::T3& v = xyz[std::size_t(k)];
        const double g = gain[std::size_t(k)];
        cols[std::size_t(k)] = spectral::apply3(toOut, {v[0] * g, v[1] * g, v[2] * g});
    }
    CrtScreen s;
    s.matrix = {cols[0][0], cols[1][0], cols[2][0], cols[0][1], cols[1][1], cols[2][1], cols[0][2], cols[1][2], cols[2][2]};
    s.mono = !colour;
    for (int k = 0; k < 3; ++k) s.decay[std::size_t(k)] = parts[std::size_t(k)].decay;
    return s;
}

CrtHalo crtHaloKernel(const CrtParams& p, double mmPerPx, int maxCells) {
    const double t = p.glassThickness, n = p.glassN, rho = p.albedo, T = p.transmission;
    CrtHalo k;
    k.ringMm = 2 * t * std::tan(std::asin(1 / n));
    const double reach = 3.2 * k.ringMm;
    int q = std::max(1, int(jsRound(p.cell / mmPerPx)));
    while ((maxCells * q * mmPerPx) < reach) q++;
    k.q = q; k.cellMm = q * mmPerPx;
    const int R = std::min(maxCells, int(std::ceil(reach / k.cellMm))), S = 2 * R + 1;
    k.R = R;
    // radialReturn, with no back reflector.
    const double dr = k.cellMm / 8, rMax = (R + 0.5) * k.cellMm;
    std::vector<double> bins(std::size_t(std::ceil(rMax / dr)) + 1, 0.0);
    const int steps = 40000;
    const double dth = kPi / 2 / steps;
    for (int i = 0; i < steps; ++i) {
        const double th = (i + 0.5) * dth, w = std::sin(2 * th) * dth;
        const double e = w * fresnelUnpolarised(n, th) * std::pow(T, 2 / std::cos(th));
        const double r = 2 * t * std::tan(th);
        k.kappa += e;
        if (r < rMax) bins[std::size_t(std::floor(r / dr))] += e;
    }
    std::vector<double> K1(std::size_t(S) * S, 0.0);
    const int A = 96;
    for (std::size_t i = 0; i < bins.size(); ++i) {
        if (!(bins[i] != 0)) continue;
        const double r = (double(i) + 0.5) * dr;
        for (int a = 0; a < A; ++a) {
            const double ang = ((a + 0.5) * 2 * kPi) / A;
            const int x = int(jsRound((r * std::cos(ang)) / k.cellMm)), y = int(jsRound((r * std::sin(ang)) / k.cellMm));
            if (std::abs(x) <= R && std::abs(y) <= R) K1[std::size_t((y + R) * S + x + R)] += bins[i] / A;
        }
    }
    const std::vector<double> K2 = convolve(K1, K1, R), K3 = convolve(K2, K1, R);
    std::vector<double> K(K1.size());
    double total = 0;
    for (std::size_t i = 0; i < K.size(); ++i) { K[i] = rho * K1[i] + rho * rho * K2[i] + rho * rho * rho * K3[i]; total += K[i]; }
    const std::size_t centre = std::size_t(R * S + R);
    const double norm = 1 + total;
    k.direct = (1 + K[centre]) / norm;
    k.weights.resize(K.size());
    for (std::size_t i = 0; i < K.size(); ++i) k.weights[i] = i == centre ? 0.0f : float(K[i] / norm);
    return k;
}

CrtPlan resolveCrt(const CrtParams& p, int srcW, int srcH, int outW, int outH) {
    CrtPlan plan;
    plan.p = p; plan.srcW = srcW; plan.srcH = srcH; plan.outW = outW; plan.outH = outH;
    const double fsc = p.pal ? 4.43361875 : 315.0 / 88.0, lumaMHz = p.pal ? 5.0 : 4.2, iMHz = 1.3, qMHz = p.pal ? 1.3 : 0.4;
    plan.fs = 4 * fsc;
    plan.N = std::max(1, int(jsRound((srcW * plan.fs) / p.pixelClockMHz)));
    const double diag = p.diagonalIn * 25.4, a = p.aspect;
    plan.faceW = (diag * a) / std::hypot(a, 1.0); plan.faceH = diag / std::hypot(a, 1.0);
    plan.rasterW = plan.faceW * (1 + p.overscan); plan.rasterH = plan.faceH * (1 + p.overscan);
    plan.lines = srcH; plan.lineMm = plan.rasterH / plan.lines;
    plan.distance = 4 * plan.faceH;
    plan.viewW = (plan.faceW * (1 + p.border)) / p.zoom;
    plan.viewH = plan.viewW * (double(outH) / outW);
    plan.screen = crtScreen(p.screen);
    plan.field = p.pal ? 1.0 / 50 : 1 / 59.94;
    plan.halo = crtHaloKernel(p, plan.viewW / outW, 25);
    plan.lineCyc = (p.pal ? 270.6 : 180.0) / 360; plan.frameCyc = (p.pal ? 0.0 : 180.0) / 360;
    auto lp = [&](double mhz, int half) { return mhz > 0 ? lowpassTaps(std::min(0.49, mhz / plan.fs), half) : std::vector<double>{1.0}; };
    putTaps(plan, TapRgb, lp(p.rgbMHz, 8));
    putTaps(plan, TapEnc, lp(p.encodeChromaMHz, 12));
    putTaps(plan, TapSep, !p.comb ? notchTaps(0.25, 1.0 / plan.fs, 16) : std::vector<double>{1.0});
    putTaps(plan, TapLum, lp(lumaMHz, 12));
    putTaps(plan, TapI, lp(iMHz, 24));
    putTaps(plan, TapQ, lp(qMHz, 24));
    for (int c = 0; c < 3; ++c) {
        const auto& d = plan.screen.decay[std::size_t(c)];
        plan.decay[std::size_t(c)] = {d[0], std::exp(-plan.field / d[1]), std::exp(-plan.field / d[2]), d[1], d[2]};
    }
    return plan;
}

}  // namespace raw::style
