// ReSTIR DI stage 1 (evidence/restir-di-bounds.json, r1 to r4) on retro_room with 96 point lights:
// the exact mode against the path tracer, unbiasedness of uniform, ris and restir over 256 seeds
// with a false control, and single-frame error at equal shadow rays.
//   test_restir_di [out.json]
#include "raw/renderer/rt_restir.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
using namespace raw;
using namespace raw::rt;

namespace {
double lum(const float* c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
PtScene scene() {
    PtScene s = ptRetroRoom();
    s.sunIrradiance = {0, 0, 0}; s.sky = {0, 0, 0};
    for (PtMaterial& m : s.materials) m.emission = {0, 0, 0};
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (Vec3 p : s.geo.pos) { lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}; hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)}; }
    s.lights = scatterLights(96, lo, hi, 20261021, 2.0, 6.0);
    finishPtScene(s);
    return s;
}
DiDesc desc(DiMode m, std::uint64_t seed) {
    DiDesc d; d.width = 96; d.height = 64; d.mode = m; d.seed = seed;
    d.camera = {{0.0f, 1.6f, -1.0f}, {0.0f, 1.4f, -8.0f}, {0.0f, 1.0f, 0.0f}, 1.1f};
    return d;
}
double imageMean(const DiOutput& o) {
    double s = 0; long n = 0;
    for (std::size_t i = 0; i < o.covered.size(); ++i) if (o.covered[i]) { s += lum(o.radiance.data() + 3 * i); ++n; }
    return n ? s / double(n) : 0.0;
}
double relRmse(const DiOutput& a, const DiOutput& ref) {
    double d2 = 0, s = 0; long n = 0;
    for (std::size_t i = 0; i < ref.covered.size(); ++i) if (ref.covered[i]) {
        const double x = lum(a.radiance.data() + 3 * i), r = lum(ref.radiance.data() + 3 * i);
        d2 += (x - r) * (x - r); s += r; ++n;
    }
    return n && s > 0 ? std::sqrt(d2 / double(n)) / (s / double(n)) : 0.0;
}
const char* name(DiMode m) { return m == DiMode::Uniform ? "uniform" : m == DiMode::Ris ? "ris" : m == DiMode::Restir ? "restir" : m == DiMode::RestirBiased ? "restir_biased" : "exact"; }
}  // namespace

int main(int argc, char** argv) {
    const PtScene s = scene();
    const DiOutput ex = directLightCpu(s, desc(DiMode::Exact, 1));
    CHECK(ex.error.empty());
    const double exMean = imageMean(ex);
    std::string js = "{\n \"lights\": 96, \"size\": [96, 64], \"exact_mean\": " + std::to_string(exMean);

    // r1: exact against the path tracer at one bounce with every other light source off.
    PathTraceDesc pd; pd.width = 96; pd.height = 64; pd.spp = 1; pd.maxBounces = 1; pd.jitter = false; pd.seed = 1;
    pd.camera = pd.previous = desc(DiMode::Exact, 1).camera;
    const PathTraceOutput pt = pathTraceCpu(s, pd, 0);
    double worst = 0;
    for (std::size_t i = 0; i < ex.covered.size(); ++i) if (ex.covered[i]) for (int c = 0; c < 3; ++c) {
        const double a = ex.radiance[i * 3 + std::size_t(c)], b = pt.radiance[i * 3 + std::size_t(c)];
        worst = std::max(worst, std::fabs(a - b) / std::max(std::fabs(b), 1e-6));
    }
    std::printf("r1 exact against the path tracer: worst relative %.2e (bound 1e-5); exact image mean %.5f\n", worst, exMean);
    CHECK(worst <= 1e-5);
    CHECK(exMean > 0.0);
    js += ",\n \"r1_worst_relative\": " + std::to_string(worst);

    // r2 and r4: image means over 256 seeds; r3: single-frame relative RMSE, mean of the first 16.
    double rmseUniform = 0, rmseRestir = 0;
    for (DiMode m : {DiMode::Uniform, DiMode::Ris, DiMode::Restir, DiMode::RestirBiased}) {
        double s1 = 0, s2 = 0, rm = 0;
        const int F = 256;
        for (int f = 0; f < F; ++f) {
            const DiOutput o = directLightCpu(s, desc(m, std::uint64_t(f + 7)));
            const double v = imageMean(o);
            s1 += v; s2 += v * v;
            if (f < 16) rm += relRmse(o, ex) / 16.0;
            if (f == 0) CHECK(o.shadowRays <= std::uint64_t(96 * 64));   // at most one shadow ray a pixel
        }
        const double mean = s1 / F, se = std::sqrt(std::max(0.0, s2 / F - mean * mean) / (F - 1)), z = se > 0 ? (mean - exMean) / se : 0.0;
        std::printf("%s: mean %.5f (se %.1e, %.2f se from exact), single-frame relative RMSE %.4f\n", name(m), mean, se, z, rm);
        js += std::string(",\n \"") + name(m) + "\": {\"mean\": " + std::to_string(mean) + ", \"se\": " + std::to_string(se) + ", \"z\": " + std::to_string(z) + ", \"relative_rmse_single_frame\": " + std::to_string(rm) + "}";
        if (m != DiMode::RestirBiased) CHECK(std::fabs(z) <= 4.0);
        if (m == DiMode::Uniform) rmseUniform = rm;
        if (m == DiMode::Restir) rmseRestir = rm;
    }
    std::printf("r3 restir / uniform single-frame RMSE: %.3f (bound at most 0.7)\n", rmseRestir / rmseUniform);
    CHECK(rmseRestir <= 0.7 * rmseUniform);

    // r2b: ris without its 1/M must miss r2 by more than 10 standard errors.
    DiDesc c = desc(DiMode::Ris, 7); c.controlDropOneOverM = true;
    const double cm = imageMean(directLightCpu(s, c));
    std::printf("r2b control (ris without 1/M): mean %.4f against exact %.5f\n", cm, exMean);
    CHECK(cm > 10.0 * exMean);
    js += ",\n \"r3_ratio\": " + std::to_string(rmseRestir / rmseUniform) + ",\n \"r2b_control_mean\": " + std::to_string(cm) + "\n}\n";
    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << js;
    return raw_test_summary();
}
