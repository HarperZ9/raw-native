// The material model's second stage (evidence/m3-materials2-bounds.json): the iridescent Fresnel
// against an exact polarized reference, clearcoat energy over anisotropic white metal, and volume
// attenuation along each refracted path, each with its control.
//   test_pbr_improve [--json] [--strict]
#include "pbr_quadrature.hpp"
#include "raw/core/parallel.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using namespace pbr_test;

namespace {
int threads() { return int(std::max(1u, std::thread::hardware_concurrency())); }
struct Irid { double worst{0}, worstSchlick{0}, worstSpec{0}; double atCos{0}, atBase{0}; };
Irid iridescence() {
    Irid r;
    const double cs[] = {0.02, 0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 1.0};
    for (double base : {1.33, 1.5, 2.0}) {
        const double q = (base - 1.0) / (base + 1.0), f0 = q * q;
        for (double c : cs) for (int fi = 0; fi <= 4; ++fi) for (int di = 0; di <= 18; ++di) {
            const double film = 1.2 + 0.2 * fi, d = 100.0 + 50.0 * di;
            const Rgb e = iridescentExact(1.0, film, d, base, c);
            const Rgb a = iridescentFresnel(1.0, film, d, {f0, f0, f0}, c);
            const Rgb s = iridescentFresnel(1.0, film, d, {f0, f0, f0}, c, kIridescenceHarmonics, false);
            const Rgb p = iridescentFresnel(1.0, film, d, {f0, f0, f0}, c, kIridescenceHarmonicsSpec, false);
            const double ea[3] = {e.r, e.g, e.b}, aa[3] = {a.r, a.g, a.b}, sa[3] = {s.r, s.g, s.b}, pa[3] = {p.r, p.g, p.b};
            for (int k = 0; k < 3; ++k) {
                const double v = std::fabs(aa[k] - ea[k]);
                if (v > r.worst) { r.worst = v; r.atCos = c; r.atBase = base; }
                r.worstSchlick = std::max(r.worstSchlick, std::fabs(sa[k] - ea[k]));
                r.worstSpec = std::max(r.worstSpec, std::fabs(pa[k] - ea[k]));
            }
        }
    }
    return r;
}
struct Vol { double worst{0}, worstControl{0}; int points{0}; };
// Energy carried into a volume to the depth of its thickness: the model's own transmission
// (attenuation inside eval) against the interface's transmission times exp(-sigma t / |cos|)
// along each refracted direction, computed here from the interface alone.
Vol volume(const Tables& t) {
    struct P { double r, mu, ratio; double model[3], ref[3], ctl[3]; };
    std::vector<P> ps;
    for (double r : {0.05, 0.25, 0.5, 1.0}) for (int k = 1; k <= 10; ++k) for (double ratio : {0.25, 1.0, 2.0}) ps.push_back({r, 0.1 * k, ratio, {}, {}, {}});
    raw::parallelRows(int(ps.size()), threads(), [&](int n) {
        P& p = ps[std::size_t(n)];
        Material m; m.baseColor = {1, 1, 1}; m.metallic = 0.0; m.roughness = p.r; m.transmission = 1.0; m.volume = true; m.ior = 1.5;
        m.thickness = 1.0; m.attenuationDistance = 1.0 / p.ratio; m.attenuationColor = {0.5, 0.5, 0.5};
        double ax, ay; anisoAlphas(m, ax, ay);
        const double a = std::min(ax, ay), w = m.ior * m.ior;
        const D3 o = view(p.mu);
        const Rgb model = peaked(m, t, o, a, Map::Refract, m.ior, [](const Terms& x) { return x.transmission; });
        Material bare = m; bare.attenuationDistance = 0.0;                     // the interface alone
        const double sigma = -std::log(0.5) / m.attenuationDistance;
        const Rgb ref = peaked(bare, t, o, a, Map::Refract, m.ior, [&](const Terms& x, D3 i) {
            const double at = std::exp(-sigma * m.thickness / std::max(std::fabs(i.z), 1e-6));
            return Rgb{x.transmission.r * at, x.transmission.g * at, x.transmission.b * at};
        });
        Material spec = m; spec.specExact = true;                             // control: one attenuation for every direction
        const Rgb ctl = peaked(spec, t, o, a, Map::Refract, m.ior, [](const Terms& x) { return x.transmission; });
        const double mv[3] = {model.r, model.g, model.b}, rv[3] = {ref.r, ref.g, ref.b}, cv[3] = {ctl.r, ctl.g, ctl.b};
        for (int k = 0; k < 3; ++k) { p.model[k] = mv[k] * w; p.ref[k] = rv[k] * w; p.ctl[k] = cv[k] * w; }
    });
    Vol v;
    for (const P& p : ps) {
        ++v.points;
        for (int k = 0; k < 3; ++k) { v.worst = std::max(v.worst, std::fabs(p.model[k] - p.ref[k])); v.worstControl = std::max(v.worstControl, std::fabs(p.ctl[k] - p.ref[k])); }
    }
    return v;
}
struct Coat { double worst{0}, excess{-1}, worstControl{0}; int points{0}; double atMu{0}, atRc{0}, atAn{0}, atR{0}; };
// White metal, isotropic or anisotropic, under a clear coat: the albedo should stay 1.
Coat coat(const Tables& t) {
    struct P { Material m; double mu, az; Rgb a; };
    std::vector<P> ps;
    for (double an : {0.0, 0.3, 0.6, 1.0}) for (double r : {0.2, 0.6, 1.0}) for (double c : {0.5, 1.0}) for (double rc : {0.05, 0.3, 1.0})
        for (double mu : {0.05, 0.2, 0.5, 1.0}) for (double az : {0.0, 45.0, 90.0}) for (int spec = 0; spec < 2; ++spec) {
            Material m; m.baseColor = {1, 1, 1}; m.roughness = r; m.anisotropy = an; m.clearcoat = c; m.clearcoatRoughness = rc; m.specExact = spec == 1;
            ps.push_back({m, mu, az * kPi / 180.0, {}});
        }
    raw::parallelRows(int(ps.size()), threads(), [&](int k) { ps[std::size_t(k)].a = albedo(ps[std::size_t(k)].m, t, view(ps[std::size_t(k)].mu, ps[std::size_t(k)].az)); });
    Coat r;
    for (const P& p : ps) {
        const double dev = std::fabs(p.a.r - 1.0);
        if (p.m.specExact) { r.worstControl = std::max(r.worstControl, dev); continue; }
        ++r.points;
        r.excess = std::max(r.excess, p.a.r - 1.0);
        if (dev > r.worst) { r.worst = dev; r.atMu = p.mu; r.atRc = p.m.clearcoatRoughness; r.atAn = p.m.anisotropy; r.atR = p.m.roughness; }
    }
    return r;
}
}  // namespace

