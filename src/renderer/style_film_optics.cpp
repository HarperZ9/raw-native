// The film's optics and grain: the base-glass halo kernel (web/shaders/crt/halo.mjs) and the
// Boolean-model grain (web/shaders/film/grain.mjs). See raw/renderer/style_film.hpp. Operations
// are in the JavaScript's order; a float here is a Float32Array element there.
#include "raw/renderer/style_film.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
constexpr int kJN = 33, kMuLevels = 512, kPoisK = 16;
const double kMuMax = -std::log(0.001) / kPi;
int jsRound(double x) { return int(std::floor(x + 0.5)); }

double fresnel(double n, double theta) {
    const double s = n * std::sin(theta);
    if (s >= 1) return 1;
    const double ci = std::cos(theta), ct = std::sqrt(1 - s * s);
    const double rs = (n * ci - ct) / (n * ci + ct), rp = (n * ct - ci) / (n * ct + ci);
    return 0.5 * (rs * rs + rp * rp);
}
double backPlate(double t, double n, double T, double R, std::vector<double>& bins, double dr, double rMax) {
    const double thc = std::asin(1 / n);
    const int A = 160, B = 80, C = 48;
    double total = 0;
    for (int i = 0; i < A; ++i) {
        const double th = ((i + 0.5) * thc) / A, w = std::sin(2 * th) * (thc / A) * (1 - fresnel(n, th)) * std::pow(T, 1 / std::cos(th)) * R * 0.92;
        const double r1 = t * std::tan(th);
        for (int j = 0; j < B; ++j) {
            const double s2 = (j + 0.5) / B, th2 = std::asin(std::sqrt(s2) / n), r2 = t * std::tan(th2), w2 = (w / B) * std::pow(T, 1 / std::cos(th2));
            for (int k = 0; k < C; ++k) {
                const double r = std::sqrt(r1 * r1 + r2 * r2 + 2 * r1 * r2 * std::cos(((k + 0.5) * 2 * kPi) / C));
                if (r < rMax) bins[std::size_t(std::floor(r / dr))] += w2 / C;
                total += w2 / C;
            }
        }
    }
    return total;
}
double radialReturn(const BaseGlass& g, double dr, double rMax, std::vector<double>& bins) {
    bins.assign(std::size_t(std::ceil(rMax / dr)) + 1, 0.0);
    const int steps = 40000;
    const double dth = kPi / 2 / steps;
    double kappa = 0;
    for (int i = 0; i < steps; ++i) {
        const double th = (i + 0.5) * dth, w = std::sin(2 * th) * dth;
        const double e = w * fresnel(g.n, th) * std::pow(g.transmission, 2 / std::cos(th));
        const double r = 2 * g.thickness * std::tan(th);
        kappa += e;
        if (r < rMax) bins[std::size_t(std::floor(r / dr))] += e;
    }
    if (g.backReflectance > 0) kappa += backPlate(g.thickness, g.n, g.transmission, g.backReflectance, bins, dr, rMax);
    return kappa;
}
std::vector<double> convolve(const std::vector<double>& a, const std::vector<double>& b, int R) {
    const int S = 2 * R + 1;
    std::vector<double> o(static_cast<std::size_t>(S) * S, 0.0);
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
double lens(double s) { return 2 * std::acos(s / 2) - (s / 2) * std::sqrt(4 - s * s); }
double tableAt(const std::vector<float>& t, double u) {
    const double f = std::min(double(kJN - 1), std::max(0.0, u * (kJN - 1)));
    const int i = std::min(kJN - 2, int(std::floor(f)));
    return double(t[std::size_t(i)]) + (double(t[std::size_t(i + 1)]) - double(t[std::size_t(i)])) * (f - i);
}
int poissonCount(double mu, std::uint32_t h) {
    const auto& P = filmPoissonTable();
    const int m = std::min(kMuLevels - 1, std::max(0, int(std::floor((mu / kMuMax) * kMuLevels))));
    int k = 0;
    while (k < kPoisK && h >= P[std::size_t(m * kPoisK + k)]) k++;
    return k;
}
double uAt(const std::vector<float>& F, int w, int x, int y, int c) { return F[(std::size_t(y) * w + std::size_t(x)) * 4 + std::size_t(c)]; }
double grainMC(const FilmPlan& plan, int px, int py, int c, double rUm, int frame, const std::vector<float>& F) {
    const double um = plan.umPerPx, rPx = rUm / um;
    const int W = plan.outW, H = plan.outH;
    const int ns = int(std::min(64.0, std::max(8.0, std::ceil(12 / (kPi * rPx * rPx)))));
    const double d = rUm;
    const std::uint32_t seed = std::uint32_t(frame * 3 + c);
    double hit = 0;
    for (int s = 0; s < ns; ++s) {
        const double x = (px + hash3(std::uint32_t(px), std::uint32_t(py), seed * 131u + 2u * std::uint32_t(s))) * um;
        const double y = (py + hash3(std::uint32_t(px), std::uint32_t(py), seed * 131u + 2u * std::uint32_t(s) + 1u)) * um;
        const int cx0 = int(std::floor(x / d)), cy0 = int(std::floor(y / d));
        double cov = 0;
        for (int cy = cy0 - 1; cy <= cy0 + 1 && cov < 1; ++cy) for (int cx = cx0 - 1; cx <= cx0 + 1 && cov < 1; ++cx) {
            const int qx = std::min(W - 1, std::max(0, int(std::floor(((cx + 0.5) * d) / um)))), qy = std::min(H - 1, std::max(0, int(std::floor(((cy + 0.5) * d) / um))));
            const double u = std::min(0.999, uAt(F, W, qx, qy, c)), mu = -std::log(1 - u) / kPi;
            const std::uint32_t ucx = std::uint32_t(cx), ucy = std::uint32_t(cy);
            const int k = poissonCount(mu, pcg(ucx ^ pcg(ucy ^ pcg(seed * 977u + 1u))));
            for (int gI = 0; gI < k; ++gI) {
                const double gx = (cx + hash3(ucx, ucy, seed * 977u + 2u + 2u * std::uint32_t(gI))) * d, gy = (cy + hash3(ucx, ucy, seed * 977u + 3u + 2u * std::uint32_t(gI))) * d;
                const double e = std::min(1.0, std::max(0.0, 0.5 + (rUm - std::hypot(x - gx, y - gy)) / (0.05 * rUm)));
                if (e > cov) cov = e;
            }
        }
        hit += cov;
    }
    return hit / ns;
}
double grainAnalytic(const FilmPlan& plan, int px, int py, int c, double u, double rUm, int frame) {
    const std::uint32_t seed = std::uint32_t(frame * 3 + c);
    const double u1 = std::max(1e-7, hash3(std::uint32_t(px), std::uint32_t(py), seed * 131u + 200u)), u2 = hash3(std::uint32_t(px), std::uint32_t(py), seed * 131u + 201u);
    const double z = std::sqrt(-2 * std::log(u1)) * std::cos(2 * kPi * u2);
    return u + std::sqrt(tableAt(plan.jTable, u) * (rUm * rUm) / (plan.umPerPx * plan.umPerPx)) * z;
}
}  // namespace

