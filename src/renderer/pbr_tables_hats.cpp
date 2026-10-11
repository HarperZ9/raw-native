// The coat compensation's tables (raw/renderer/pbr.hpp, Tables::HA and the rest;
// evidence/m3-materials2-bounds.json, method note 1 of 2026-10-11). The coat's albedo at the
// direction a base reflects into is represented on hats in u = sqrt(mu); these tables hold how
// much of the white GGX lobe's energy lands in each hat, so the base's coat-weighted albedo Q
// is a short sum instead of an integral.
#include "raw/renderer/pbr.hpp"
#include "raw/renderer/brdf.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
namespace raw::pbr {
namespace {
constexpr int J = Tables::kHats, NM = Tables::kHatMu, NR = Tables::kHatRough;
double radicalInverse(std::uint32_t b) {
    b = (b << 16) | (b >> 16);
    b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
    b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
    b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
    b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
    return b * 2.3283064365386963e-10;
}
double lambda(double mu, double a) { const double m2 = mu * mu; return 0.5 * (-1.0 + std::sqrt(1.0 + a * a * (1.0 - m2) / m2)); }
double roughAt(int i) { return Tables::kRoughMax * i / (NR - 1); }
// The isotropic lobe's split albedo by hat of the reflected direction's cosine: VNDF sampling
// (Heitz 2018) on a Hammersley set, as the A and B tables, each sample's weight shared between
// the two hats around its u.
void splitHats(double mu, double a, int n, double* outA, double* outB) {
    const double vx = std::sqrt(std::max(0.0, 1.0 - mu * mu)), vz = mu;
    const double lv = std::sqrt(a * a * vx * vx + vz * vz), nx = a * vx / lv, nz = vz / lv;
    const double g1 = 1.0 / (1.0 + lambda(mu, a));
    std::fill(outA, outA + J, 0.0); std::fill(outB, outB + J, 0.0);
    for (int k = 0; k < n; ++k) {
        const double u1 = (k + 0.5) / n, u2 = radicalInverse(std::uint32_t(k));
        const double r = std::sqrt(u1), phi = 2.0 * kPi * u2, s = 0.5 * (1.0 + nz);
        const double p1 = r * std::cos(phi), p2 = (1.0 - s) * std::sqrt(1.0 - p1 * p1) + s * r * std::sin(phi);
        const double p3 = std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2));
        // t1 = (0, 1, 0), t2 = cross(n, t1) = (-nz, 0, nx)
        double mx = -nz * p2 + nx * p3, my = p1, mz = nx * p2 + nz * p3;
        mx *= a; my *= a; mz = std::max(1e-12, mz);
        const double lm = std::sqrt(mx * mx + my * my + mz * mz);
        mx /= lm; my /= lm; mz /= lm;
        const double vom = vx * mx + vz * mz, li = 2.0 * vom * mz - vz;
        if (li <= 0.0) continue;
        const double w = (1.0 / (1.0 + lambda(mu, a) + lambda(li, a))) / g1;
        const double c = 1.0 - vom, c5 = c * c * c * c * c;
        const double x = std::sqrt(std::min(li, 1.0)) * (J - 1);
        const int h0 = std::min(int(x), J - 2);
        const double f = x - h0;
        outA[h0] += w * (1.0 - c5) * (1.0 - f); outA[h0 + 1] += w * (1.0 - c5) * f;
        outB[h0] += w * c5 * (1.0 - f); outB[h0 + 1] += w * c5 * f;
    }
    for (int j = 0; j < J; ++j) { outA[j] /= n; outB[j] /= n; }
}
void locate(double x, int n, int& i0, double& f) {
    const double c = std::clamp(x, 0.0, n - 1.0);
    i0 = std::min(int(c), n - 2); f = c - i0;
}
double rowRead(const std::vector<double>& g, int stride, int offset, double r) {
    int i0; double f;
    locate(r / Tables::kRoughMax * (NR - 1), NR, i0, f);
    return g[std::size_t(i0) * stride + offset] * (1.0 - f) + g[std::size_t(i0 + 1) * stride + offset] * f;
}
}  // namespace

double Tables::hat(int j, double mu) {
    const double x = std::sqrt(std::clamp(mu, 0.0, 1.0)) * (kHats - 1);
    return std::max(0.0, 1.0 - std::fabs(x - j));
}

