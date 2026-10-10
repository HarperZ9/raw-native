// The spec-exact forms (raw::pbr::Material::specExact): the clearcoat weighted by Fresnel at
// the view alone and iridescence summed to the second harmonic, as KHR_materials_clearcoat
// and KHR_materials_iridescence write them, checked against the formulas written out here.
#include "raw/renderer/pbr.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
using namespace raw::pbr;

namespace {
struct Rng { std::uint64_t s; double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s >> 11) * (1.0 / 9007199254740992.0); } };
D3 upper(Rng& r) { const double mu = 0.05 + 0.95 * r.next(), ph = 2 * kPi * r.next(), s = std::sqrt(1 - mu * mu); return {s * std::cos(ph), s * std::sin(ph), mu}; }
double rel(double a, double b) { return std::fabs(a - b) / std::max(1e-12, std::fabs(b)); }
}  // namespace

int main() {
    const Tables t;
    Rng r{42};
    double worstCoat = 0.0, worstIrid = 0.0, plainDiffers = 0.0;
    for (int k = 0; k < 2000; ++k) {
        Material base; base.baseColor = {r.next(), r.next(), r.next()}; base.metallic = r.next(); base.roughness = r.next(); base.specExact = true;
        Material m = base; m.clearcoat = r.next(); m.clearcoatRoughness = r.next();
        const D3 o = upper(r), i = upper(r);
        D3 h{o.x + i.x, o.y + i.y, o.z + i.z};
        const double l = std::sqrt(h.x * h.x + h.y * h.y + h.z * h.z);
        h = {h.x / l, h.y / l, h.z / l};
        const double a = alphaOf(m.clearcoatRoughness), fr = schlick(0.04, 1.0, o.z), c = m.clearcoat;
        const double layer = ggxD(h, a, a) * smithV(o, i, a, a);
        const Rgb b = eval(base, t, o, i), got = eval(m, t, o, i);
        worstCoat = std::max({worstCoat, rel(got.r, b.r * (1 - c * fr) + c * fr * layer), rel(got.g, b.g * (1 - c * fr) + c * fr * layer)});
        Material p = m; p.specExact = false;
        plainDiffers = std::max(plainDiffers, rel(eval(p, t, o, i).g, got.g));
        Material ir; ir.baseColor = base.baseColor; ir.metallic = 1; ir.roughness = base.roughness; ir.iridescence = 1;
        ir.iridescenceThickness = 100 + 900 * r.next(); ir.specExact = true;
        const double ar = alphaOf(ir.roughness);
        const Rgb f2 = iridescentFresnel(1.0, ir.iridescenceIor, ir.iridescenceThickness, ir.baseColor, o.x * h.x + o.y * h.y + o.z * h.z, 2, false);   // the text's Schlick interfaces
        const Rgb spec = evalTerms(ir, t, o, i).specular;
        const double dv = ggxD(h, ar, ar) * smithV(o, i, ar, ar);
        worstIrid = std::max({worstIrid, rel(spec.r, f2.r * dv), rel(spec.b, f2.b * dv)});
    }
    std::printf("spec-exact clearcoat worst %.3e, iridescence (2 harmonics) worst %.3e; the default form differs by up to %.3f\n", worstCoat, worstIrid, plainDiffers);
    CHECK(worstCoat <= 1e-12);
    CHECK(worstIrid <= 1e-12);
    CHECK(plainDiffers > 1e-3);           // the flag changes the result
    return raw_test_summary();
}