double filmRadialKappa(const BaseGlass& g, double dr, double rMax) { std::vector<double> b; return radialReturn(g, dr, rMax, b); }

HaloKernel filmHaloKernel(const BaseGlass& g, double mmPerPx, int maxCells) {
    HaloKernel hk;
    const double t = g.thickness, n = g.n, rho = g.albedo;
    hk.ringMm = 2 * t * std::tan(std::asin(1 / n));
    const double reach = 3.2 * hk.ringMm;
    int q = std::max(1, jsRound((g.cell > 0 ? g.cell : 1) / mmPerPx));
    while ((maxCells * q * mmPerPx) < reach) q++;
    const double cellMm = q * mmPerPx;
    const int R = std::min(maxCells, int(std::ceil(reach / cellMm))), S = 2 * R + 1;
    const double dr = cellMm / 8;
    std::vector<double> bins;
    hk.kappa = radialReturn(g, dr, (R + 0.5) * cellMm, bins);
    std::vector<double> K1(static_cast<std::size_t>(S) * S, 0.0);
    const int A = 96;
    for (std::size_t i = 0; i < bins.size(); ++i) {
        if (bins[i] == 0) continue;
        const double r = (double(i) + 0.5) * dr;
        for (int k = 0; k < A; ++k) {
            const double a = ((k + 0.5) * 2 * kPi) / A;
            const int x = jsRound((r * std::cos(a)) / cellMm), y = jsRound((r * std::sin(a)) / cellMm);
            if (std::abs(x) <= R && std::abs(y) <= R) K1[std::size_t((y + R) * S + x + R)] += bins[i] / A;
        }
    }
    const std::vector<double> K2 = convolve(K1, K1, R), K3 = convolve(K2, K1, R);
    std::vector<double> K(K1.size());
    double total = 0;
    for (std::size_t i = 0; i < K.size(); ++i) { K[i] = rho * K1[i] + rho * rho * K2[i] + rho * rho * rho * K3[i]; total += K[i]; }
    const std::size_t centre = std::size_t(R * S + R);
    const double norm = 1 + total;
    hk.weights.resize(K.size());
    for (std::size_t i = 0; i < K.size(); ++i) hk.weights[i] = i == centre ? 0.0f : float(K[i] / norm);
    hk.q = q; hk.R = R;
    return hk;
}

