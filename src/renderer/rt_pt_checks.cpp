// P5 checks on the path tracer's split radiance: see raw/renderer/rt_pt_checks.hpp.
#include "raw/renderer/rt_pt_checks.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::rt {
namespace {
double lum3(const std::vector<float>& v, std::size_t p) { return 0.2126 * v[p * 3] + 0.7152 * v[p * 3 + 1] + 0.0722 * v[p * 3 + 2]; }
const char* tf(bool b) { return b ? "true" : "false"; }
}  // namespace

SplitSum splitSum(const PathTraceOutput& o) {
    SplitSum s;
    for (std::size_t i = 0; i < o.radiance.size() && i < o.diffuse.size(); ++i) {
        const double r = o.radiance[i], sum = double(o.diffuse[i]) + double(o.specular[i]) + double(o.emission[i]);
        ++s.values;
        if (!std::isfinite(r) || !std::isfinite(sum)) { ++s.nonfinite; continue; }
        const double ratio = std::fabs(sum - r) / (1e-4 * (1.0 + std::fabs(r)));
        s.worstRatio = std::max(s.worstRatio, ratio);
        s.over += ratio > 1.0;
    }
    return s;
}
std::string splitSumJson(const SplitSum& s, const char* scene, const char* side, bool control) {
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"P5a\", \"scene\": \"%s\", \"side\": \"%s\", \"control\": %s, \"values\": %ld, \"over\": %ld, "
                  "\"nonfinite\": %ld, \"worst_ratio_to_bound\": %.4g, \"within\": %s}",
                  scene, side, tf(control), s.values, s.over, s.nonfinite, s.worstRatio, tf(s.within()));
    return b;
}

FurnaceSplit furnaceSplit(const PathTraceOutput& o, float E, float a) {
    FurnaceSplit f;
    f.expected = double(E) * a / (1.0 - a);
    const std::size_t n = o.depth.size();
    double sum = 0, var = 0;
    for (std::size_t p = 0; p < n; ++p) {
        for (int k = 0; k < 3; ++k) {
            f.specularNonzero += o.specular[p * 3 + std::size_t(k)] != 0.0f;
            f.emissionWrong += o.emission[p * 3 + std::size_t(k)] != E;
        }
        const double l = lum3(o.diffuse, p);
        sum += l; var += o.diffuseVariance[p];
        f.worstPixelZ = std::max(f.worstPixelZ, std::fabs(l - f.expected) / std::sqrt(std::max(1e-20, double(o.diffuseVariance[p]))));
    }
    f.meanDiffuse = sum / double(n);
    f.z = std::fabs(f.meanDiffuse - f.expected) / (std::sqrt(var) / double(n));
    return f;
}
std::string furnaceSplitJson(const FurnaceSplit& f, float a, const char* side, bool control) {
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"P5b\", \"albedo\": %.1f, \"side\": \"%s\", \"control\": %s, \"specular_nonzero\": %ld, \"emission_wrong\": %ld, "
                  "\"mean_diffuse\": %.6f, \"expected\": %.6f, \"z\": %.3f, \"worst_pixel_z\": %.3f, \"within\": %s}",
                  a, side, tf(control), f.specularNonzero, f.emissionWrong, f.meanDiffuse, f.expected, f.z, f.worstPixelZ, tf(f.within()));
    return b;
}

SplitConv splitConv(const PathTraceOutput& g, const PathTraceOutput& c, bool specular) {
    SplitConv r;
    const std::vector<float>& gv = specular ? g.specular : g.diffuse;
    const std::vector<float>& cv = specular ? c.specular : c.diffuse;
    const std::vector<float>& gs = specular ? g.specularVariance : g.diffuseVariance;
    const std::vector<float>& cs = specular ? c.specularVariance : c.diffuseVariance;
    const std::size_t n = g.depth.size();
    long over = 0;
    double sumZ = 0, sg = 0, sc = 0, vg = 0, vc = 0;
    for (std::size_t p = 0; p < n; ++p) {
        const double a = lum3(gv, p), b = lum3(cv, p), v = double(gs[p]) + double(cs[p]);
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(v)) { ++r.nonfinite; continue; }
        const double z = v > 0 ? std::fabs(a - b) / std::sqrt(v) : (a == b ? 0.0 : 1e9);
        sumZ += std::min(z, 1e9); over += z > 4.0;
        sg += a; sc += b; vg += gs[p]; vc += cs[p];
    }
    r.meanAbsZ = sumZ / double(n); r.fracOver4 = double(over) / double(n);
    r.imageZ = vg + vc > 0 ? std::fabs(sg - sc) / std::sqrt(vg + vc) : (sg == sc ? 0.0 : 1e9);
    return r;
}
std::string splitConvJson(const SplitConv& v, const char* scene, bool specular, bool control) {
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"P5c\", \"scene\": \"%s\", \"component\": \"%s\", \"control\": %s, \"mean_abs_z\": %.4f, "
                  "\"fraction_z_over_4\": %.5f, \"image_mean_z\": %.3f, \"nonfinite\": %ld, \"within\": %s}",
                  scene, specular ? "specular" : "diffuse", tf(control), v.meanAbsZ, v.fracOver4, v.imageZ, v.nonfinite, tf(v.within()));
    return b;
}

PlaneFurnace planeFurnace(const PathTraceOutput& o) {
    PlaneFurnace f;
    const std::size_t W = std::size_t(o.width), H = std::size_t(o.height);
    double sum = 0, var = 0;
    f.lo = 1e30; f.hi = -1e30;
    for (std::size_t y = 0; y < H; ++y) {
        double rs = 0, rv = 0;
        for (std::size_t x = 0; x < W; ++x) {
            const std::size_t p = y * W + x;
            ++f.pixels;
            f.misses += o.triangle[p] < 0;
            const double l = lum3(o.radiance, p), v = o.variance[p];
            if (!std::isfinite(l) || !std::isfinite(v)) { ++f.nonfinite; continue; }
            rs += l; rv += v;
        }
        const double rm = rs / double(W), rse = std::sqrt(rv) / double(W);
        f.lo = std::min(f.lo, rm); f.hi = std::max(f.hi, rm);
        f.rowsOver += std::fabs(rm - 1.0) > 4.0 * rse + PlaneFurnace::kTol;
        sum += rs; var += rv;
    }
    f.mean = sum / double(f.pixels);
    f.se = std::sqrt(var) / double(f.pixels);
    return f;
}
std::string planeFurnaceJson(const PlaneFurnace& f, const char* material, const char* side, bool control) {
    char b[500];
    std::snprintf(b, sizeof b, "{\"check\": \"e2\", \"material\": \"%s\", \"side\": \"%s\", \"control_legacy_lobe\": %s, \"pixels\": %ld, \"misses\": %ld, "
                  "\"mean\": %.6f, \"standard_error\": %.3g, \"lowest_row\": %.5f, \"highest_row\": %.5f, \"rows_over\": %ld, \"nonfinite\": %ld, \"within\": %s}",
                  material, side, tf(control), f.pixels, f.misses, f.mean, f.se, f.lo, f.hi, f.rowsOver, f.nonfinite, tf(f.within()));
    return b;
}

}  // namespace raw::rt
