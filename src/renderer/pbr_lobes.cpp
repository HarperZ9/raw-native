// Lobes of the glTF material model: see raw/renderer/pbr.hpp.
#include "raw/renderer/pbr.hpp"
#include <algorithm>
#include <cmath>
namespace raw::pbr {
namespace {
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
}  // namespace

double alphaOf(double roughness) {
    const double r = std::clamp(roughness, kMinRoughness, 1.0);
    return r * r;
}
// KHR_materials_anisotropy: alpha_t = mix(alpha, 1, anisotropy^2), alpha_b = alpha, along
// the anisotropy direction (the caller rotates the frame by anisotropyRotation).
void anisoAlphas(const Material& m, double& ax, double& ay) {
    const double a = alphaOf(m.roughness), k = std::clamp(m.anisotropy, 0.0, 1.0);
    ax = a + (1.0 - a) * k * k;
    ay = a;
}
// 1 / (pi ax ay ((hx/ax)^2 + (hy/ay)^2 + hz^2)^2). The isotropic case is
// a^2 / (pi (hx^2 + hy^2 + a^2 hz^2)^2): no 1 - hz^2, so float32 keeps its precision at the peak.
double ggxD(D3 h, double ax, double ay) {
    if (h.z <= 0.0) return 0.0;
    const double u = h.x / ax, v = h.y / ay, s = u * u + v * v + h.z * h.z;
    return 1.0 / (kPi * ax * ay * s * s);
}
// Height-correlated Smith (Heitz 2014), anisotropic. With Q(w) = sqrt(ax^2 wx^2 + ay^2 wy^2 + wz^2):
// G2 = 2 |mu_o| |mu_i| / (|mu_i| Q(o) + |mu_o| Q(i)), V = G2 / (4 |mu_o| |mu_i|).
double smithV(D3 wo, D3 wi, double ax, double ay) {
    const double qo = std::sqrt(ax * ax * wo.x * wo.x + ay * ay * wo.y * wo.y + wo.z * wo.z);
    const double qi = std::sqrt(ax * ax * wi.x * wi.x + ay * ay * wi.y * wi.y + wi.z * wi.z);
    return 0.5 / (std::fabs(wi.z) * qo + std::fabs(wo.z) * qi);
}
double smithG2(D3 wo, D3 wi, double ax, double ay) {
    return 4.0 * std::fabs(wo.z) * std::fabs(wi.z) * smithV(wo, wi, ax, ay);
}
double schlick(double f0, double f90, double cosine) {
    const double m = 1.0 - std::clamp(cosine, 0.0, 1.0), m5 = m * m * m * m * m;
    return f0 + (f90 - f0) * m5;
}
// Charlie (Estevez and Kulla 2017): (2 + 1/a) sin(theta_h)^(1/a) / (2 pi), a = roughness^2.
double charlieD(D3 h, double sheenRoughness) {
    const double r = std::clamp(sheenRoughness, kMinSheenRoughness, 1.0), inv = 1.0 / (r * r);
    const double s2 = h.x * h.x + h.y * h.y;
    return (2.0 + inv) * std::pow(s2, 0.5 * inv) / (2.0 * kPi);
}
// The specification's fit: L(x) = a / (1 + b x^c) + d x + e, coefficients mixed by (1 - alpha)^2.
// (The Neubelt visibility, tried first, is not bounded: its albedo passes 1 at grazing
// angles, and the albedo scaling then went negative; found by the GPU parity dump, 2026-10-10.)
namespace {
double sheenL(double x, double a) {
    const double t = (1.0 - a) * (1.0 - a);
    const double A = 21.5473 + (25.3245 - 21.5473) * t, B = 3.82987 + (3.32435 - 3.82987) * t;
    const double C = 0.19823 + (0.16801 - 0.19823) * t, D = -1.97760 + (-1.27393 + 1.97760) * t;
    const double E = -4.32054 + (-4.85967 + 4.32054) * t;
    return A / (1.0 + B * std::pow(x, C)) + D * x + E;
}
double sheenLambda(double c, double a) {
    return std::fabs(c) < 0.5 ? std::exp(sheenL(c, a)) : std::exp(2.0 * sheenL(0.5, a) - sheenL(1.0 - c, a));
}
}  // namespace
double charlieV(double muO, double muI, double sheenRoughness) {
    const double r = std::clamp(sheenRoughness, kMinSheenRoughness, 1.0), a = r * r;
    return 1.0 / ((1.0 + sheenLambda(muO, a) + sheenLambda(muI, a)) * (4.0 * muO * muI));
}

double walterBtdf(D3 in, double etaIn, D3 out, double etaOut, double ax, double ay, double f0) {
    if (in.z * out.z >= 0.0) return 0.0;
    D3 h{-(etaIn * in.x + etaOut * out.x), -(etaIn * in.y + etaOut * out.y), -(etaIn * in.z + etaOut * out.z)};
    const double l = std::sqrt(dot(h, h));
    if (l <= 0.0) return 0.0;
    h = {h.x / l, h.y / l, h.z / l};
    if (h.z < 0.0) h = {-h.x, -h.y, -h.z};
    const double ih = dot(in, h), oh = dot(out, h);
    if (ih * in.z <= 0.0 || oh * out.z <= 0.0) return 0.0;        // each direction on its own side of the facet
    const double cosLow = etaIn <= etaOut ? std::fabs(ih) : std::fabs(oh);
    const double fr = schlick(f0, 1.0, cosLow);
    const double den = etaIn * ih + etaOut * oh;
    return std::fabs(ih) * std::fabs(oh) / (std::fabs(in.z) * std::fabs(out.z)) * etaOut * etaOut * (1.0 - fr) *
           smithG2(in, out, ax, ay) * ggxD(h, ax, ay) / (den * den);
}

}  // namespace raw::pbr
