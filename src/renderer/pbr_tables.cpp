// Energy tables of the glTF material model: see raw/renderer/pbr.hpp.
#include "raw/renderer/pbr.hpp"
#include "raw/renderer/brdf.hpp"
#include "raw/core/parallel.hpp"
#include <thread>
#include <algorithm>
#include <cmath>
#include <cstdint>
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
double lambda(double mu, double a) {
    const double m2 = mu * mu;
    return 0.5 * (-1.0 + std::sqrt(1.0 + a * a * (1.0 - m2) / m2));
}

// Split albedo of the isotropic white lobe by VNDF sampling (Heitz 2018) on a Hammersley
// set: the estimator G2 / G1 carries F = 1; the Schlick split weights it by
// 1 - (1 - v.m)^5 (into A) and (1 - v.m)^5 (into B).
void albedoSplit(double mu, double a, int n, double& outA, double& outB) {
    const D3 v{std::sqrt(std::max(0.0, 1.0 - mu * mu)), 0.0, mu};
    const D3 vh{a * v.x, 0.0, v.z};
    const double lv = std::sqrt(vh.x * vh.x + vh.z * vh.z);
    const D3 vn{vh.x / lv, 0.0, vh.z / lv};
    const D3 t1{0.0, 1.0, 0.0};                                    // (-vh.y, vh.x, 0) normalised, vh.y = 0
    const D3 t2{vn.y * t1.z - vn.z * t1.y, vn.z * t1.x - vn.x * t1.z, vn.x * t1.y - vn.y * t1.x};   // cross(vn, t1)
    const double g1 = 1.0 / (1.0 + lambda(mu, a));
    double sa = 0.0, sb = 0.0;
    for (int k = 0; k < n; ++k) {
        const double u1 = (k + 0.5) / n, u2 = radicalInverse(std::uint32_t(k));
        const double r = std::sqrt(u1), phi = 2.0 * kPi * u2, s = 0.5 * (1.0 + vn.z);
        const double p1 = r * std::cos(phi), p2 = (1.0 - s) * std::sqrt(1.0 - p1 * p1) + s * r * std::sin(phi);
        const double p3 = std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2));
        D3 m{t1.x * p1 + t2.x * p2 + vn.x * p3, t1.y * p1 + t2.y * p2 + vn.y * p3, t1.z * p1 + t2.z * p2 + vn.z * p3};
        m = {a * m.x, a * m.y, std::max(1e-12, m.z)};
        const double lm = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
        m = {m.x / lm, m.y / lm, m.z / lm};
        const double vom = v.x * m.x + v.y * m.y + v.z * m.z, li = 2.0 * vom * m.z - v.z;
        if (li <= 0.0) continue;
        const double w = (1.0 / (1.0 + lambda(mu, a) + lambda(li, a))) / g1;
        const double c = 1.0 - vom, c5 = c * c * c * c * c;
        sa += w * (1.0 - c5); sb += w * c5;
    }
    outA = sa / n; outB = sb / n;
}

// Sum of l * G2 / G1 over VNDF samples (view in the xz plane at +x): the centroid of f cos.
void lobeCentroid(double mu, double a, int n, double& cx, double& cz) {
    const D3 v{std::sqrt(std::max(0.0, 1.0 - mu * mu)), 0.0, mu};
    const double lv = std::sqrt(a * a * v.x * v.x + v.z * v.z);
    const D3 vn{a * v.x / lv, 0.0, v.z / lv};
    const D3 t1{0.0, 1.0, 0.0};
    const D3 t2{vn.y * t1.z - vn.z * t1.y, vn.z * t1.x - vn.x * t1.z, vn.x * t1.y - vn.y * t1.x};
    const double g1 = 1.0 / (1.0 + lambda(mu, a));
    for (int k = 0; k < n; ++k) {
        const double u1 = (k + 0.5) / n, u2 = radicalInverse(std::uint32_t(k));
        const double r = std::sqrt(u1), phi = 2.0 * kPi * u2, s = 0.5 * (1.0 + vn.z);
        const double p1 = r * std::cos(phi), p2 = (1.0 - s) * std::sqrt(1.0 - p1 * p1) + s * r * std::sin(phi);
        const double p3 = std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2));
        D3 m{t1.x * p1 + t2.x * p2 + vn.x * p3, t1.y * p1 + t2.y * p2 + vn.y * p3, t1.z * p1 + t2.z * p2 + vn.z * p3};
        m = {a * m.x, a * m.y, std::max(1e-12, m.z)};
        const double lm = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
        m = {m.x / lm, m.y / lm, m.z / lm};
        const double vom = v.x * m.x + v.y * m.y + v.z * m.z;
        const D3 l{2.0 * vom * m.x - v.x, 2.0 * vom * m.y - v.y, 2.0 * vom * m.z - v.z};
        if (l.z <= 0.0) continue;
        const double w = (1.0 / (1.0 + lambda(mu, a) + lambda(l.z, a))) / g1;
        cx += w * l.x; cz += w * l.z;
    }
}

