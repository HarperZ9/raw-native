// Reciprocity of the glTF material model, the clearcoat-zero identity, and the iridescence
// fast path against its spectral reference (evidence/m3-materials-bounds.json).
//   test_pbr_reciprocity [--json]
#include "raw/renderer/pbr.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
using namespace raw::pbr;

namespace {
struct Rng {
    std::uint64_t s;
    double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s >> 11) * (1.0 / 9007199254740992.0); }
};
D3 upper(Rng& r) {
    const double mu = 0.02 + 0.98 * r.next(), ph = 2.0 * kPi * r.next(), s = std::sqrt(1.0 - mu * mu);
    return {s * std::cos(ph), s * std::sin(ph), mu};
}
Rgb rgbOf(Rng& r) { return {r.next(), r.next(), r.next()}; }
// A random material from one of the families, every extension reachable.
Material randomMaterial(Rng& r, int family) {
    Material m;
    m.baseColor = rgbOf(r); m.metallic = r.next(); m.roughness = r.next();
    m.ior = 1.0 + 1.5 * r.next(); m.specular = r.next(); m.specularColor = rgbOf(r);
    switch (family % 7) {
    case 1: m.clearcoat = r.next(); m.clearcoatRoughness = r.next();
            m.coatNormal = {0.3 * (r.next() - 0.5), 0.3 * (r.next() - 0.5), 1.0}; break;
    case 2: m.sheenColor = rgbOf(r); m.sheenRoughness = r.next(); break;
    case 3: m.transmission = r.next(); break;
    case 4: m.anisotropy = r.next(); m.anisotropyRotation = 2.0 * kPi * r.next(); break;
    case 5: m.iridescence = r.next(); m.iridescenceIor = 1.2 + 0.8 * r.next(); m.iridescenceThickness = 100.0 + 900.0 * r.next(); break;
    case 6: m.clearcoat = r.next(); m.sheenColor = rgbOf(r); m.anisotropy = r.next(); m.iridescence = r.next(); m.transmission = r.next(); break;
    default: break;
    }
    return m;
}
double relErr(Rgb a, Rgb b) {
    double e = 0.0;
    for (int k = 0; k < 3; ++k) {
        const double x = k == 0 ? a.r : k == 1 ? a.g : a.b, y = k == 0 ? b.r : k == 1 ? b.g : b.b;
        e = std::max(e, std::fabs(x - y) / std::max(1.0, std::fabs(x)));
    }
    return e;
}
bool sameBits(Rgb a, Rgb b) { return std::memcmp(&a, &b, sizeof a) == 0; }
}  // namespace

int main(int argc, char** argv) {
    const bool json = argc > 1 && std::strcmp(argv[1], "--json") == 0;
    const Tables t;
    Rng rng{0x9E3779B97F4A7C15ull};
    double worstR = 0.0, worstThin = 0.0, worstVol = 0.0;
    int nonzero = 0;
    for (int k = 0; k < 20000; ++k) {
        const Material m = randomMaterial(rng, k);
        const D3 o = upper(rng), i = upper(rng);
        const Rgb a = eval(m, t, o, i), b = eval(m, t, i, o);
        worstR = std::max(worstR, relErr(a, b));
        if (a.max3() > 0.0) ++nonzero;
        if (m.transmission > 0.0) {                    // thin-walled: light below, swapped through the mirror
            const D3 ib{i.x, i.y, -i.z}, ob{o.x, o.y, -o.z};
            worstThin = std::max(worstThin, relErr(eval(m, t, o, ib), eval(m, t, i, ob)));
        }
        double ax, ay; anisoAlphas(m, ax, ay);         // the volume interface, both ways
        const D3 in{i.x, i.y, -i.z};
        const double f1 = walterBtdf(in, m.ior, o, 1.0, ax, ay, 0.04), f2 = walterBtdf(o, 1.0, in, m.ior, ax, ay, 0.04);
        worstVol = std::max(worstVol, std::fabs(f1 / 1.0 - f2 / (m.ior * m.ior)) / std::max(1.0, std::fabs(f1)));
    }
    CHECK(worstR <= 1e-12); CHECK(worstThin <= 1e-12); CHECK(worstVol <= 1e-12); CHECK(nonzero > 19000);

    // Clearcoat factor 0 with every other coat parameter set: identical to no coat.
    Rng r2{12345};
    bool zeroSame = true;
    for (int k = 0; k < 2000; ++k) {
        Material m = randomMaterial(r2, 0), c = m;
        c.clearcoat = 0.0; c.clearcoatRoughness = r2.next(); c.coatNormal = {0.2, -0.1, 1.0};
        const D3 o = upper(r2), i = upper(r2);
        zeroSame = zeroSame && sameBits(eval(m, t, o, i), eval(c, t, o, i));
    }
    CHECK(zeroSame);

    // Iridescence: the series with the specification's interfaces (polarized = false) against the
    // spectral Airy reference with the same interfaces; it checks the harmonic truncation and the
    // Gaussian sensitivity. The default polarized path has its own exact reference (test_pbr_improve).
    double worstIr = 0.0, atIor = 0, atD = 0, atCos = 0;
    const Rgb bases[2] = {{0.04, 0.04, 0.04}, {0.95, 0.78, 0.34}};
    for (const Rgb& f0 : bases) for (int fi = 0; fi <= 4; ++fi) for (int d = 1; d <= 10; ++d) for (int c = 1; c <= 10; ++c) {
        const double ior = 1.2 + 0.2 * fi, th = 100.0 * d, cs = 0.1 * c;
        const Rgb fast = iridescentFresnel(1.0, ior, th, f0, cs, kIridescenceHarmonics, false), ref = iridescentSpectral(1.0, ior, th, f0, cs);
        const double e = std::max({std::fabs(fast.r - ref.r), std::fabs(fast.g - ref.g), std::fabs(fast.b - ref.b)});
        if (e > worstIr) { worstIr = e; atIor = ior; atD = th; atCos = cs; }
    }
    CHECK(worstIr <= 0.05);
    if (json)
        std::printf("{\n \"reciprocity_cases\": 20000,\n \"reciprocity_worst_rel\": %.3e,\n \"thin_walled_worst_rel\": %.3e,\n"
                    " \"volume_generalized_worst_rel\": %.3e,\n \"clearcoat_zero_bit_identical\": %s,\n"
                    " \"iridescence_cases\": 1000,\n \"iridescence_worst_abs\": %.4f,\n \"iridescence_worst_at\": {\"film_ior\": %.1f, \"thickness_nm\": %.0f, \"cos\": %.1f},\n"
                    " \"failures\": %d\n}\n", worstR, worstThin, worstVol, zeroSame ? "true" : "false", worstIr, atIor, atD, atCos, raw_test_failures());
    else
        std::printf("reciprocity %.3e, thin-walled %.3e, volume (generalized) %.3e, clearcoat 0 identical: %s, iridescence worst %.4f at ior %.1f, %.0f nm, cos %.1f\n",
                    worstR, worstThin, worstVol, zeroSame ? "yes" : "no", worstIr, atIor, atD, atCos);
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
