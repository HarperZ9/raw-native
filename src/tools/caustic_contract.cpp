// Effect C1's equivalence contract (evidence/hw-c1-caustic-bounds.json): K1, K2, D1 to D3, sweeps.
#include "raw/tools/caustic_cmd.hpp"
#include "raw/renderer/rt_caustic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
namespace raw {
namespace {
constexpr int kB = 16;
// B batch renders: per pixel and channel the mean and the sample variance of the mean's batches.
struct Batches { std::vector<double> mean, var, lightMean; double sum[3]{}, sumVar[3]{}; long nonfinite{0}; std::string error; };
Batches batches(const std::function<rt::CausticImage(int)>& render) {
    Batches b;
    std::vector<std::vector<double>> img;
    for (int i = 0; i < kB; ++i) {
        rt::CausticImage im = render(i);
        if (!im.error.empty()) { b.error = im.error; return b; }
        if (b.lightMean.empty()) b.lightMean.assign(im.lightXyz.size(), 0.0);
        for (std::size_t q = 0; q < im.lightXyz.size(); ++q) b.lightMean[q] += im.lightXyz[q] / kB;
        img.push_back(std::move(im.xyz));
    }
    const std::size_t n = img[0].size();
    b.mean.assign(n, 0.0); b.var.assign(n, 0.0);
    double sums[kB][3] = {};
    for (std::size_t q = 0; q < n; ++q) {
        double m = 0, v = 0;
        for (int i = 0; i < kB; ++i) { m += img[std::size_t(i)][q]; sums[i][q % 3] += img[std::size_t(i)][q]; }
        m /= kB;
        for (int i = 0; i < kB; ++i) { const double e = img[std::size_t(i)][q] - m; v += e * e; }
        b.mean[q] = m; b.var[q] = v / (kB - 1);
        b.nonfinite += !std::isfinite(m) || !std::isfinite(v);
    }
    for (int ch = 0; ch < 3; ++ch) {
        double m = 0, v = 0;
        for (int i = 0; i < kB; ++i) m += sums[i][ch];
        m /= kB;
        for (int i = 0; i < kB; ++i) v += (sums[i][ch] - m) * (sums[i][ch] - m);
        b.sum[ch] = m; b.sumVar[ch] = v / (kB - 1);
    }
    return b;
}
struct Cmp { double meanAbsZ{0}, fracOver4{0}, imageZ[3]{}; long nonfinite{0}; bool ok() const {
    return nonfinite == 0 && meanAbsZ <= 0.9 && fracOver4 <= 0.005 && imageZ[0] <= 3 && imageZ[1] <= 3 && imageZ[2] <= 3; } };
// mask: per pixel, true where the comparison counts (empty: every pixel). Image sums stay whole-image.
Cmp compare(const Batches& a, const Batches& b, const std::vector<bool>& mask = {}) {
    Cmp c;
    c.nonfinite = a.nonfinite + b.nonfinite;
    std::size_t n = 0;
    double sz = 0; long over = 0;
    for (std::size_t q = 0; q < a.mean.size(); ++q) {
        if (!mask.empty() && !mask[q / 3]) continue;
        ++n;
        const double v = (a.var[q] + b.var[q]) / kB, d = std::fabs(a.mean[q] - b.mean[q]);
        const double z = v > 0 ? d / std::sqrt(v) : (d == 0 ? 0.0 : 1e9);
        sz += std::min(z, 1e9); over += z > 4.0;
    }
    c.meanAbsZ = n ? sz / double(n) : 0.0; c.fracOver4 = n ? double(over) / double(n) : 0.0;
    for (int ch = 0; ch < 3; ++ch)
        c.imageZ[ch] = std::fabs(a.sum[ch] - b.sum[ch]) / std::sqrt(std::max(1e-300, (a.sumVar[ch] + b.sumVar[ch]) / kB));
    return c;
}
std::string row(const char* check, const char* what, const Cmp& c, bool mustFail, const std::string& extra = "") {
    char b[700];
    std::snprintf(b, sizeof b, "{\"check\": \"%s\", \"case\": \"%s\", \"mean_abs_z\": %.4f, \"fraction_z_over_4\": %.5f, \"image_z_xyz\": [%.3f, %.3f, %.3f], "
                  "\"nonfinite\": %ld, \"within_k1_bound\": %s%s, \"verdict\": \"%s\"}", check, what, c.meanAbsZ, c.fracOver4,
                  c.imageZ[0], c.imageZ[1], c.imageZ[2], c.nonfinite, c.ok() ? "true" : "false", extra.c_str(), (c.ok() != mustFail) ? "pass" : "FAIL");
    return b;
}
double diagonal(const rt::CausticScene& s) {
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
    for (std::size_t i = 0; i < s.tris.size(); ++i) { lo[i % 3] = std::min(lo[i % 3], double(s.tris[i])); hi[i % 3] = std::max(hi[i % 3], double(s.tris[i])); }
    return std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
}
struct Run { rhi::Device& dev; std::string rows, error; bool all{true}; };
void add(Run& r, const std::string& s, bool pass) { r.rows += (r.rows.empty() ? "  " : ",\n  ") + s; r.all = r.all && pass; }
Batches hwBatches(Run& r, const rt::CausticScene& s, rt::CausticDesc d, std::uint64_t seed0, const float* off = nullptr) {
    Batches b = batches([&](int i) { d.seed = seed0 + std::uint64_t(i); return rt::causticHw(r.dev, s, d, off); });
    if (!b.error.empty() && r.error.empty()) r.error = b.error;
    return b;
}
// Addendum 1 (written after run 1): every K1 comparison is also made on the caustic region, the pixels
// where the CPU reference's light paths carry at least 5% of Y. A case is within the K1 bound only when
// both the whole image and the region are.
std::vector<bool> causticRegion(const Batches& cpu, long& count) {
    std::vector<bool> m(cpu.mean.size() / 3, false);
    count = 0;
    for (std::size_t p = 0; p < m.size(); ++p) {
        m[p] = cpu.mean[p * 3 + 1] > 0 && cpu.lightMean[p * 3 + 1] >= 0.05 * cpu.mean[p * 3 + 1];
        count += m[p];
    }
    return m;
}
void both(Run& r, const char* check, const std::string& what, const Batches& hw, const Batches& cpu, const std::vector<bool>& region,
          bool mustFail, bool bounded) {
    const Cmp w = compare(hw, cpu), g = compare(hw, cpu, region);
    const bool ok = w.ok() && g.ok();
    char extra[200];
    std::snprintf(extra, sizeof extra, ", \"region_mean_abs_z\": %.4f, \"region_fraction_z_over_4\": %.5f, \"within_k1_bound_with_region\": %s",
                  g.meanAbsZ, g.fracOver4, ok ? "true" : "false");
    std::string row0 = row(check, what.c_str(), w, mustFail, extra);
    const std::string verdict = !bounded ? "reported" : ((ok != mustFail) ? "pass" : "FAIL");
    const std::size_t v = row0.rfind("\"verdict\": \"");
    row0 = row0.substr(0, v) + "\"verdict\": \"" + verdict + "\"}";
    add(r, row0, !bounded || (ok != mustFail));
}
void k1AndControls(Run& r, bool quick) {
    const rt::CausticScene s = rt::causticPrismRoom(0);
    rt::CausticDesc d;
    d.width = quick ? 96 : 192; d.height = quick ? 54 : 108; d.spp = 4; d.photonsPerPixel = 4; d.maxBounces = 12;
    const Batches cpu = batches([&](int i) { rt::CausticDesc e = d; e.seed = 5000 + std::uint64_t(i); return rt::causticCpu(s, e); });
    const Batches hw = hwBatches(r, s, d, 1000);
    if (!r.error.empty()) return;
    long regionPx = 0;
    const std::vector<bool> region = causticRegion(cpu, regionPx);
    both(r, "K1", "hardware against cpu (region " + std::to_string(regionPx) + " px)", hw, cpu, region, false, true);
    rt::CausticDesc d1 = d; d1.controls.noDispersion = true;
    both(r, "D1", "control: no dispersion (d-line index at every wavelength)", hwBatches(r, s, d1, 1000), cpu, region, true, true);
    rt::CausticDesc d2 = d; d2.controls.dropSplatCosine = true;
    both(r, "D2", "control: splat cosine dropped", hwBatches(r, s, d2, 1000), cpu, region, true, true);
    const double diag = diagonal(s);
    for (double share : {0.001, 0.003, 0.01, 0.03}) {
        const float m = float(share * diag / std::sqrt(3.0)), off[3] = {m, m, m};
        char what[96];
        std::snprintf(what, sizeof what, "control: instance offset %.1f%% of the diagonal (%.4f)", share * 100, share * diag);
        both(r, share == 0.01 ? "D3" : "D3_sweep", what, hwBatches(r, s, d, 1000, off), cpu, region, true, share == 0.01);
    }
    for (double dn : {1e-4, 3e-4, 1e-3, 3e-3}) {
        rt::CausticDesc e = d; e.controls.indexOffset = dn;
        char what[64];
        std::snprintf(what, sizeof what, "sweep: glass index + %.0e", dn);
        both(r, "index_sweep", what, hwBatches(r, s, e, 1000), cpu, region, true, false);
    }
}void k2(Run& r, bool quick) {
    const rt::CausticScene s = rt::causticPrismRoom(1, 8.0f, 30.0f);
    const int glass = rt::causticGlassPixels(s, 96, 54);
    rt::CausticDesc d;
    d.width = 96; d.height = 54; d.spp = quick ? 4 : 16; d.photonsPerPixel = quick ? 4 : 16;
    const Batches split = hwBatches(r, s, d, 2000);
    rt::CausticDesc e = d; e.split = false; e.spp = quick ? 256 : 1024; e.photonsPerPixel = 0;
    const Batches ref = hwBatches(r, s, e, 3000);
    if (!r.error.empty()) return;
    const Cmp c = compare(split, ref);
    const double power = std::sqrt(ref.sumVar[1] / kB) / std::max(1e-300, ref.sum[1]);
    char extra[160];
    std::snprintf(extra, sizeof extra, ", \"reference_se_share_of_y\": %.5f, \"glass_pixels_in_view\": %d", power, glass);
    add(r, row("K2", "split estimator against bsdf-only, no glass in view", c, false, extra), c.ok() && power <= 0.02 && glass == 0);
    // Addendum 2 (after run 2), reported only: the same test between two BSDF-only seed sets, which share
    // an expectation exactly; how often it fails measures the test's calibration on this heavy-tailed estimator.
    const Batches ref2 = hwBatches(r, s, e, 4000);
    if (!r.error.empty()) return;
    const Cmp nul = compare(ref2, ref);
    std::string nr = row("K2_null", "bsdf-only against bsdf-only, other seeds (calibration, reported)", nul, false);
    nr = nr.substr(0, nr.rfind("\"verdict\": \"")) + "\"verdict\": \"reported\"}";
    add(r, nr, true);
}
}  // namespace

CausticReport causticContract(rhi::Device& dev, bool quick) {
    Run r{dev};
    k1AndControls(r, quick);
    if (r.error.empty()) k2(r, quick);
    CausticReport rep;
    rep.pass = r.error.empty() && r.all;
    rep.json = "{\n \"schema\": \"raw-native.evidence/1\",\n \"bounds\": \"evidence/hw-c1-caustic-bounds.json\",\n \"backend\": \"" +
               rhi::hw::jsonEscape(dev.backendName()) + "\",\n \"adapter\": \"" + rhi::hw::jsonEscape(dev.adapter().description) +
               "\",\n \"driver\": \"" + rhi::hw::jsonEscape(dev.adapter().driver) + "\",\n \"quick\": " + (quick ? "true" : "false") +
               ",\n \"batches\": " + std::to_string(kB) + ",\n \"error\": \"" + rhi::hw::jsonEscape(r.error) + "\",\n \"results\": [\n" + r.rows +
               "\n ],\n \"pass\": " + (rep.pass ? "true" : "false") + "\n}\n";
    return rep;
}

}  // namespace raw