// The white sheen lobe's albedo by Gauss-Legendre over the incident hemisphere.
double sheenAlbedo(double mu, double rs, const std::vector<double>& x, const std::vector<double>& w) {
    const D3 o{std::sqrt(std::max(0.0, 1.0 - mu * mu)), 0.0, mu};
    double s = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double th = 0.5 * kPi * x[i], ci = std::cos(th), si = std::sin(th);
        for (std::size_t j = 0; j < x.size(); ++j) {
            const double ph = kPi * x[j];
            const D3 in{si * std::cos(ph), si * std::sin(ph), ci};
            D3 h{o.x + in.x, o.y + in.y, o.z + in.z};
            const double l = std::sqrt(h.x * h.x + h.y * h.y + h.z * h.z);
            h = {h.x / l, h.y / l, h.z / l};
            s += w[i] * w[j] * charlieD(h, rs) * charlieV(mu, ci, rs) * ci * si;
        }
    }
    return s * 2.0 * kPi * 0.5 * kPi;      // phi over [0, pi] doubled; theta over [0, pi/2]
}

// The sheen table's view axis is u = sqrt(mu): the sheen albedo rises from 0 at mu = 0 to its
// peak within a few hundredths, which a grid in theta sampled too coarsely (the albedo
// scaling then let the grazing furnace reach 1.049, 2026-10-10).
double sheenMu(int j) { const double u = double(j) / (Tables::kSheenMu - 1); return u * u; }
double gridTheta(int j, int n) { return -Tables::kThetaPad + (kPi / 2 + Tables::kThetaPad) * j / (n - 1); }
double gridRough(int i, int n) { return Tables::kRoughMax * i / (n - 1); }

// Bilinear read of a (rough rows) x (theta columns) grid.
double read2(const std::vector<double>& g, int nt, int nr, double mu, double r) {
    const double th = std::acos(std::clamp(mu, 0.0, 1.0));
    const double x = std::clamp((th + Tables::kThetaPad) / (kPi / 2 + Tables::kThetaPad) * (nt - 1), 0.0, nt - 1.0);
    const double y = std::clamp(r / Tables::kRoughMax * (nr - 1), 0.0, nr - 1.0);
    const int x0 = std::min(int(x), nt - 2), y0 = std::min(int(y), nr - 2);
    const double fx = x - x0, fy = y - y0;
    const auto at = [&](int i, int j) { return g[std::size_t(i) * nt + j]; };
    return (at(y0, x0) * (1 - fx) + at(y0, x0 + 1) * fx) * (1 - fy) + (at(y0 + 1, x0) * (1 - fx) + at(y0 + 1, x0 + 1) * fx) * fy;
}
double read1(const std::vector<double>& g, double r) {
    const int n = int(g.size());
    const double y = std::clamp(r / Tables::kRoughMax * (n - 1), 0.0, n - 1.0);
    const int y0 = std::min(int(y), n - 2);
    const double f = y - y0;
    return g[y0] * (1 - f) + g[y0 + 1] * f;
}
}  // namespace