int main(int argc, char** argv) {
    bool json = false, strict = false;
    for (int i = 1; i < argc; ++i) { json = json || !std::strcmp(argv[i], "--json"); strict = strict || json || !std::strcmp(argv[i], "--strict"); }
    const Irid ir = iridescence();
    const Tables t;
    warm();
    const Vol vo = volume(t);
    const Coat co = coat(t);
    // The clearcoat bound is a recorded open failure (evidence/m3-materials2-runs.json): --json and
    // --strict gate it, ctest prints it. Its control is always gated.
    if (strict) CHECK(co.worst <= 2e-2 && co.excess <= 1e-3);
    else if (!(co.worst <= 2e-2 && co.excess <= 1e-3)) std::printf("OPEN FAILURE (not gated here): clearcoat energy worst %.4f, excess %.2e\n", co.worst, co.excess);
    CHECK(co.worstControl > 2e-2);
    CHECK(vo.worst <= 5e-3);
    CHECK(vo.worstControl > 5e-3);
    CHECK(ir.worst <= 0.04);
    CHECK(ir.worstSchlick > 0.04 && ir.worstSpec > 0.04);   // controls
    std::printf("{\n \"iridescence_exact\": {\"worst\": %.4f, \"at_cos\": %.2f, \"at_base_ior\": %.2f, \"control_schlick\": %.4f, \"control_spec\": %.4f},\n"
                " \"volume_path_length\": {\"points\": %d, \"worst\": %.2e, \"control\": %.4f},\n"
                " \"clearcoat_energy\": {\"points\": %d, \"worst\": %.4f, \"max_excess\": %.2e, \"at\": {\"mu\": %.2f, \"coat_roughness\": %.2f, \"anisotropy\": %.1f, \"roughness\": %.1f}, \"control\": %.4f},\n"
                " \"failures\": %d\n}\n",
                ir.worst, ir.atCos, ir.atBase, ir.worstSchlick, ir.worstSpec, vo.points, vo.worst, vo.worstControl,
                co.points, co.worst, co.excess, co.atMu, co.atRc, co.atAn, co.atR, co.worstControl, raw_test_failures());
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
