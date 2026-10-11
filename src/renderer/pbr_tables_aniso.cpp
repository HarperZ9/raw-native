// Anisotropic split albedo of the GGX lobe (raw/renderer/pbr.hpp, Tables::A4 and B4).
// The multiple-scattering term is exact only if E is the lobe's true directional albedo,
// and under anisotropy that albedo depends on the view's azimuth as well as its angle:
// reading the isotropic table at a projected roughness misses the masking of light that
// leaves along the wide axis (it lost up to 47% in the first furnace run, 2026-10-10).
#include "raw/renderer/pbr.hpp"
#include "raw/renderer/brdf.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
namespace raw::pbr {
namespace {
double radicalInverse(std::uint32_t b) {
    b = (b << 16) | (b >> 16);
    b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
    b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
    b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
    b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
    return b * 2.3283064365386963e-10;
}
D3 unit(D3 a) { const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return {a.x / l, a.y / l, a.z / l}; }
double lambdaAn(D3 w, double ax, double ay) {
    return 0.5 * (-1.0 + std::sqrt(1.0 + (ax * ax * w.x * w.x + ay * ay * w.y * w.y) / (w.z * w.z)));
}
// VNDF sampling of the anisotropic lobe (Heitz 2018) on a Hammersley set; weights as in
// the isotropic tables: G2 / G1 split by the Schlick factor.
// With hA and hB, each sample's weight is also shared between the two hats in u = sqrt(mu)
// around its reflected direction (the coat compensation's tables, Tables::HA4).
void splitAn(D3 v, double ax, double ay, int n, double& outA, double& outB, double* hA = nullptr, double* hB = nullptr) {
    constexpr int J = Tables::kHats;
    if (hA) { std::fill(hA, hA + J, 0.0); std::fill(hB, hB + J, 0.0); }
    const D3 vh = unit({ax * v.x, ay * v.y, v.z});
    const double lensq = vh.x * vh.x + vh.y * vh.y;
    const D3 t1 = lensq > 0 ? D3{-vh.y / std::sqrt(lensq), vh.x / std::sqrt(lensq), 0.0} : D3{1.0, 0.0, 0.0};
    const D3 t2{vh.y * t1.z - vh.z * t1.y, vh.z * t1.x - vh.x * t1.z, vh.x * t1.y - vh.y * t1.x};
    const double lv = lambdaAn(v, ax, ay), g1 = 1.0 / (1.0 + lv);
    double sa = 0.0, sb = 0.0;
    for (int k = 0; k < n; ++k) {
        const double u1 = (k + 0.5) / n, u2 = radicalInverse(std::uint32_t(k));
        const double r = std::sqrt(u1), phi = 2.0 * kPi * u2, s = 0.5 * (1.0 + vh.z);
        const double p1 = r * std::cos(phi), p2 = (1.0 - s) * std::sqrt(1.0 - p1 * p1) + s * r * std::sin(phi);
        const double p3 = std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2));
        const D3 nh{t1.x * p1 + t2.x * p2 + vh.x * p3, t1.y * p1 + t2.y * p2 + vh.y * p3, t1.z * p1 + t2.z * p2 + vh.z * p3};
        const D3 m = unit({ax * nh.x, ay * nh.y, std::max(1e-12, nh.z)});
        const double vom = v.x * m.x + v.y * m.y + v.z * m.z;
        const D3 l{2.0 * vom * m.x - v.x, 2.0 * vom * m.y - v.y, 2.0 * vom * m.z - v.z};
        if (l.z <= 0.0) continue;
        const double w = (1.0 / (1.0 + lv + lambdaAn(l, ax, ay))) / g1, c = 1.0 - vom, c5 = c * c * c * c * c;
        sa += w * (1.0 - c5); sb += w * c5;
        if (hA) {
            const double x = std::sqrt(std::min(l.z, 1.0)) * (J - 1);
            const int h0 = std::min(int(x), J - 2);
            const double f = x - h0;
            hA[h0] += w * (1.0 - c5) * (1.0 - f); hA[h0 + 1] += w * (1.0 - c5) * f;
            hB[h0] += w * c5 * (1.0 - f); hB[h0 + 1] += w * c5 * f;
        }
    }
    outA = sa / n; outB = sb / n;
    if (hA) for (int j = 0; j < J; ++j) { hA[j] /= n; hB[j] /= n; }
}
double phiAt(int j) { return 0.5 * kPi * j / (Tables::kAnPhi - 1); }   // uniform; the sin^2(phi) axis of method note 3 was worse near 90 degrees and is reverted
double roughAt(int j) { return Tables::kRoughMax * j / (Tables::kAnRough - 1); }
double kAt(int j) { return std::sqrt(double(j) / (Tables::kAnK - 1)); }   // the k^2 axis (method note 2 of 2026-10-11)
// Fractional index on a uniform grid of n nodes, and the lower node.
void locate(double x, int n, int& i0, double& f) {
    const double c = std::clamp(x, 0.0, n - 1.0);
    i0 = std::min(int(c), n - 2); f = c - i0;
}
}  // namespace