Tables::Tables()
    : a_(std::size_t(kRough) * kTheta), b_(std::size_t(kRough) * kTheta), sh_(std::size_t(kSheenRough) * kSheenMu),
      aavg_(kRough), bavg_(kRough) {
    // Every row is independent and written by one thread, so the tables are identical for any
    // thread count (raw/core/parallel.hpp).
    const int threads = int(std::max(1u, std::thread::hardware_concurrency()));
    parallelRows(kRough, threads, [&](int i) {
        const double a = std::max(gridRough(i, kRough) * gridRough(i, kRough), 1e-4);
        for (int j = 0; j < kTheta; ++j) {
            const double mu = std::max(std::cos(gridTheta(j, kTheta)), 1e-4);
            albedoSplit(mu, a, kSamples, a_[std::size_t(i) * kTheta + j], b_[std::size_t(i) * kTheta + j]);
        }
    });
    // Cosine-weighted means, from the tables themselves, so 2 * integral(A(mu) mu dmu) is
    // the quadrature of exactly the function the model reads.
    std::vector<double> x, w;
    brdf::gaussLegendre01(64, x, w);
    for (int i = 0; i < kRough; ++i) {
        const double r = gridRough(i, kRough);
        double sa = 0.0, sb = 0.0;
        for (std::size_t k = 0; k < x.size(); ++k) { sa += w[k] * A(x[k], r) * x[k]; sb += w[k] * B(x[k], r) * x[k]; }
        aavg_[i] = 2.0 * sa; bavg_[i] = 2.0 * sb;
    }
    std::vector<double> gx, gw;
    brdf::gaussLegendre01(96, gx, gw);
    parallelRows(kSheenRough, threads, [&](int i) {
        for (int j = 0; j < kSheenMu; ++j)
            sh_[std::size_t(i) * kSheenMu + j] = sheenAlbedo(std::max(sheenMu(j), 1e-4), gridRough(i, kSheenRough), gx, gw);
    });
    buildAnisotropic();
    buildDominant();
}
double Tables::A(double mu, double r) const { return read2(a_, kTheta, kRough, mu, r); }
double Tables::Dom(double mu, double r) const { return read2(dom_, kDomMu, kDomRough, mu, r); }

// The lobe centroid's elevation: VNDF samples of the white lobe, each weighted by its
// estimator G2 / G1 (f cos / pdf), summed as vectors.
void Tables::buildDominant() {
    dom_.assign(std::size_t(kDomRough) * kDomMu, 0.0);
    parallelRows(kDomRough, int(std::max(1u, std::thread::hardware_concurrency())), [&](int i) {
        const double a = std::max(gridRough(i, kDomRough) * gridRough(i, kDomRough), 1e-4);
        for (int j = 0; j < kDomMu; ++j) {
            const double th = std::clamp(gridTheta(j, kDomMu), 0.0, kPi / 2 - 1e-4), mu = std::cos(th);
            double cx = 0.0, cz = 0.0;
            lobeCentroid(mu, a, 1 << 11, cx, cz);
            dom_[std::size_t(i) * kDomMu + j] = std::atan2(-cx, cz);
        }
    });
}
double Tables::B(double mu, double r) const { return read2(b_, kTheta, kRough, mu, r); }
double Tables::Aavg(double r) const { return read1(aavg_, r); }
double Tables::Bavg(double r) const { return read1(bavg_, r); }
double Tables::Sh(double mu, double r) const {
    const double x = std::sqrt(std::clamp(mu, 0.0, 1.0)) * (kSheenMu - 1);
    const double y = std::clamp(r / kRoughMax * (kSheenRough - 1), 0.0, kSheenRough - 1.0);
    const int x0 = std::min(int(x), kSheenMu - 2), y0 = std::min(int(y), kSheenRough - 2);
    const double fx = x - x0, fy = y - y0;
    const auto at = [&](int i, int j) { return sh_[std::size_t(i) * kSheenMu + j]; };
    return (at(y0, x0) * (1 - fx) + at(y0, x0 + 1) * fx) * (1 - fy) + (at(y0 + 1, x0) * (1 - fx) + at(y0 + 1, x0 + 1) * fx) * fy;
}

}  // namespace raw::pbr
