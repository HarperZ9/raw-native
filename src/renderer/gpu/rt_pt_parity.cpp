// The GPU path tracer's checks: P1 to P4 of evidence/rt-r2-bounds.json.
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace raw::gpu_check {
namespace {
float lumAt(const std::vector<float>& rgb, std::size_t p) { return 0.2126f * rgb[p * 3] + 0.7152f * rgb[p * 3 + 1] + 0.0722f * rgb[p * 3 + 2]; }
struct Ctx { rhi::Device& dev; RtPtParity& R; bool all{true}; int w{128}, h{96}; std::uint32_t spp{256}; };
void record(Ctx& c, const std::string& s, bool pass) {
    c.R.results += (c.R.results.empty() ? "  " : ",\n  ") + s;
    c.all = c.all && pass;
}
// P1: the furnace on the GPU; returns whether it was within the bound.
bool furnace(Ctx& c, float a, bool dropCos, std::string& line) {
    const rt::PtScene s = rt::ptFurnace(1.0f, a);
    rt::PathTraceDesc d;
    d.spp = 64; d.maxBounces = 64; d.russianRoulette = false; d.width = 64; d.height = 48; d.seed = 101;
    d.camera = d.previous = rt::cameraOf(rt::furnaceBox());
    d.controls.dropLambertCosine = dropCos;
    const rt::PathTraceOutput o = pathTraceGpu(c.dev, s, d);
    if (!o.error.empty()) { c.R.error = o.error; return false; }
    const double expect = 1.0 / (1.0 - a);
    double sum = 0, var = 0, worst = 0;
    for (std::size_t p = 0; p < o.depth.size(); ++p) {
        const double l = lumAt(o.radiance, p);
        sum += l; var += o.variance[p];
        worst = std::max(worst, std::fabs(l - expect) / std::sqrt(std::max(1e-20, double(o.variance[p]))));
    }
    const double n = double(o.depth.size()), mean = sum / n, z = std::fabs(mean - expect) / (std::sqrt(var) / n);
    const bool ok = z <= 4.0 && worst <= 6.0;
    char b[400];
    std::snprintf(b, sizeof b, "{\"check\": \"P1\", \"albedo\": %.1f, \"control_drop_cosine\": %s, \"mean\": %.6f, \"expected\": %.6f, \"z\": %.3f, \"worst_pixel_z\": %.3f, \"within\": %s}",
                  a, dropCos ? "true" : "false", mean, expect, z, worst, ok ? "true" : "false");
    line = b;
    return ok;
}
// P2: GPU against CPU at 256 spp each with different seeds.
struct Conv { double meanAbsZ{0}, fracOver4{0}, imageZ{0}, meanAbsZNoisy{0}; long nonfiniteGpu{0}, nonfiniteCpu{0}, zeroVar{0}; bool ok{false}; };
Conv compare(const rt::PathTraceOutput& g, const rt::PathTraceOutput& c) {
    Conv r;
    const std::size_t n = g.depth.size();
    long over = 0, noisy = 0;
    double sumZ = 0, sg = 0, sc = 0, vg = 0, vc = 0, sumNoisy = 0;
    for (std::size_t p = 0; p < n; ++p) {
        const double a = lumAt(g.radiance, p), b = lumAt(c.radiance, p), v = double(g.variance[p]) + double(c.variance[p]);
        r.nonfiniteGpu += !std::isfinite(a) || !std::isfinite(g.variance[p]);
        r.nonfiniteCpu += !std::isfinite(b) || !std::isfinite(c.variance[p]);
        r.zeroVar += !(v > 0);
        if (v > 0 && std::isfinite(v)) { sumNoisy += std::fabs(a - b) / std::sqrt(v); ++noisy; }
        const double z = v > 0 ? std::fabs(a - b) / std::sqrt(v) : (a == b ? 0.0 : 1e9);
        sumZ += std::min(z, 1e9); over += z > 4.0;
        sg += a; sc += b; vg += g.variance[p]; vc += c.variance[p];
    }
    r.meanAbsZ = sumZ / double(n); r.fracOver4 = double(over) / double(n);
    r.meanAbsZNoisy = noisy ? sumNoisy / double(noisy) : 0.0;
    r.imageZ = std::fabs(sg - sc) / std::sqrt(vg + vc);
    r.ok = r.meanAbsZ <= 0.9 && r.fracOver4 <= 0.002 && r.imageZ <= 3.0;
    return r;
}
std::string convJson(const char* scene, const char* what, const Conv& v) {
    char b[500];
    std::snprintf(b, sizeof b, "{\"check\": \"P2\", \"scene\": \"%s\", \"case\": \"%s\", \"mean_abs_z\": %.4f, \"fraction_z_over_4\": %.5f, \"image_mean_z\": %.3f, "
                  "\"reported_mean_abs_z_noisy_pixels\": %.4f, \"zero_variance_pixels\": %ld, \"nonfinite_gpu\": %ld, \"nonfinite_cpu\": %ld, \"within\": %s}",
                  scene, what, v.meanAbsZ, v.fracOver4, v.imageZ, v.meanAbsZNoisy, v.zeroVar, v.nonfiniteGpu, v.nonfiniteCpu, v.ok ? "true" : "false");
    return b;
}
rt::PathTraceDesc descFor(const swr::Scene& s, int w, int h, std::uint32_t spp, std::uint64_t seed) {
    rt::PathTraceDesc d;
    d.width = w; d.height = h; d.spp = spp; d.seed = seed;
    d.camera = d.previous = rt::cameraOf(s);
    return d;
}
// P4: the pixel-centre AOVs against the R1 rasterizer's visibility buffer and resolve.
bool nearEdge(const swr::Setup& s, int src, float px, float py) {
    if (src < 0) return false;
    for (int k = 0; k < swr::kSlotsPerTri; ++k) {
        const std::size_t slot = std::size_t(src) * swr::kSlotsPerTri + std::size_t(k);
        if (s.ints[slot * swr::kSetupInts + 6] == 0) continue;
        const float* q = &s.screen[slot * 6];
        for (int e = 0; e < 3; ++e) {
            const float ax = q[2 * e], ay = q[2 * e + 1], bx = q[2 * ((e + 1) % 3)], by = q[2 * ((e + 1) % 3) + 1];
            const float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
            const float t = std::clamp(l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0.0f, 0.0f, 1.0f);
            if (std::hypot(ax + t * dx - px, ay + t * dy - py) <= 1.0f) return true;
        }
    }
    return false;
}
struct AovCounts {
    long covered{0}, triDiff{0}, edgeExempt{0}, depthOver{0}, normalOver{0}, albedoDiff{0}, texelExempt{0};
    double worstDepth{0}, worstNormal{0};
    // Diagnosis (reported): on the pixels that fail, which side matches a float64 ray-triangle answer.
    long albedoPtAnalytic{0}, albedoR1Analytic{0}, depthPtAnalytic{0}, depthR1Analytic{0};
};
// The float64 hit of the pixel-centre ray on triangle t: x distance along the ray, y z barycentrics.
struct Exact { double t, u, v; };
Exact exactHit(const Tri& tr, Vec3 eye, Vec3 dir) {
    const double ax = tr.a.x, ay = tr.a.y, az = tr.a.z;
    const double e1[3] = {tr.b.x - ax, tr.b.y - ay, tr.b.z - az}, e2[3] = {tr.c.x - ax, tr.c.y - ay, tr.c.z - az}, d[3] = {dir.x, dir.y, dir.z};
    const double p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2], tv[3] = {eye.x - ax, eye.y - ay, eye.z - az};
    const double u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) / det;
    const double q[3] = {tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0]};
    return {(e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det, u, (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det};
}
// One pixel of P4 where both sides see triangle t: depth, normal and albedo.
void aovPixel(AovCounts& k, const rt::PtScene& ps, const swr::Scene& sc, const swr::Visibility& v, const swr::Resolved& r,
              const rt::PathTraceOutput& g, const std::vector<Tri>& tris, std::size_t p, int t, Vec3 dir, Vec3 fwd) {
    const Exact ex = exactHit(tris[std::size_t(t)], sc.eye, dir);
    const double n = sc.nearZ, f = sc.farZ;
    const double vd = 2.0 * f * n / ((f + n) - double(v.depth[p]) * (f - n));
    const double rel = std::fabs(vd - g.depth[p]) / vd;
    k.worstDepth = std::max(k.worstDepth, rel); k.depthOver += rel > 1e-4;
    if (rel > 1e-4) {
        const double ed = ex.t * double(dot(dir, fwd));
        k.depthPtAnalytic += std::fabs(g.depth[p] - ed) / ed <= 1e-5;
        k.depthR1Analytic += std::fabs(vd - ed) / ed <= 1e-5;
    }
    const Tri& tr = tris[std::size_t(t)];
    Vec3 ng = normalize(cross(tr.b - tr.a, tr.c - tr.a));
    if (dot(ng, dir) > 0.0f) ng = ng * -1.0f;
    Vec3 rn{r.normal[p * 3], r.normal[p * 3 + 1], r.normal[p * 3 + 2]};
    if (dot(rn, ng) < 0.0f) rn = rn * -1.0f;
    const Vec3 gn = normalize(Vec3{g.normal[p * 3], g.normal[p * 3 + 1], g.normal[p * 3 + 2]});
    const double dn = 1.0 - double(dot(rn, gn));
    k.worstNormal = std::max(k.worstNormal, dn); k.normalOver += dn > 1e-5;
    const std::uint32_t tex = r.texel[p] >> 24, tx = r.texel[p] & 4095u, ty = (r.texel[p] >> 12) & 4095u;
    const swr::TextureSet::Entry& e = sc.tex.entries[tex];
    const std::uint32_t col = sc.tex.texels[e.offset + ty * e.width + tx];
    const float m = ps.materials[tex].baseFactor / 255.0f;
    const float ra[3] = {float(col & 255u) * m, float((col >> 8) & 255u) * m, float((col >> 16) & 255u) * m};
    bool same = true;
    for (int q = 0; q < 3; ++q) same = same && ra[q] == g.albedo[p * 3 + std::size_t(q)];
    const auto nearInt = [](float x) { return std::fabs(x - std::floor(x + 0.5f)) < 1e-3f; };
    if (!same) {
        if (nearInt(r.texelCoord[p * 2]) || nearInt(r.texelCoord[p * 2 + 1])) { ++k.texelExempt; return; }
        ++k.albedoDiff;
        const swr::Geometry& G = ps.geo;
        const std::uint32_t i0 = G.idx[std::size_t(t) * 3], i1 = G.idx[std::size_t(t) * 3 + 1], i2 = G.idx[std::size_t(t) * 3 + 2];
        const double w0 = 1.0 - ex.u - ex.v;
        const double eu = G.uv[i0].x * w0 + G.uv[i1].x * ex.u + G.uv[i2].x * ex.v, ev = G.uv[i0].y * w0 + G.uv[i1].y * ex.u + G.uv[i2].y * ex.v;
        const std::uint32_t ex_ = std::uint32_t(std::int64_t(std::floor(eu * e.width))) & (e.width - 1), ey = std::uint32_t(std::int64_t(std::floor(ev * e.height))) & (e.height - 1);
        const std::uint32_t ec = sc.tex.texels[e.offset + ey * e.width + ex_];
        const float ea[3] = {float(ec & 255u) * m, float((ec >> 8) & 255u) * m, float((ec >> 16) & 255u) * m};
        bool pt = true, r1 = true;
        for (int q = 0; q < 3; ++q) { pt = pt && ea[q] == g.albedo[p * 3 + std::size_t(q)]; r1 = r1 && ea[q] == ra[q]; }
        k.albedoPtAnalytic += pt; k.albedoR1Analytic += r1;
    }
}
std::string aovs(Ctx& c, const rt::PtScene& ps, const swr::Scene& base, bool& ok) {
    const int W = 256, H = 256;
    swr::Scene sc = base;
    sc.geo = ps.geo; sc.tex = ps.tex;
    const swr::Options o;
    const swr::Setup st = swr::setup(sc.geo, sc.viewProj(W, H), W, H, o);
    const swr::Visibility v = swr::rasterize(st, o);
    const swr::Resolved r = swr::resolve(st, v, sc.geo, sc.tex, sc.light, o);
    rt::PathTraceDesc d = descFor(base, W, H, 1, 7);
    d.jitter = false;
    const rt::PathTraceOutput g = pathTraceGpu(c.dev, ps, d);
    if (!g.error.empty()) { c.R.error = g.error; ok = false; return ""; }
    const std::vector<Tri> tris = rt::trianglesOf(ps.geo);
    const Vec3 fwd = normalize(base.target - base.eye), side = normalize(cross(fwd, base.up)), up = cross(side, fwd);
    const float th = std::tan(base.fovy * 0.5f);
    AovCounts k;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::size_t p = std::size_t(y * W + x);
            const int rt = v.slot[p] ? int((v.slot[p] - 1) / swr::kSlotsPerTri) : -1, gt = g.triangle[p];
            if (rt >= 0) ++k.covered;
            if (rt != gt) {
                if (nearEdge(st, rt, float(x) + 0.5f, float(y) + 0.5f) || nearEdge(st, gt, float(x) + 0.5f, float(y) + 0.5f)) ++k.edgeExempt;
                else ++k.triDiff;
                continue;
            }
            if (rt < 0) continue;
            const float nx = (float(x) + 0.5f) / float(W) * 2.0f - 1.0f, ny = 1.0f - (float(y) + 0.5f) / float(H) * 2.0f;
            aovPixel(k, ps, sc, v, r, g, tris, p, rt, normalize(fwd + side * (nx * th * float(W) / float(H)) + up * (ny * th)), fwd);
        }
    ok = k.triDiff == 0 && k.depthOver == 0 && k.normalOver == 0 && k.albedoDiff == 0 && k.covered > 10000;
    char b[900];
    std::snprintf(b, sizeof b, "{\"check\": \"P4\", \"scene\": \"%s\", \"covered\": %ld, \"triangle_differences\": %ld, \"edge_exempt\": %ld, "
                  "\"depth_over_1e-4\": %ld, \"worst_depth_rel\": %.3e, \"normal_over_1e-5\": %ld, \"worst_normal_1_minus_dot\": %.3e, "
                  "\"albedo_differences\": %ld, \"texel_boundary_exempt\": %ld, \"diagnosis_albedo_failures_matching_float64\": {\"path_tracer\": %ld, \"r1\": %ld}, "
                  "\"diagnosis_depth_failures_matching_float64\": {\"path_tracer\": %ld, \"r1\": %ld}, \"pass\": %s}",
                  ps.name.c_str(), k.covered, k.triDiff, k.edgeExempt, k.depthOver, k.worstDepth, k.normalOver, k.worstNormal, k.albedoDiff,
                  k.texelExempt, k.albedoPtAnalytic, k.albedoR1Analytic, k.depthPtAnalytic, k.depthR1Analytic, ok ? "true" : "false");
    return b;
}
// P2 for one scene, with the MIS control on scenes that have emitters.
void convergence(Ctx& c, const rt::PtScene& ps, const swr::Scene& base) {
    const rt::PathTraceOutput g = pathTraceGpu(c.dev, ps, descFor(base, c.w, c.h, c.spp, 11));
    if (!g.error.empty()) { c.R.error = g.error; return; }
    const rt::PathTraceOutput cpu = rt::pathTraceCpu(ps, descFor(base, c.w, c.h, c.spp, 9001));
    const Conv v = compare(g, cpu);
    record(c, convJson(ps.name.c_str(), "gpu against cpu", v), v.ok);
    if (ps.emitters.empty()) return;
    rt::PathTraceDesc dc = descFor(base, c.w, c.h, c.spp, 11);
    dc.controls.misWeightOne = true;
    const rt::PathTraceOutput gc = pathTraceGpu(c.dev, ps, dc);
    if (!gc.error.empty()) { c.R.error = gc.error; return; }
    const Conv vc = compare(gc, cpu);
    record(c, convJson(ps.name.c_str(), "control: MIS weight 1", vc), !vc.ok);   // the control must fail
}
// P3: the same seed twice is bit-identical; seed + 1 changes nearly every pixel.
void determinism(Ctx& c, const rt::PtScene& ps, const swr::Scene& base) {
    const rt::PathTraceDesc d3 = descFor(base, 64, 48, 16, 5);
    rt::PathTraceDesc d4 = d3;
    d4.seed = 6;
    const rt::PathTraceOutput a = pathTraceGpu(c.dev, ps, d3), b = pathTraceGpu(c.dev, ps, d3), e = pathTraceGpu(c.dev, ps, d4);
    if (!a.error.empty() || !b.error.empty() || !e.error.empty()) { c.R.error = a.error + b.error + e.error; return; }
    const auto eq = [](const std::vector<float>& x, const std::vector<float>& y) { return std::memcmp(x.data(), y.data(), x.size() * 4) == 0; };
    const bool same = eq(a.radiance, b.radiance) && eq(a.albedo, b.albedo) && eq(a.variance, b.variance) && eq(a.depth, b.depth) &&
                      eq(a.normal, b.normal) && eq(a.motion, b.motion) && a.triangle == b.triangle;
    long changed = 0;
    for (std::size_t p = 0; p < a.depth.size(); ++p) changed += std::memcmp(&a.radiance[p * 3], &e.radiance[p * 3], 12) != 0;
    const double frac = double(changed) / double(a.depth.size());
    char b3[200];
    std::snprintf(b3, sizeof b3, "{\"check\": \"P3\", \"same_seed_identical\": %s, \"seed_plus_1_pixels_changed\": %.4f, \"pass\": %s}",
                  same ? "true" : "false", frac, same && frac >= 0.99 ? "true" : "false");
    record(c, b3, same && frac >= 0.99);
}
}  // namespace

