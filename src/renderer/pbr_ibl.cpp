// The material's response to image lighting (split sum): see raw/renderer/pbr.hpp.
// Each weight is the directional albedo of a lobe at the view, from the same tables and
// layer weights as eval, so a uniform environment returns exactly what the furnace does.
#include "raw/renderer/pbr.hpp"
#include "pbr_internal.hpp"
#include <algorithm>
#include <cmath>
namespace raw::pbr {
using namespace detail;
namespace {
// Specular weight of one Fresnel family: f0 A + f90 B, with iridescence mixed in at the view.
Rgb specWeight(const Material& m, Rgb f0, double f90, double a, double b, double muO) {
    const Rgb w = each([&](int k) { return ch(f0, k) * a + f90 * b; });
    if (m.iridescence <= 0.0) return w;
    const Rgb fi = iridescentFresnel(1.0, m.iridescenceIor, m.iridescenceThickness, f0, muO);
    return w * (1.0 - m.iridescence) + fi * ((a + b) * m.iridescence);
}
}  // namespace

IblResponse iblResponse(const Material& m, const Tables& t, D3 wo) {
    IblResponse r;
    const Setup s = setup(m, t);
    const D3 o{s.cosRot * wo.x + s.sinRot * wo.y, -s.sinRot * wo.x + s.cosRot * wo.y, wo.z};
    double a, b;
    split(s, t, o, a, b);
    const double e = a + b, mt = std::clamp(m.metallic, 0.0, 1.0), tr = std::clamp(m.transmission, 0.0, 1.0);
    const Rgb esO = esAt(s, t, o);
    r.roughness = s.r;
    r.specular = specWeight(m, m.baseColor, 1.0, a, b, o.z) * mt + specWeight(m, s.f0d, s.f90, a, b, o.z) * (1.0 - mt);
    Rgb under = each([&](int k) { return 1.0 - ch(esO, k); });
    if (m.iridescence > 0.0) {
        const double mo = iridescentFresnel(1.0, m.iridescenceIor, m.iridescenceThickness, s.f0d, o.z).max3();
        under = under * (1.0 - m.iridescence) + rgb((1.0 - mo) * m.iridescence);
    }
    r.irradiance = (s.kmsMetal * mt + s.kmsDiel * (1.0 - mt)) * (1.0 - e) + m.baseColor * under * ((1.0 - mt) * (1.0 - tr));
    if (tr > 0.0 && mt < 1.0) {
        const Rgb att = each([&](int k) {
            if (!m.volume || m.attenuationDistance <= 0.0 || m.thickness <= 0.0) return 1.0;
            return std::exp(std::log(std::max(ch(m.attenuationColor, k), 1e-300)) / m.attenuationDistance * m.thickness);
        });
        r.transmission = m.baseColor * att * under * (tr * (1.0 - mt));
    }
    const double sh = m.sheenColor.max3();
    if (sh > 0.0) {
        const double e_sh = t.Sh(wo.z, m.sheenRoughness), keep = 1.0 - sh * e_sh;
        r.specular = r.specular * keep; r.irradiance = r.irradiance * keep; r.transmission = r.transmission * keep;
        r.irradiance = r.irradiance + m.sheenColor * e_sh;
    }
    const double c = std::clamp(m.clearcoat, 0.0, 1.0);
    if (c > 0.0) {
        const double rc = std::clamp(m.clearcoatRoughness, kMinRoughness, 1.0), mu = std::max(dot(norm(m.coatNormal), wo), 0.0);
        const double kc = kms(0.04 + 0.96 / 21.0, t.Eavg(rc)), ac = t.A(mu, rc), bc = t.B(mu, rc);
        const double ec = 0.04 * ac + bc + (1.0 - ac - bc) * kc, keep = (1.0 - c * ec) * (1.0 - c * ec);
        r.specular = r.specular * keep; r.irradiance = r.irradiance * keep; r.transmission = r.transmission * keep;
        r.coat = rgb(c * (0.04 * ac + bc));
        r.coatRoughness = rc;
        r.irradiance = r.irradiance + rgb(c * kc * (1.0 - ac - bc));
    }
    return r;
}

}  // namespace raw::pbr
