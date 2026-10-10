// The base model's specular lobe: see raw/renderer/brdf.hpp.
#include "raw/renderer/brdf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace raw::brdf {
namespace {
struct V3 { double x, y, z; };
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalize(V3 a) { const double l = std::sqrt(dot(a, a)); return a * (1.0 / l); }
double radicalInverse(std::uint32_t b) {
    b = (b << 16) | (b >> 16);
    b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
    b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
    b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
    b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
    return b * 2.3283064365386963e-10;
}
double lambda(double mu, double alpha) {
    const double m2 = mu * mu, t2 = (1.0 - m2) / m2;
    return 0.5 * (-1.0 + std::sqrt(1.0 + alpha * alpha * t2));
}
}  // namespace

double alphaOf(double r) { return r * r; }
double ggxD(double muH, double a) {
    const double a2 = a * a, d = muH * muH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}
double smithG1(double mu, double a) { return 1.0 / (1.0 + lambda(mu, a)); }
double smithG2(double muO, double muI, double a) { return 1.0 / (1.0 + lambda(muO, a) + lambda(muI, a)); }
double smithV(double muO, double muI, double a) {
    const double a2 = a * a;
    const double go = muI * std::sqrt(muO * muO * (1.0 - a2) + a2), gi = muO * std::sqrt(muI * muI * (1.0 - a2) + a2);
    return 0.5 / (go + gi);
}
double schlickF(double f0, double voh) { const double m = 1.0 - voh; return f0 + (1.0 - f0) * m * m * m * m * m; }

double albedoVndf(double mu, double a, int n) {
    const V3 v{std::sqrt(std::max(0.0, 1.0 - mu * mu)), 0.0, mu};
    const V3 vh = normalize({a * v.x, a * v.y, v.z});
    const double lensq = vh.x * vh.x + vh.y * vh.y;
    const V3 t1 = lensq > 0 ? V3{-vh.y, vh.x, 0.0} * (1.0 / std::sqrt(lensq)) : V3{1.0, 0.0, 0.0};
    const V3 t2 = cross(vh, t1);
    const double g1 = smithG1(mu, a);
    double sum = 0.0;
    for (int k = 0; k < n; ++k) {
        const double u1 = (k + 0.5) / n, u2 = radicalInverse(std::uint32_t(k));
        const double r = std::sqrt(u1), phi = 2.0 * kPi * u2, s = 0.5 * (1.0 + vh.z);
        const double p1 = r * std::cos(phi), p2 = (1.0 - s) * std::sqrt(1.0 - p1 * p1) + s * r * std::sin(phi);
        const V3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2));
        const V3 m = normalize({a * nh.x, a * nh.y, std::max(1e-12, nh.z)});
        const double vom = dot(v, m), li = 2.0 * vom * m.z - v.z;     // the z of the reflected direction
        if (li <= 0.0) continue;
        sum += smithG2(mu, li, a) / g1;
    }
    return sum / n;
}

EnergyTable::EnergyTable() : e_(std::size_t(kRough) * kTheta) {
    for (int i = 0; i < kRough; ++i) {
        const double r = kRoughMax * i / (kRough - 1), a = std::max(alphaOf(r), 1e-4);
        for (int j = 0; j < kTheta; ++j) {
            const double th = -kThetaPad + (kPi / 2 + kThetaPad) * j / (kTheta - 1);
            const double mu = std::max(std::cos(th), 1e-4);
            e_[std::size_t(i) * kTheta + j] = i == 0 ? 1.0 : albedoVndf(mu, a, kSamples);
        }
    }
}
double EnergyTable::E(double mu, double r) const {
    const double th = std::acos(std::clamp(mu, 0.0, 1.0));
    const double x = std::clamp((th + kThetaPad) / (kPi / 2 + kThetaPad) * (kTheta - 1), 0.0, kTheta - 1.0);
    const double y = std::clamp(r / kRoughMax * (kRough - 1), 0.0, kRough - 1.0);
    const int x0 = std::min(int(x), kTheta - 2), y0 = std::min(int(y), kRough - 2);
    const double fx = x - x0, fy = y - y0;
    const auto at = [&](int i, int j) { return e_[std::size_t(i) * kTheta + j]; };
    return (at(y0, x0) * (1 - fx) + at(y0, x0 + 1) * fx) * (1 - fy) + (at(y0 + 1, x0) * (1 - fx) + at(y0 + 1, x0 + 1) * fx) * fy;
}
double EnergyTable::Eavg(double r) const {
    static std::vector<double> x, w;
    if (x.empty()) gaussLegendre01(64, x, w);
    double s = 0.0;
    for (std::size_t k = 0; k < x.size(); ++k) s += w[k] * E(x[k], r) * x[k];
    return 2.0 * s;
}

double multiScatter(const EnergyTable& t, double muO, double muI, double r) {
    const double ea = t.Eavg(r);
    return (1.0 - t.E(muO, r)) * (1.0 - t.E(muI, r)) / (kPi * (1.0 - ea));
}

void gaussLegendre01(int n, std::vector<double>& x, std::vector<double>& w) {
    x.assign(n, 0.0); w.assign(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double z = std::cos(kPi * (i + 0.75) / (n + 0.5)), pp = 0.0;
        for (int it = 0; it < 100; ++it) {
            double p1 = 1.0, p2 = 0.0;
            for (int j = 1; j <= n; ++j) { const double p3 = p2; p2 = p1; p1 = ((2.0 * j - 1.0) * z * p2 - (j - 1.0) * p3) / j; }
            pp = n * (z * p1 - p2) / (z * z - 1.0);
            const double z1 = z;
            z = z1 - p1 / pp;
            if (std::fabs(z - z1) < 1e-15) break;
        }
        x[i] = 0.5 * (1.0 - z); w[i] = 1.0 / ((1.0 - z * z) * pp * pp);
    }
}

}  // namespace raw::brdf