std::string RtPtParity::json() const {
    return "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R2, the GPU path tracer: P1 to P4 (evidence/rt-r2-bounds.json)\",\n"
           " \"interface_version\": " + std::to_string(rt::kPathTraceVersion) + ",\n \"backend\": \"" + backend + "\",\n \"adapter\": \"" + adapter +
           "\",\n \"error\": \"" + error + "\",\n \"pass\": " + (pass() ? "true" : "false") + ",\n \"results\": [\n" + results + "\n ]\n}\n";
}

RtPtParity rtPtParity(rhi::Device& dev, bool quick) {
    RtPtParity R;
    R.backend = dev.backendName(); R.adapter = dev.adapter().description;
    Ctx c{dev, R};
    if (quick) { c.w = 64; c.h = 48; c.spp = 128; }
    std::string line;
    for (float a : {0.5f, 0.8f}) { const bool ok = furnace(c, a, false, line); record(c, line, ok); }
    { const bool ok = furnace(c, 0.5f, true, line); record(c, line, !ok); }        // the control must fail
    if (!R.error.empty()) return R;
    const swr::Scene room = swr::retroRoom(), street = swr::isoStreet();
    const rt::PtScene pr = rt::ptRetroRoom(), pst = rt::ptIsoStreet();
    convergence(c, pr, room);
    if (R.error.empty()) convergence(c, pst, street);
    if (R.error.empty()) determinism(c, pr, room);
    for (const auto& [ps, base] : {std::pair<const rt::PtScene*, const swr::Scene*>{&pr, &room}, {&pst, &street}}) {
        if (!R.error.empty()) return R;
        bool ok = false;
        const std::string s = aovs(c, *ps, *base, ok);
        if (R.error.empty()) record(c, s, ok);
    }
    R.allPass = c.all && R.error.empty();
    return R;
}

}  // namespace raw::gpu_check
