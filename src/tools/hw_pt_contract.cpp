// HW H2.0's equivalence contract for the hardware path tracer (evidence/hw-h2-pt-bounds.json).
#include "raw/tools/hw_pt_cmd.hpp"
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/renderer/rt_pt_hw.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "rr_scenes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
namespace raw {
namespace {
double lum(const std::vector<float>& c, std::size_t p) { return 0.2126 * c[p * 3] + 0.7152 * c[p * 3 + 1] + 0.0722 * c[p * 3 + 2]; }
struct Stat {
    double meanAbsZ{0}, fracOver4{0}, imageZ{0}, identical{0}, meanRel{0}, maxRel{0};
    long nonfinite{0};
    bool ok() const { return nonfinite == 0 && meanAbsZ <= 0.9 && fracOver4 <= 0.002 && imageZ <= 3.0; }
};
Stat compare(const rt::PathTraceOutput& a, const rt::PathTraceOutput& b) {
    Stat s;
    const std::size_t n = a.depth.size();
    double sa = 0, sb = 0, va = 0, vb = 0, sumZ = 0;
    long over = 0, same = 0;
    for (std::size_t p = 0; p < n; ++p) {
        const double x = lum(a.radiance, p), y = lum(b.radiance, p), v = double(a.variance[p]) + double(b.variance[p]);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(v)) { ++s.nonfinite; continue; }
        const double z = v > 0 ? std::fabs(x - y) / std::sqrt(v) : (x == y ? 0.0 : 1e9);
        sumZ += std::min(z, 1e9); over += z > 4.0;
        sa += x; sb += y; va += a.variance[p]; vb += b.variance[p];
        same += std::memcmp(&a.radiance[p * 3], &b.radiance[p * 3], 12) == 0;
        const double rel = std::fabs(x - y) / std::max(1e-3, std::fabs(y));
        s.meanRel += rel; s.maxRel = std::max(s.maxRel, rel);
    }
    s.meanAbsZ = sumZ / double(n); s.fracOver4 = double(over) / double(n);
    s.imageZ = std::fabs(sa - sb) / std::sqrt(std::max(1e-30, va + vb));
    s.identical = double(same) / double(n); s.meanRel /= double(n);
    return s;
}
std::string statJson(const char* check, const std::string& scene, const char* what, const Stat& s, bool mustFail, bool bounded = true) {
    char b[700];
    std::snprintf(b, sizeof b, "{\"check\": \"%s\", \"scene\": \"%s\", \"case\": \"%s\", \"mean_abs_z\": %.4f, \"fraction_z_over_4\": %.5f, "
                  "\"image_z\": %.3f, \"nonfinite\": %ld, \"fraction_bit_identical\": %.5f, \"mean_rel_diff\": %.3e, \"max_rel_diff\": %.3e, "
                  "\"within_q1_bound\": %s, \"verdict\": \"%s\"}",
                  check, scene.c_str(), what, s.meanAbsZ, s.fracOver4, s.imageZ, s.nonfinite, s.identical, s.meanRel, s.maxRel,
                  s.ok() ? "true" : "false", !bounded ? "reported" : (s.ok() != mustFail) ? "pass" : "FAIL");
    return b;
}
struct Case { std::string name; rt::PtScene s; rt::PtCamera cam; int w, h; };
rt::PathTraceDesc desc(const Case& c, std::uint32_t spp, std::uint64_t seed) {
    rt::PathTraceDesc d;
    d.width = c.w; d.height = c.h; d.spp = spp; d.seed = seed; d.maxBounces = 64;
    d.camera = d.previous = c.cam;
    return d;
}
double diagonal(const rt::PtScene& s) {
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const Tri& t : rt::trianglesOf(s.geo)) for (Vec3 v : {t.a, t.b, t.c}) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    const Vec3 d = hi - lo;
    return std::sqrt(double(d.x) * d.x + double(d.y) * d.y + double(d.z) * d.z);
}
struct Run { rhi::Device& dev; std::string rows, error; bool all{true}, fallback{false}; };
void add(Run& r, const std::string& row, bool pass) { r.rows += (r.rows.empty() ? "  " : ",\n  ") + row; r.all = r.all && pass; }
bool hw(Run& r, const Case& c, const rt::PathTraceDesc& d, rt::PathTraceOutput& o, const gpu_check::HwPtOptions& opt = {}) {
    gpu_check::HwPtResult h = gpu_check::pathTraceHw(r.dev, c.s, d, opt);
    if (!h.out.error.empty()) { r.error = h.out.error; return false; }
    o = std::move(h.out);
    return true;
}
// Q1, Q2 and (on retro_room) C1 to C3 for one scene.
void scene(Run& r, const Case& c, std::uint32_t spp, bool controls) {
    rt::PathTraceOutput h, cpu;
    if (!hw(r, c, desc(c, spp, 11), h)) return;
    cpu = rt::pathTraceCpu(c.s, desc(c, spp, 9001));
    const Stat q1 = compare(h, cpu);
    add(r, statJson("Q1", c.name, "hardware against cpu", q1, false), q1.ok());
    const rt::PathTraceOutput g = gpu_check::pathTraceGpu(r.dev, c.s, desc(c, spp, 11));
    if (!g.error.empty()) { r.error = g.error; return; }
    const Stat q2 = compare(h, g);
    add(r, statJson("Q2", c.name, "hardware against compute, same seed", q2, false), q2.ok());
    if (!controls) return;
    for (int k = 0; k < 2; ++k) {
        rt::PathTraceDesc d = desc(c, spp, 11);
        (k == 0 ? d.controls.misWeightOne : d.controls.dropLambertCosine) = true;
        rt::PathTraceOutput x;
        if (!hw(r, c, d, x)) return;
        const Stat s = compare(x, cpu);
        add(r, statJson(k == 0 ? "C1" : "C2", c.name, k == 0 ? "control: MIS weight 1" : "control: Lambert cosine dropped", s, true), !s.ok());
    }
    const double diag = diagonal(c.s);
    for (double share : {0.001, 0.003, 0.01, 0.03}) {
        gpu_check::HwPtOptions o;
        const float m = float(share * diag / std::sqrt(3.0));
        o.offset[0] = o.offset[1] = o.offset[2] = m;
        rt::PathTraceOutput x;
        if (!hw(r, c, desc(c, spp, 11), x, o)) return;
        const Stat s = compare(x, cpu);
        char what[96];
        std::snprintf(what, sizeof what, "control: instance offset %.1f%% of the diagonal (%.4f)", share * 100, double(m) * std::sqrt(3.0));
        const bool bounded = share == 0.01;   // C3; the others are the reported sweep
        add(r, statJson(bounded ? "C3" : "C3_sweep", c.name, what, s, true, bounded), bounded ? !s.ok() : true);
    }
}
}  // namespace