void Tables::buildHats() {
    ha_.assign(std::size_t(NR) * J * NM, 0.0); hb_.assign(ha_.size(), 0.0);
    const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    parallelRows(NR, threads, [&](int i) {
        const double a = std::max(roughAt(i) * roughAt(i), 1e-4);
        double sa[J], sb[J];
        for (int c = 0; c < NM; ++c) {
            const double u = double(c) / (NM - 1), mu = std::max(u * u, 1e-4);
            splitHats(mu, a, kSamples, sa, sb);
            for (int j = 0; j < J; ++j) { ha_[(std::size_t(i) * J + j) * NM + c] = sa[j]; hb_[(std::size_t(i) * J + j) * NM + c] = sb[j]; }
        }
    });
    // Moments by Gauss-Legendre on u (mu = u^2, mu dmu = 2 u^3 du), from the tables as read.
    std::vector<double> x, w;
    brdf::gaussLegendre01(128, x, w);
    g1_.assign(J, 0.0);
    for (int j = 0; j < J; ++j) for (std::size_t q = 0; q < x.size(); ++q) g1_[std::size_t(j)] += 4.0 * w[q] * hat(j, x[q] * x[q]) * x[q] * x[q] * x[q];
    // The coat albedo's projection onto the hats per coat roughness row: the cosine-weighted Gram
    // system is tridiagonal (neighbouring hats overlap), solved directly.
    coef_.assign(std::size_t(NR) * J, 0.0);
    {
        std::vector<double> gx, gw;
        brdf::gaussLegendre01(384, gx, gw);
        std::vector<double> gram(std::size_t(J) * J, 0.0);
        for (std::size_t q = 0; q < gx.size(); ++q) {
            const double u = gx[q], mu = u * u, ww = 4.0 * gw[q] * u * u * u;
            for (int j = 0; j < J; ++j) for (int l = 0; l < J; ++l) gram[std::size_t(j) * J + l] += ww * hat(j, mu) * hat(l, mu);
        }
        for (int i = 0; i < NR; ++i) {
            const double rc = std::max(roughAt(i), kMinRoughness);
            const double kc = [&] { const double f = 0.04 + 0.96 / 21.0, e = Eavg(rc); return f * f * e / (1.0 - f * (1.0 - e)); }();
            std::vector<double> m = gram, b(J, 0.0);
            for (std::size_t q = 0; q < gx.size(); ++q) {
                const double u = gx[q], mu = u * u, ww = 4.0 * gw[q] * u * u * u;
                const double ec = 0.04 * A(mu, rc) + B(mu, rc) + (1.0 - E(mu, rc)) * kc;
                for (int j = 0; j < J; ++j) b[std::size_t(j)] += ww * hat(j, mu) * ec;
            }
            for (int c = 0; c < J; ++c) for (int rr = 0; rr < J; ++rr) {
                if (rr == c) continue;
                const double f = m[std::size_t(rr) * J + c] / m[std::size_t(c) * J + c];
                for (int q = 0; q < J; ++q) m[std::size_t(rr) * J + q] -= f * m[std::size_t(c) * J + q];
                b[std::size_t(rr)] -= f * b[std::size_t(c)];
            }
            for (int j = 0; j < J; ++j) coef_[std::size_t(i) * J + j] = b[std::size_t(j)] / m[std::size_t(j) * J + j];
        }
    }
    hbar_.assign(std::size_t(NR) * J * 2, 0.0); hh_.assign(std::size_t(NR) * J * J * 2, 0.0); g_.assign(std::size_t(NR) * J * 2, 0.0);
    for (int i = 0; i < NR; ++i) {
        const double r = roughAt(i);
        for (std::size_t q = 0; q < x.size(); ++q) {
            const double u = x[q], mu = u * u, ww = 4.0 * w[q] * u * u * u, ea = A(mu, r), eb = B(mu, r);
            for (int j = 0; j < J; ++j) {
                const double hj = hat(j, mu), haj = HA(j, mu, r), hbj = HB(j, mu, r);
                g_[(std::size_t(i) * J + j) * 2] += ww * ea * hj; g_[(std::size_t(i) * J + j) * 2 + 1] += ww * eb * hj;
                hbar_[(std::size_t(i) * J + j) * 2] += ww * haj; hbar_[(std::size_t(i) * J + j) * 2 + 1] += ww * hbj;
                for (int l = 0; l < J; ++l) {
                    const double hl = hat(l, mu);
                    if (hl == 0.0) continue;
                    hh_[((std::size_t(i) * J + j) * J + l) * 2] += ww * hl * haj; hh_[((std::size_t(i) * J + j) * J + l) * 2 + 1] += ww * hl * hbj;
                }
            }
        }
    }
}
// Hemispherical means of the anisotropic hat tables, per (k, r) row, by Gauss-Legendre over
// mu and the azimuth quadrant: HAbar4, HBbar4, then GA4, GB4 (moments of A4 and B4), then
// HAA4, HBB4 (against each hat).
void Tables::buildHatMeans4(const std::vector<double>& x, const std::vector<double>& w) {
    hm4_.assign(std::size_t(kAnK) * kAnRough * kHm4, 0.0);
    parallelRows(kAnK * kAnRough, int(std::max(1u, std::thread::hardware_concurrency())), [&](int row) {
        const int ik = row / kAnRough, ir = row % kAnRough;
        const double r = kRoughMax * ir / (kAnRough - 1), k = std::sqrt(double(ik) / (kAnK - 1));   // the k^2 axis
        double* out = &hm4_[std::size_t(row) * kHm4];
        for (std::size_t p = 0; p < x.size(); ++p) for (std::size_t q = 0; q < x.size(); ++q) {
            const double ph = 0.5 * kPi * x[p], mu = x[q], ww = 2.0 * w[p] * w[q] * mu;
            const double a4 = A4(mu, ph, r, k), b4 = B4(mu, ph, r, k);
            for (int j = 0; j < J; ++j) {
                const double ha = HA4(j, mu, ph, r, k), hb = HB4(j, mu, ph, r, k), hj = hat(j, mu);
                out[j * 2] += ww * ha; out[j * 2 + 1] += ww * hb;
                out[J * 2 + j * 2] += ww * a4 * hj; out[J * 2 + j * 2 + 1] += ww * b4 * hj;
                for (int l = 0; l < J; ++l) {
                    const double hl = hat(l, mu);
                    if (hl == 0.0) continue;
                    out[J * 4 + (j * J + l) * 2] += ww * hl * ha; out[J * 4 + (j * J + l) * 2 + 1] += ww * hl * hb;
                }
            }
        }
    });
}
double Tables::coef(int j, double rc) const { return rowRead(coef_, J, j, rc); }
double Tables::HA(int j, double mu, double r) const {
    int x0, y0; double fx, fy;
    locate(std::sqrt(std::clamp(mu, 0.0, 1.0)) * (NM - 1), NM, x0, fx);
    locate(r / kRoughMax * (NR - 1), NR, y0, fy);
    const auto at = [&](int i, int c) { return ha_[(std::size_t(i) * J + j) * NM + c]; };
    return (at(y0, x0) * (1 - fx) + at(y0, x0 + 1) * fx) * (1 - fy) + (at(y0 + 1, x0) * (1 - fx) + at(y0 + 1, x0 + 1) * fx) * fy;
}
double Tables::HB(int j, double mu, double r) const {
    int x0, y0; double fx, fy;
    locate(std::sqrt(std::clamp(mu, 0.0, 1.0)) * (NM - 1), NM, x0, fx);
    locate(r / kRoughMax * (NR - 1), NR, y0, fy);
    const auto at = [&](int i, int c) { return hb_[(std::size_t(i) * J + j) * NM + c]; };
    return (at(y0, x0) * (1 - fx) + at(y0, x0 + 1) * fx) * (1 - fy) + (at(y0 + 1, x0) * (1 - fx) + at(y0 + 1, x0 + 1) * fx) * fy;
}
double Tables::HAbar(int j, double r) const { return rowRead(hbar_, J * 2, j * 2, r); }
double Tables::HBbar(int j, double r) const { return rowRead(hbar_, J * 2, j * 2 + 1, r); }
double Tables::HAA(int j, int l, double r) const { return rowRead(hh_, J * J * 2, (j * J + l) * 2, r); }
double Tables::HBB(int j, int l, double r) const { return rowRead(hh_, J * J * 2, (j * J + l) * 2 + 1, r); }
double Tables::GA(int j, double r) const { return rowRead(g_, J * 2, j * 2, r); }
double Tables::GB(int j, double r) const { return rowRead(g_, J * 2, j * 2 + 1, r); }

}  // namespace raw::pbr
