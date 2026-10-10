// The glTF material model, composed from its lobes: see raw/renderer/pbr.hpp.
#include "raw/renderer/pbr.hpp"
#include "pbr_internal.hpp"
#include <algorithm>
#include <cmath>
namespace raw::pbr {
namespace {
using namespace detail;

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
            const double mo = irid(m, s.f0d, o.z).max3();
            const double mi = irid(m, s.f0d, i.z).max3();
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

using namespace detail;
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
    const double frV = schlick(0.04, 1.0, co);                     // spec-exact: Fresnel at the view alone
    const double aCoat = m.specExact ? 1.0 - c * frV : (1.0 - c * ec(co)) * (1.0 - c * ec(ci));
    out = {out.specular * aCoat, {}, out.smooth * aCoat, out.transmission * aCoat};
    if (mo > 0.0 && mi > 0.0 && wi.z > 0.0) {
        const D3 h = norm({wo.x + wi.x, wo.y + wi.y, wo.z + wi.z});
        const double hn = dot(h, nc), cx = h.y * nc.z - h.z * nc.y, cy = h.z * nc.x - h.x * nc.z, cz = h.x * nc.y - h.y * nc.x;
        const double s2 = cx * cx + cy * cy + cz * cz, den = s2 + a * a * hn * hn;
        const double d = hn > 0.0 ? a * a / (kPi * den * den) : 0.0;
        const double v = 0.5 / (mi * sqrt(mo * mo * (1.0 - a * a) + a * a) + mo * sqrt(mi * mi * (1.0 - a * a) + a * a));
        if (m.specExact) { out.coat = rgb(c * frV * d * v); return out; }
        out.coat = rgb(c * d * v * schlick(0.04, 1.0, dot(wo, h)));
        const double ms = (1.0 - t.E(mo, rc)) * (1.0 - t.E(mi, rc)) / (kPi * (1.0 - t.Eavg(rc)));
        out.smooth = out.smooth + rgb(c * kc * ms);
    }
    return out;
}
Rgb eval(const Material& m, const Tables& t, D3 wo, D3 wi) { return evalTerms(m, t, wo, wi).total(); }

}  // namespace raw::pbr