std::vector<float> filmVarianceTable() {
    std::vector<float> t(kJN);
    for (int i = 0; i < kJN; ++i) {
        const double u = std::min(0.995, double(i) / (kJN - 1)), lr = -std::log(1 - u) / kPi;
        double acc = 0;
        const int n = 400;
        for (int k = 0; k < n; ++k) { const double s = ((k + 0.5) * 2) / n; acc += (std::exp(lr * lens(s)) - 1) * 2 * kPi * s * (2.0 / n); }
        t[std::size_t(i)] = float((1 - u) * (1 - u) * acc);
    }
    return t;
}
const std::vector<std::uint32_t>& filmPoissonTable() {
    static const std::vector<std::uint32_t> T = [] {
        std::vector<std::uint32_t> t(static_cast<std::size_t>(kMuLevels) * kPoisK);
        for (int m = 0; m < kMuLevels; ++m) {
            const double mu = ((m + 0.5) * kMuMax) / kMuLevels;
            double p = std::exp(-mu), F = p;
            for (int k = 0; k < kPoisK; ++k) { t[std::size_t(m * kPoisK + k)] = std::uint32_t(std::min(4294967295.0, std::floor(F * 4294967296.0))); p *= mu / (k + 1); F += p; }
        }
        return t;
    }();
    return T;
}

double filmGrainAt(const FilmPlan& plan, int px, int py, int c, const std::vector<float>& F, int frame) {
    const double rUm = plan.grainUm[std::size_t(c)], u = uAt(F, plan.outW, px, py, c), rPx = rUm / plan.umPerPx;
    const double tt = std::min(1.0, std::max(0.0, (rPx - 0.25) / (0.4 - 0.25))), w = tt * tt * (3 - 2 * tt);
    if (rUm <= 0) return u;
    double v;
    if (w >= 1) v = grainMC(plan, px, py, c, rUm, frame, F);
    else if (w <= 0) v = grainAnalytic(plan, px, py, c, u, rUm, frame);
    else {
        const double a = grainMC(plan, px, py, c, rUm, frame, F) - u, b = grainAnalytic(plan, px, py, c, u, rUm, frame) - u;
        v = u + w * a + std::sqrt(1 - w * w) * b;
    }
    const double cs = plan.grainContrast[std::size_t(c)] * plan.p.grainAmount;
    return std::min(1.0, std::max(0.0, u + (v - u) * cs));
}

}  // namespace raw::style
