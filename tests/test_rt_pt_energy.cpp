// The path tracer's energy checks, interface v4 (docs/rt-interface-v4.md): e1 the white furnace
// of the lobe alone, e2 the white-plane furnace at the default settings, e3 the closed-box
// furnace at the default bounce limit; each with its control.
//   test_rt_pt_energy [out.json]
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_pt_checks.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/core/parallel.hpp"
#include "../src/renderer/rt_pt_math.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
using namespace raw;
using namespace raw::rt;

namespace {
std::string js;
void add(const std::string& s) { js += (js.empty() ? "  " : ",\n  ") + s; }
int hw() { return int(std::max(1u, std::thread::hardware_concurrency())); }
struct Rng {
    std::uint64_t s;
    float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return float(double(s >> 11) * (1.0 / 9007199254740992.0)); }
};
// e1 (interface v4): the white furnace of the lobe alone, by the tracer's own sampler. White base,
// metal, dielectric at the default specular 0.5 and at 1, and half metal; roughness 0.05 to 1 and
// mu 0.05 to 1. Each directional albedo is within 4 standard errors plus 1e-3 of 1; the control,
// interface v3's lobe, creates light at grazing views and loses it on rough metal.
void bsdfFurnace(bool legacy) {
    const float roughs[] = {0.05f, 0.1f, 0.2f, 0.3f, 0.5f, 0.7f, 0.85f, 1.0f}, mus[] = {0.05f, 0.1f, 0.2f, 0.4f, 0.7f, 1.0f};
    const float mats[4][2] = {{1.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, 1.0f}, {0.5f, 0.5f}};   // metallic, specular
    constexpr int kR = 8, kM = 6, kCases = 4 * kR * kM, kN = 1 << 20;
    std::vector<double> mean(kCases), se(kCases);
    parallelRows(kCases, hw(), [&](int c) {
        const int mi = c / (kR * kM), ri = (c / kM) % kR, ui = c % kM;
        pt::Bsdf m{{1, 1, 1}, roughs[ri], mats[mi][0], mats[mi][1]};
        m.legacy = legacy;
        const float mu = mus[ui];
        const Vec3 wo{std::sqrt(1.0f - mu * mu), 0.0f, mu};
        Rng r{0xe1ULL + std::uint64_t(c) * 104729ULL};
        double s = 0, s2 = 0;
        for (int k = 0; k < kN; ++k) {
            Vec3 wi;
            double w = 0;
            if (pt::sampleBsdf(m, wo, r.next(), r.next(), r.next(), wi)) {
                const pt::Eval e = pt::evalBsdf(m, wo, wi);
                if (e.pdf > 0) w = pt::luminance(e.fcos) / e.pdf;
            }
            s += w; s2 += w * w;
        }
        mean[std::size_t(c)] = s / kN;
        se[std::size_t(c)] = std::sqrt(std::max(0.0, s2 / kN - (s / kN) * (s / kN)) / (kN - 1));
    });
    double hi = 0, lo = 1e9, worstExcess = 0;
    int outside = 0, hiCase = 0, loCase = 0;
    for (int c = 0; c < kCases; ++c) {
        const double dev = std::fabs(mean[std::size_t(c)] - 1.0), bound = 4.0 * se[std::size_t(c)] + 1e-3;
        outside += dev > bound;
        worstExcess = std::max(worstExcess, dev);
        if (mean[std::size_t(c)] > hi) { hi = mean[std::size_t(c)]; hiCase = c; }
        if (mean[std::size_t(c)] < lo) { lo = mean[std::size_t(c)]; loCase = c; }
    }
    const auto name = [&](int c) {
        char b[96];
        std::snprintf(b, sizeof b, "metallic %.1f specular %.1f roughness %.2f mu %.2f", mats[c / (kR * kM)][0], mats[c / (kR * kM)][1],
                      roughs[(c / kM) % kR], mus[c % kM]);
        return std::string(b);
    };
    std::printf("e1 bsdf white furnace%s: %d cases, %d outside 4 se + 1e-3; highest %.5f (se %.1e, %s), lowest %.5f (se %.1e, %s), worst |albedo - 1| %.2e\n",
                legacy ? " (control: interface v3 lobe)" : "", kCases, outside, hi, se[std::size_t(hiCase)], name(hiCase).c_str(), lo, se[std::size_t(loCase)], name(loCase).c_str(), worstExcess);
    CHECK(legacy ? outside > 0 && hi > 1.1 && lo < 0.5 : outside == 0);
    char b[500];
    std::snprintf(b, sizeof b, "{\"check\": \"e1\", \"control_legacy_lobe\": %s, \"cases\": %d, \"samples\": %d, \"outside\": %d, \"highest\": %.6f, "
                  "\"highest_case\": \"%s\", \"lowest\": %.6f, \"lowest_case\": \"%s\", \"worst_abs_deviation\": %.3e}",
                  legacy ? "true" : "false", kCases, kN, outside, hi, name(hiCase).c_str(), lo, name(loCase).c_str(), worstExcess);
    add(b);
}
// e2 on the reference: the white plane at the default settings (PathTraceDesc and PtMaterial defaults).
void planeFurnaceCpu(const char* what, const PtMaterial& m, bool legacy) {
    const PtScene s = ptWhitePlane(m, {1.0f, 1.0f, 1.0f});
    PathTraceDesc d;
    d.width = 64; d.height = 48; d.spp = 64; d.seed = 5;
    d.camera = d.previous = whitePlaneCamera();
    d.controls.legacyLobeV2 = legacy;
    const PlaneFurnace f = planeFurnace(pathTraceCpu(s, d));
    std::printf("e2 cpu white plane %s%s: mean %.5f (se %.2e), rows %.4f to %.4f, %ld rows over -> %s\n", what, legacy ? " (control: interface v3 lobe)" : "",
                f.mean, f.se, f.lo, f.hi, f.rowsOver, f.within() ? "within" : "outside");
    CHECK(legacy ? !f.within() : f.within());
    add(planeFurnaceJson(f, what, "cpu", legacy));
}
// e3: the closed-box furnace at the default PathTraceDesc (bounce limit and Russian roulette as
// shipped): the image mean within 4 standard errors of E / (1 - a); the worst pixel is reported
// (roulette gives single pixels a heavy tail). The control cuts paths at 8 bounces, the v3 default.
void furnaceDefaults(float a, bool control) {
    const PtScene s = ptFurnace(1.0f, a);
    PathTraceDesc d;
    d.width = 48; d.height = 32; d.spp = 64;
    if (control) d.maxBounces = 8;
    d.camera = d.previous = cameraOf(furnaceBox());
    const PathTraceOutput o = pathTraceCpu(s, d);
    const double expect = 1.0 / (1.0 - a);
    double sum = 0, var = 0, worstPx = 0;
    for (std::size_t p = 0; p < o.depth.size(); ++p) {
        const double l = pt::luminance({o.radiance[p * 3], o.radiance[p * 3 + 1], o.radiance[p * 3 + 2]});
        sum += l; var += o.variance[p];
        worstPx = std::max(worstPx, std::fabs(l - expect) / std::sqrt(std::max(1e-20, double(o.variance[p]))));
    }
    const double n = double(o.depth.size()), mean = sum / n, z = std::fabs(mean - expect) / (std::sqrt(var) / n);
    const bool ok = z <= 4.0;
    std::printf("e3 cpu furnace at the defaults a=%.1f%s (maxBounces %u, roulette on): mean %.5f expect %.5f (%.4f of it), %.2f standard errors; worst pixel %.2f -> %s\n",
                a, control ? " (control: 8 bounces)" : "", d.maxBounces, mean, expect, mean / expect, z, worstPx, ok ? "within" : "outside");
    CHECK(control ? !ok : ok);
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"e3 cpu\", \"albedo\": %.1f, \"max_bounces\": %u, \"control\": %s, \"mean\": %.6f, \"expected\": %.6f, \"z\": %.3f, \"worst_pixel_z\": %.3f, \"within\": %s}",
                  a, d.maxBounces, control ? "true" : "false", mean, expect, z, worstPx, ok ? "true" : "false");
    add(b);
}
}  // namespace

int main(int argc, char** argv) {
    bsdfFurnace(false);
    bsdfFurnace(true);
    planeFurnaceCpu("default dielectric", PtMaterial{}, false);
    planeFurnaceCpu("dielectric specular 1 roughness 0.1", {1.0f, 0.1f, 0.0f, 1.0f, {}}, false);
    planeFurnaceCpu("metal roughness 1", {1.0f, 1.0f, 1.0f, 0.5f, {}}, false);
    planeFurnaceCpu("default dielectric", PtMaterial{}, true);
    planeFurnaceCpu("metal roughness 1", {1.0f, 1.0f, 1.0f, 0.5f, {}}, true);
    furnaceDefaults(0.8f, false);
    furnaceDefaults(0.9f, false);
    furnaceDefaults(0.8f, true);
    if (argc > 1)
        std::ofstream(argv[1]) << "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"path tracer interface v4, the energy checks (e1 to e3)\",\n"
                               << " \"pass\": " << (raw_test_failures() == 0 ? "true" : "false") << ",\n \"results\": [\n" << js << "\n ]\n}\n";
    return raw_test_summary();
}
