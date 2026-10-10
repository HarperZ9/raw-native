// The glTF material model, composed from its lobes: see raw/renderer/pbr.hpp.
#include "raw/renderer/pbr.hpp"
#include <algorithm>
#include <cmath>
namespace raw::pbr {
namespace {
using std::sqrt;
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 norm(D3 a) { const double l = sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
double ch(const Rgb& c, int k) { return k == 0 ? c.r : k == 1 ? c.g : c.b; }
Rgb rgb(double v) { return {v, v, v}; }
Rgb operator+(Rgb a, Rgb b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Rgb operator*(Rgb a, Rgb b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
Rgb operator*(Rgb a, double s) { return {a.r * s, a.g * s, a.b * s}; }
template<class F> Rgb each(F f) { return {f(0), f(1), f(2)}; }

// Everything about one material that does not depend on the two directions.
struct Setup {
    double ax, ay, r, k, ebar, abar, bbar, f90, cosRot, sinRot;
    bool an;
    Rgb f0d, kmsMetal, kmsDiel, esAvg;
};
double kms(double favg, double ebar) { return favg * favg * ebar / (1.0 - favg * (1.0 - ebar)); }
Setup setup(const Material& m, const Tables& t) {
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
void split(const Setup& s, const Tables& t, D3 w, double& a, double& b) {
    const double mu = std::fabs(w.z);
    if (s.an) {
        const double ph = std::atan2(std::fabs(w.y), std::fabs(w.x));
        a = t.A4(mu, ph, s.r, s.k); b = t.B4(mu, ph, s.r, s.k);
    } else {
        a = t.A(mu, s.r); b = t.B(mu, s.r);
    }
}
// Directional albedo of the dielectric specular layer, per channel.
Rgb esAt(const Setup& s, const Tables& t, D3 w) {
    double a, b;
    split(s, t, w, a, b);
    const double e = a + b;
    return each([&](int k) { return ch(s.f0d, k) * a + s.f90 * b + (1.0 - e) * ch(s.kmsDiel, k); });
}
// The white multiple-scattering lobe: unit albedo with the single-scattering lobe.
double msWhite(const Setup& s, const Tables& t, D3 o, D3 i) {
    double ao, bo, ai, bi;
    split(s, t, o, ao, bo); split(s, t, i, ai, bi);
    return (1.0 - ao - bo) * (1.0 - ai - bi) / (kPi * (1.0 - s.ebar));
}
Rgb fresnel(const Material& m, Rgb f0, double f90, double voh) {
    const Rgb f = each([&](int k) { return schlick(ch(f0, k), f90, voh); });
    if (m.iridescence <= 0.0) return f;
    const Rgb fi = iridescentFresnel(1.0, m.iridescenceIor, m.iridescenceThickness, f0, voh);
    return f * (1.0 - m.iridescence) + fi * m.iridescence;
}

// The base: metal and dielectric mixed by metallic, below sheen and coat. o and i are in
// the anisotropy frame.
Terms base(const Material& m, const Tables& t, const Setup& s, D3 o, D3 i) {
    Terms out;
    const double mt = std::clamp(m.metallic, 0.0, 1.0);
    const Rgb esO = esAt(s, t, o), esI = esAt(s, t, i);
    const double tr = std::clamp(m.transmission, 0.0, 1.0);
    if (i.z > 0.0) {
        const D3 h = norm({o.x + i.x, o.y + i.y, o.z + i.z});
        const double dv = ggxD(h, s.ax, s.ay) * smithV(o, i, s.ax, s.ay), voh = dot(o, h), ms = msWhite(s, t, o, i);
        const Rgb fm = fresnel(m, m.baseColor, 1.0, voh), fd = fresnel(m, s.f0d, s.f90, voh);
        out.specular = (fm * mt + fd * (1.0 - mt)) * dv;
        out.smooth = (s.kmsMetal * mt + s.kmsDiel * (1.0 - mt)) * ms;
        Rgb w = each([&](int k) { return (1.0 - ch(esO, k)) * (1.0 - ch(esI, k)) / (1.0 - ch(s.esAvg, k)); });
        if (m.iridescence > 0.0) {
            const double mo = iridescentFresnel(1.0, m.iridescenceIor, m.iridescenceThickness, s.f0d, o.z).max3();
            const double mi = iridescentFresnel(1.0, m.iridescenceIor, m.iridescenceThickness, s.f0d, i.z).max3();
            w = w * (1.0 - m.iridescence) + rgb((1.0 - mo) * (1.0 - mi) * m.iridescence);
        }
        out.smooth = out.smooth + m.baseColor * w * ((1.0 - mt) * (1.0 - tr) / kPi);
        return out;
    }
    if (tr <= 0.0 || mt >= 1.0) return out;
    if (!m.volume) {                                   // thin-walled: the lobe mirrored through the surface
        const D3 im{i.x, i.y, -i.z}, h = norm({o.x + im.x, o.y + im.y, o.z + im.z});
        const double g = ggxD(h, s.ax, s.ay) * smithV(o, im, s.ax, s.ay) + msWhite(s, t, o, im);
        out.transmission = m.baseColor * each([&](int k) { return std::min(1.0 - ch(esO, k), 1.0 - ch(esI, k)); }) * (g * tr * (1.0 - mt));
        return out;
    }
    const Rgb att = each([&](int k) {
        if (m.attenuationDistance <= 0.0 || m.thickness <= 0.0) return 1.0;
        return std::exp(std::log(std::max(ch(m.attenuationColor, k), 1e-300)) / m.attenuationDistance * m.thickness);
    });
    out.transmission = m.baseColor * att * each([&](int k) {
        return walterBtdf(i, m.ior, o, 1.0, s.ax, s.ay, ch(s.f0d, k)) * tr * (1.0 - mt);
    });
    return out;
}
}  // namespace

Rgb Terms::total() const { return specular + coat + smooth + transmission; }

Rgb emission(const Material& m) { return m.emissive * m.emissiveStrength; }

// Coat over sheen over base: f = coat + A_coat (sheen + A_sheen base).
Terms evalTerms(const Material& m, const Tables& t, D3 wo, D3 wi) {
    Terms out;
    if (wo.z <= 0.0 || wi.z == 0.0) return out;
    const Setup s = setup(m, t);
    const auto rot = [&](D3 w) { return D3{s.cosRot * w.x + s.sinRot * w.y, -s.sinRot * w.x + s.cosRot * w.y, w.z}; };
    out = base(m, t, s, rot(wo), rot(wi));
    double aSheen = 1.0;
    const double sh = m.sheenColor.max3();
    if (sh > 0.0) {
        aSheen = std::min(1.0 - sh * t.Sh(wo.z, m.sheenRoughness), 1.0 - sh * t.Sh(std::fabs(wi.z), m.sheenRoughness));
        Rgb lobe{};
        if (wi.z > 0.0) lobe = m.sheenColor * (charlieD(norm({wo.x + wi.x, wo.y + wi.y, wo.z + wi.z}), m.sheenRoughness) * charlieV(wo.z, wi.z, m.sheenRoughness));
        out = {out.specular * aSheen, out.coat, out.smooth * aSheen + lobe, out.transmission * aSheen};
    }
    const double c = std::clamp(m.clearcoat, 0.0, 1.0);
    if (c <= 0.0) return out;
    const D3 nc = norm(m.coatNormal);
    // Cosines against the coat normal; a direction behind it (a tilted coat normal) sees the
    // coat at grazing. The same rule for both directions keeps the product reciprocal.
    const double mo = dot(nc, wo), mi = dot(nc, wi), rc = std::clamp(m.clearcoatRoughness, kMinRoughness, 1.0), a = rc * rc;
    const double co = std::max(mo, 0.0), ci = wi.z > 0.0 ? std::max(mi, 0.0) : std::fabs(mi);
    const double kc = kms(0.04 + 0.96 / 21.0, t.Eavg(rc));
    const auto ec = [&](double mu) { const double e = t.E(mu, rc); return 0.04 * t.A(mu, rc) + t.B(mu, rc) + (1.0 - e) * kc; };
    const double aCoat = (1.0 - c * ec(co)) * (1.0 - c * ec(ci));
    out = {out.specular * aCoat, {}, out.smooth * aCoat, out.transmission * aCoat};
    if (mo > 0.0 && mi > 0.0 && wi.z > 0.0) {
        const D3 h = norm({wo.x + wi.x, wo.y + wi.y, wo.z + wi.z});
        const double hn = dot(h, nc), cx = h.y * nc.z - h.z * nc.y, cy = h.z * nc.x - h.x * nc.z, cz = h.x * nc.y - h.y * nc.x;
        const double s2 = cx * cx + cy * cy + cz * cz, den = s2 + a * a * hn * hn;
        const double d = hn > 0.0 ? a * a / (kPi * den * den) : 0.0;
        const double v = 0.5 / (mi * sqrt(mo * mo * (1.0 - a * a) + a * a) + mo * sqrt(mi * mi * (1.0 - a * a) + a * a));
        out.coat = rgb(c * d * v * schlick(0.04, 1.0, dot(wo, h)));
        const double ms = (1.0 - t.E(mo, rc)) * (1.0 - t.E(mi, rc)) / (kPi * (1.0 - t.Eavg(rc)));
        out.smooth = out.smooth + rgb(c * kc * ms);
    }
    return out;
}
Rgb eval(const Material& m, const Tables& t, D3 wo, D3 wi) { return evalTerms(m, t, wo, wi).total(); }

}  // namespace raw::pbr
