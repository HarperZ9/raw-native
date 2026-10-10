#pragma once
// Directional albedo of a material by deterministic quadrature, lobe class by lobe class
// (raw/renderer/pbr.hpp, Terms): each peaked class is integrated over its half vector
// with tan(theta_h) / alpha as the radial variable, so the nodes follow the lobe at every
// roughness; the smooth class is integrated over the incident direction. This is a
// different route from the tables the model reads (VNDF sampling), so a furnace that
// passes is not the tables agreeing with themselves.
#include "raw/renderer/pbr.hpp"
#include "raw/renderer/brdf.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
namespace pbr_test {
using namespace raw::pbr;

struct Rule { std::vector<double> x, w; };
inline const Rule& rule(int n) {
    // Node-stable and filled before any thread runs: warm() builds every rule the
    // integrators use, so concurrent calls only read.
    static std::map<int, Rule> cache;
    auto it = cache.find(n);
    if (it != cache.end()) return it->second;
    Rule& r = cache[n];
    raw::brdf::gaussLegendre01(n, r.x, r.w);
    return r;
}
inline void warm() { rule(384); rule(192); rule(96); }
inline Rgb add(Rgb a, Rgb b, double s) { return {a.r + b.r * s, a.g + b.g * s, a.b + b.b * s}; }
inline D3 nrm(D3 a) { const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return {a.x / l, a.y / l, a.z / l}; }

enum class Map { Reflect, Mirror, Refract };
// One peaked class over the half vector. alpha sets the radial variable; ior is used by Refract.
template<class Pick>
Rgb peaked(const Material& m, const Tables& t, D3 o, double alpha, Map map, double ior, Pick pick, int nr = 384, int np = 192) {
    const Rule& R = rule(nr); const Rule& P = rule(np);
    Rgb sum{};
    for (std::size_t a = 0; a < R.x.size(); ++a) {
        const double s = R.x[a], tt = s / (1.0 - s), dt = 1.0 / ((1.0 - s) * (1.0 - s));
        const double th = std::atan(alpha * tt), dth = alpha / (1.0 + alpha * alpha * tt * tt) * dt;
        const double ch = std::cos(th), sh = std::sin(th);
        for (std::size_t b = 0; b < P.x.size(); ++b) {
            const double ph = 2.0 * kPi * P.x[b];
            const D3 h{sh * std::cos(ph), sh * std::sin(ph), ch};
            const double oh = o.x * h.x + o.y * h.y + o.z * h.z;
            if (oh <= 0.0) continue;
            D3 i; double jac;
            if (map == Map::Refract) {
                const double eta = 1.0 / ior, k = 1.0 - eta * eta * (1.0 - oh * oh);
                if (k < 0.0) continue;
                const double c = eta * oh - std::sqrt(k);
                i = nrm({-eta * o.x + c * h.x, -eta * o.y + c * h.y, -eta * o.z + c * h.z});
                const double ih = i.x * h.x + i.y * h.y + i.z * h.z, den = oh + ior * ih;
                jac = den * den / (ior * ior * std::fabs(ih));      // d omega_i / d omega_h (Walter et al. 2007, eq. 17 inverted)
            } else {
                i = {2.0 * oh * h.x - o.x, 2.0 * oh * h.y - o.y, 2.0 * oh * h.z - o.z};
                jac = 4.0 * oh;
                if (map == Map::Mirror) i.z = -i.z;
            }
            if ((map == Map::Reflect) != (i.z > 0.0)) continue;
            const Rgb f = pick(evalTerms(m, t, o, i));
            sum = add(sum, f, R.w[a] * P.w[b] * 2.0 * kPi * std::fabs(i.z) * jac * sh * dth);
        }
    }
    return sum;
}
// The smooth class over the upper hemisphere.
inline Rgb smooth(const Material& m, const Tables& t, D3 o, int n = 96) {
    const Rule& R = rule(n);
    Rgb sum{};
    for (std::size_t a = 0; a < R.x.size(); ++a) {
        const double th = 0.5 * kPi * R.x[a], ci = std::cos(th), si = std::sin(th);
        for (std::size_t b = 0; b < R.x.size(); ++b) {
            const double ph = 2.0 * kPi * R.x[b];
            const Rgb f = evalTerms(m, t, o, {si * std::cos(ph), si * std::sin(ph), ci}).smooth;
            sum = add(sum, f, R.w[a] * R.w[b] * 0.5 * kPi * 2.0 * kPi * ci * si);
        }
    }
    return sum;
}
// The full albedo: every class with its rule. alpha of the base lobe is the smaller of
// the two anisotropic alphas.
inline Rgb albedo(const Material& m, const Tables& t, D3 o) {
    double ax, ay; anisoAlphas(m, ax, ay);
    const double a = std::min(ax, ay), rc = std::clamp(m.clearcoatRoughness, kMinRoughness, 1.0);
    Rgb s = smooth(m, t, o);
    s = add(s, peaked(m, t, o, a, Map::Reflect, 1.0, [](const Terms& x) { return x.specular; }), 1.0);
    if (m.clearcoat > 0.0) s = add(s, peaked(m, t, o, rc * rc, Map::Reflect, 1.0, [](const Terms& x) { return x.coat; }), 1.0);
    // With volume, f(o, i) carries light from inside (index ior) to the viewer outside; light
    // arriving from o and leaving along i carries ior^2 times that (generalized reciprocity,
    // checked in test_pbr_reciprocity), and that is the energy the furnace must bound.
    if (m.transmission > 0.0 && m.metallic < 1.0)
        s = add(s, peaked(m, t, o, a, m.volume ? Map::Refract : Map::Mirror, m.ior, [](const Terms& x) { return x.transmission; }),
                m.volume ? m.ior * m.ior : 1.0);
    return s;
}
inline D3 view(double mu, double azimuth = 0.0) {
    const double s = std::sqrt(std::max(0.0, 1.0 - mu * mu));
    return {s * std::cos(azimuth), s * std::sin(azimuth), mu};
}
}  // namespace pbr_test