void Tables::buildAnisotropic() {
    const std::size_t per = std::size_t(kAnMu) * kAnPhi;
    an_a_.assign(per * kAnRough * kAnK, 0.0); an_b_.assign(an_a_.size(), 0.0);
    const std::size_t all = per * kAnRough * kAnK;
    ha4_.assign(all * kHats, 0.0); hb4_.assign(all * kHats, 0.0);   // [j][k][r][phi][mu]
    const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    parallelRows(kAnK * kAnRough, threads, [&](int row) {
        const int ik = row / kAnRough, ir = row % kAnRough;
        const double a = std::max(roughAt(ir) * roughAt(ir), 1e-4), k = kAt(ik);
        const double ax = a + (1.0 - a) * k * k, ay = a;
        for (int ip = 0; ip < kAnPhi; ++ip) for (int im = 0; im < kAnMu; ++im) {
            const double u = double(im) / (kAnMu - 1), mu = std::max(u * u, 1e-4), s = std::sqrt(1.0 - mu * mu);   // the u = sqrt(mu) axis
            const std::size_t at = std::size_t(row) * per + std::size_t(ip) * kAnMu + im;
            double hA[kHats], hB[kHats];
            splitAn({s * std::cos(phiAt(ip)), s * std::sin(phiAt(ip)), mu}, ax, ay, kAnSamples, an_a_[at], an_b_[at], hA, hB);
            for (int j = 0; j < kHats; ++j) { ha4_[std::size_t(j) * all + at] = hA[j]; hb4_[std::size_t(j) * all + at] = hB[j]; }
        }
    });
    // Hemispherical means from the table itself: (4 / pi) * integral over phi in [0, pi/2]
    // and mu in [0, 1] of A(mu, phi) mu.
    std::vector<double> x, w;
    brdf::gaussLegendre01(32, x, w);
    an_aavg_.assign(std::size_t(kAnRough) * kAnK, 0.0); an_bavg_.assign(an_aavg_.size(), 0.0);
    for (int ik = 0; ik < kAnK; ++ik) for (int ir = 0; ir < kAnRough; ++ir) {
        double sa = 0.0, sb = 0.0;
        for (std::size_t p = 0; p < x.size(); ++p) for (std::size_t q = 0; q < x.size(); ++q) {
            const double ph = 0.5 * kPi * x[p], mu = x[q], ww = w[p] * w[q] * mu;
            sa += ww * A4(mu, ph, roughAt(ir), kAt(ik)); sb += ww * B4(mu, ph, roughAt(ir), kAt(ik));
        }
        an_aavg_[std::size_t(ik) * kAnRough + ir] = 2.0 * sa; an_bavg_[std::size_t(ik) * kAnRough + ir] = 2.0 * sb;
    }
    buildHatMeans4(x, w);
}

double Tables::read4(const std::vector<double>& g, double mu, double phi, double r, double k, std::size_t offset) const {
    double ph = std::fmod(std::fabs(phi), kPi);
    if (ph > 0.5 * kPi) ph = kPi - ph;                     // the albedo depends on cos^2 and sin^2 of the azimuth
    int i[4]; double f[4];
    locate(std::sqrt(std::clamp(mu, 0.0, 1.0)) * (kAnMu - 1), kAnMu, i[0], f[0]);
    locate(ph / (0.5 * kPi) * (kAnPhi - 1), kAnPhi, i[1], f[1]);
    locate(r / kRoughMax * (kAnRough - 1), kAnRough, i[2], f[2]);
    locate(k * k * (kAnK - 1), kAnK, i[3], f[3]);
    double s = 0.0;
    for (int c = 0; c < 16; ++c) {
        double wgt = 1.0; int idx[4];
        for (int d = 0; d < 4; ++d) { const int b = (c >> d) & 1; idx[d] = i[d] + b; wgt *= b ? f[d] : 1.0 - f[d]; }
        s += wgt * g[offset + ((std::size_t(idx[3]) * kAnRough + idx[2]) * kAnPhi + idx[1]) * kAnMu + idx[0]];
    }
    return s;
}
// A (k rows) x (r rows) grid of `stride` values each, read bilinearly at value `at`.
double Tables::readRk(const std::vector<double>& g, double r, double k, std::size_t at, int stride) const {
    int ir, ik; double fr, fk;
    locate(r / kRoughMax * (kAnRough - 1), kAnRough, ir, fr);
    locate(k * k * (kAnK - 1), kAnK, ik, fk);
    const auto v = [&](int a, int b) { return g[(std::size_t(a) * kAnRough + b) * std::size_t(stride) + at]; };
    return (v(ik, ir) * (1 - fr) + v(ik, ir + 1) * fr) * (1 - fk) + (v(ik + 1, ir) * (1 - fr) + v(ik + 1, ir + 1) * fr) * fk;
}
double Tables::read2rk(const std::vector<double>& g, double r, double k) const {
    int ir, ik; double fr, fk;
    locate(r / kRoughMax * (kAnRough - 1), kAnRough, ir, fr);
    locate(k * k * (kAnK - 1), kAnK, ik, fk);
    const auto at = [&](int a, int b) { return g[std::size_t(a) * kAnRough + b]; };
    return (at(ik, ir) * (1 - fr) + at(ik, ir + 1) * fr) * (1 - fk) + (at(ik + 1, ir) * (1 - fr) + at(ik + 1, ir + 1) * fr) * fk;
}

}  // namespace raw::pbr
