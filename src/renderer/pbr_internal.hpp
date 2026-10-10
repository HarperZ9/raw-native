#pragma once
// Private to the material model (src/renderer/pbr_*.cpp): the per-material setup and the
// energy helpers that direct lighting (pbr_material.cpp) and image lighting (pbr_ibl.cpp)
// share, so both read one energy model.
#include "raw/renderer/pbr.hpp"
#include <algorithm>
#include <cmath>
namespace raw::pbr::detail {
using std::sqrt;
inline double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline D3 norm(D3 a) { const double l = sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
inline double ch(const Rgb& c, int k) { return k == 0 ? c.r : k == 1 ? c.g : c.b; }
inline Rgb rgb(double v) { return {v, v, v}; }
inline Rgb operator+(Rgb a, Rgb b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline Rgb operator*(Rgb a, Rgb b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
inline Rgb operator*(Rgb a, double s) { return {a.r * s, a.g * s, a.b * s}; }
template<class F> inline Rgb each(F f) { return {f(0), f(1), f(2)}; }

// Everything about one material that does not depend on the two directions.
struct Setup {
    double ax, ay, r, k, ebar, abar, bbar, f90, cosRot, sinRot;
    bool an;
    Rgb f0d, kmsMetal, kmsDiel, esAvg;
};
inline double kms(double favg, double ebar) { return favg * favg * ebar / (1.0 - favg * (1.0 - ebar)); }
inline Setup setup(const Material& m, const Tables& t) {
    Setup s{};
    anisoAlphas(m, s.ax, s.ay);
    s.r = std::clamp(m.roughness, kMinRoughness, 1.0);
    s.k = std::clamp(m.anisotropy, 0.0, 1.0);
    s.an = s.k > 0.0;
    s.abar = s.an ? t.Aavg2(s.r, s.k) : t.Aavg(s.r);
    s.bbar = s.an ? t.Bavg2(s.r, s.k) : t.Bavg(s.r);
    s.ebar = s.abar + s.bbar;
    const double q = (m.ior - 1.0) / (m.ior + 1.0);
    s.f90 = m.specular;
    s.f0d = each([&](int k) { return std::min(q * q * ch(m.specularColor, k), 1.0) * m.specular; });
    s.kmsMetal = each([&](int k) { const double f0 = ch(m.baseColor, k); return kms(f0 + (1.0 - f0) / 21.0, s.ebar); });
    s.kmsDiel = each([&](int k) { const double f0 = ch(s.f0d, k); return kms(f0 + (s.f90 - f0) / 21.0, s.ebar); });
    s.esAvg = each([&](int k) { return ch(s.f0d, k) * s.abar + s.f90 * s.bbar + (1.0 - s.ebar) * ch(s.kmsDiel, k); });
    s.cosRot = std::cos(m.anisotropyRotation); s.sinRot = std::sin(m.anisotropyRotation);
    return s;
}
// Split albedo (A, B) of the lobe for a direction in the anisotropy frame.
inline void split(const Setup& s, const Tables& t, D3 w, double& a, double& b) {
    const double mu = std::fabs(w.z);
    if (s.an) {
        const double ph = std::atan2(std::fabs(w.y), std::fabs(w.x));
        a = t.A4(mu, ph, s.r, s.k); b = t.B4(mu, ph, s.r, s.k);
    } else {
        a = t.A(mu, s.r); b = t.B(mu, s.r);
    }
}
// Directional albedo of the dielectric specular layer, per channel.
inline Rgb esAt(const Setup& s, const Tables& t, D3 w) {
    double a, b;
    split(s, t, w, a, b);
    const double e = a + b;
    return each([&](int k) { return ch(s.f0d, k) * a + s.f90 * b + (1.0 - e) * ch(s.kmsDiel, k); });
}
// The white multiple-scattering lobe: unit albedo with the single-scattering lobe.
inline double msWhite(const Setup& s, const Tables& t, D3 o, D3 i) {
    double ao, bo, ai, bi;
    split(s, t, o, ao, bo); split(s, t, i, ai, bi);
    return (1.0 - ao - bo) * (1.0 - ai - bi) / (kPi * (1.0 - s.ebar));
}
inline Rgb fresnel(const Material& m, Rgb f0, double f90, double voh) {
    const Rgb f = each([&](int k) { return schlick(ch(f0, k), f90, voh); });
    if (m.iridescence <= 0.0) return f;
    const Rgb fi = iridescentFresnel(1.0, m.iridescenceIor, m.iridescenceThickness, f0, voh);
    return f * (1.0 - m.iridescence) + fi * m.iridescence;
}


}  // namespace raw::pbr::detail