HwPtReport hwPtContract(rhi::Device& dev, bool quick) {
    Run r{dev};
    std::vector<Case> cases;
    cases.push_back({"retro_room", rt::ptRetroRoom(), rt::cameraOf(swr::retroRoom()), 128, 96});
    cases.push_back({"iso_street", rt::ptIsoStreet(), rt::cameraOf(swr::isoStreet()), 128, 96});
    if (!quick) {
        Case hall{"raw_hall", {}, {}, 160, 90};
        std::string lighting;
        if (rr::scene("raw_hall", hall.s, hall.cam, lighting)) cases.push_back(std::move(hall));
    }
    const std::uint32_t spp = quick ? 64 : 256;
    for (const Case& c : cases) {
        if (!r.error.empty()) break;
        scene(r, c, spp, c.name == "retro_room");
    }
    // Q3: forced off is a separate process (RAW_NATIVE_HW_DISABLE is read per call); here the error
    // path counts as the fallback when the device has no tier 1.1.
    r.fallback = !r.error.empty() && (r.error.find("forced off") != std::string::npos || r.error.find("no DXR") != std::string::npos);
    HwPtReport rep;
    rep.pass = r.error.empty() && r.all;
    rep.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-h2-pt-bounds.json\",\n \"backend\": \"" +
               rhi::hw::jsonEscape(dev.backendName()) + "\",\n \"adapter\": \"" + rhi::hw::jsonEscape(dev.adapter().description) +
               "\",\n \"driver\": \"" + rhi::hw::jsonEscape(dev.adapter().driver) + "\",\n \"quick\": " + (quick ? "true" : "false") +
               ",\n \"spp\": " + std::to_string(spp) + ",\n \"error\": \"" + rhi::hw::jsonEscape(r.error) + "\",\n \"fallback\": " +
               (r.fallback ? "true" : "false") + ",\n \"results\": [\n" + r.rows + "\n ],\n \"pass\": " + (rep.pass ? "true" : "false") + "\n}\n";
    return rep;
}

}  // namespace raw
