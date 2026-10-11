// H1 and H2 of evidence/rt-r2-bounds.json: the hybrid rays on the GPU against CPU rays from the
// CPU rasterizer's own positions (addendum 2).
#include "raw/renderer/rt_hybrid.hpp"
#include "json_text.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
namespace raw::gpu_check {
namespace {
struct Px { bool covered{false}; Vec3 pos, ng, n, d; };
// The CPU's G-buffer pixel: swr_gbuffer's arithmetic on the CPU visibility buffer.
Px cpuPixel(const swr::Setup& s, const swr::Visibility& v, const swr::Geometry& g, const std::vector<Tri>& tris, Vec3 eye, int x, int y) {
    Px r;
    const std::uint32_t slot1 = v.slot[std::size_t(y) * std::size_t(s.width) + std::size_t(x)];
    if (!slot1) return r;
    const std::int32_t* I = &s.ints[(slot1 - 1) * swr::kSetupInts];
    const float* F = &s.floats[(slot1 - 1) * swr::kSetupFloats];
    const std::int32_t px = x * 256 + 128, py = y * 256 + 128;
    const float e0 = swr::edgeToFloat(swr::edge(I[2], I[3], I[4], I[5], px, py)), e1 = swr::edgeToFloat(swr::edge(I[4], I[5], I[0], I[1], px, py));
    const float e2 = swr::edgeToFloat(swr::edge(I[0], I[1], I[2], I[3], px, py));
    const float q0 = e0 * F[6] * F[3], q1 = e1 * F[6] * F[4], q2 = e2 * F[6] * F[5], den = q0 + q1 + q2;
    const float l0 = q0 / den, l1 = q1 / den, l2 = q2 / den;
    const float b1 = l0 * F[7] + l1 * F[9] + l2 * F[11], b2 = l0 * F[8] + l1 * F[10] + l2 * F[12], b0 = (1.0f - b1) - b2;
    const std::size_t t = std::size_t(I[6] - 1);
    const Tri& T = tris[t];
    r.covered = true;
    r.pos = T.a * b0 + T.b * b1 + T.c * b2;
    r.ng = normalize(cross(T.b - T.a, T.c - T.a));
    r.n = normalize(g.nrm[g.idx[t * 3]] * b0 + g.nrm[g.idx[t * 3 + 1]] * b1 + g.nrm[g.idx[t * 3 + 2]] * b2);
    r.d = normalize(r.pos - eye);
    if (dot(r.ng, r.d) > 0.0f) r.ng = r.ng * -1.0f;
    if (dot(r.n, r.ng) < 0.0f) r.n = r.n * -1.0f;
    return r;
}
Vec3 offsetOf(const Px& p, Vec3 dir) {
    const float e = 2e-4f * (1.0f + std::max(std::fabs(p.pos.x), std::max(std::fabs(p.pos.y), std::fabs(p.pos.z))));
    return p.pos + p.ng * (dot(p.ng, dir) > 0.0f ? e : -e);
}
bool nearSlotEdge(const swr::Setup& s, std::uint32_t slot1, float px, float py) {
    if (!slot1) return false;
    const float* q = &s.screen[(slot1 - 1) * 6];
    for (int e = 0; e < 3; ++e) {
        const float ax = q[2 * e], ay = q[2 * e + 1], bx = q[2 * ((e + 1) % 3)], by = q[2 * ((e + 1) % 3) + 1];
        const float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
        const float t = std::clamp(l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0.0f, 0.0f, 1.0f);
        if (std::hypot(ax + t * dx - px, ay + t * dy - py) <= 1.0f) return true;
    }
    return false;
}
struct Ref { std::vector<char> covered, lit, grazing; std::vector<int> tri; std::vector<float> t1, t2; swr::Setup setup; swr::Visibility vis; };
Ref cpuReference(const rt::PtScene& ps, const swr::Scene& base, int w, int h) {
    Ref R;
    const swr::Options o;
    R.setup = swr::setup(ps.geo, base.viewProj(w, h), w, h, o);
    R.vis = swr::rasterize(R.setup, o);
    const std::vector<Tri> tris = rt::trianglesOf(ps.geo);
    const std::size_t n = std::size_t(w) * std::size_t(h);
    R.covered.assign(n, 0); R.lit.assign(n, 0); R.grazing.assign(n, 0); R.tri.assign(n, -1); R.t1.assign(n, -1); R.t2.assign(n, -1);
    const Vec3 l = normalize(ps.sunDir);
    parallelRows(h, int(std::max(1u, std::thread::hardware_concurrency())), [&](int y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
            const Px q = cpuPixel(R.setup, R.vis, ps.geo, tris, base.eye, x, y);
            if (!q.covered) continue;
            R.covered[p] = 1;
            R.grazing[p] = std::fabs(dot(q.ng, l)) < 1e-3f;
            R.lit[p] = !rt::occluded(ps.tree, tris, {offsetOf(q, l), l}, 1e30f);
            const Vec3 r = q.d - q.n * (2.0f * dot(q.d, q.n));
            const Ray ray{offsetOf(q, r), r};
            float best = 1e30f, second = 1e30f;
            for (std::size_t i = 0; i < tris.size(); ++i) {
                float t, u, v;
                if (!intersectTri(ray, tris[i], t, u, v)) continue;
                if (t < best) { second = best; best = t; R.tri[p] = int(i); } else if (t < second) second = t;
            }
            if (R.tri[p] >= 0) { R.t1[p] = best; R.t2[p] = second; }
        }
    });
    return R;
}
}  // namespace

namespace {
struct HyCounts { long covered{0}, shadowDiff{0}, shadowUnexplained{0}, reflAgree{0}, reflDiff{0}, reflUnexplained{0}, reflEdge{0}, reflTie{0}; };
HyCounts compareHybrid(const Ref& R, const HybridFrame& g, int w) {
    HyCounts k;
    for (std::size_t p = 0; p < R.covered.size(); ++p) {
        if (!R.covered[p]) continue;
        ++k.covered;
        if ((g.lit[p] > 0.5f) != bool(R.lit[p])) { ++k.shadowDiff; k.shadowUnexplained += !R.grazing[p]; }
        if (g.reflTri[p] == R.tri[p]) { ++k.reflAgree; continue; }
        ++k.reflDiff;
        const float px = float(p % std::size_t(w)) + 0.5f, py = float(p / std::size_t(w)) + 0.5f;
        if (nearSlotEdge(R.setup, R.vis.slot[p], px, py)) ++k.reflEdge;
        else if (R.tri[p] >= 0 && R.t2[p] > 0 && R.t2[p] - R.t1[p] <= 1e-5f * R.t1[p]) ++k.reflTie;
        else ++k.reflUnexplained;
    }
    return k;
}
bool h1Pass(const HyCounts& k) { return k.covered > 0 && double(k.shadowDiff) <= 0.001 * double(k.covered) && k.shadowUnexplained == 0; }
bool h2Pass(const HyCounts& k) { return k.covered > 0 && double(k.reflAgree) >= 0.999 * double(k.covered) && k.reflUnexplained == 0; }
std::string hyJson(const char* scene, const char* what, const HyCounts& k, bool h1, bool h2) {
    char b[700];
    std::snprintf(b, sizeof b, "  {\"scene\": \"%s\", \"case\": \"%s\", \"covered\": %ld, \"shadow_differences\": %ld, \"shadow_not_grazing\": %ld, \"h1_within\": %s, "
                  "\"reflection_agree\": %ld, \"reflection_differences\": %ld, \"reflection_edge_exempt\": %ld, \"reflection_tie_exempt\": %ld, "
                  "\"reflection_unexplained\": %ld, \"h2_within\": %s}",
                  scene, what, k.covered, k.shadowDiff, k.shadowUnexplained, h1 ? "true" : "false", k.reflAgree, k.reflDiff, k.reflEdge, k.reflTie,
                  k.reflUnexplained, h2 ? "true" : "false");
    return b;
}
}  // namespace

std::string RtHybridParity::json() const {
    return "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R2, hybrid ray-traced shadows and reflections over the R1 visibility buffer: H1, H2 (evidence/rt-r2-bounds.json)\",\n"
           " \"backend\": \"" + backend + "\",\n \"adapter\": \"" + jsonText(adapter) + "\",\n \"error\": \"" + jsonText(error) + "\",\n \"pass\": " + (pass() ? "true" : "false") +
           ",\n \"results\": [\n" + results + "\n ]\n}\n";
}

RtHybridParity rtHybridParity(rhi::Device& dev) {
    RtHybridParity P;
    P.backend = dev.backendName(); P.adapter = dev.adapter().description;
    const int W = 256, H = 256;
    bool all = true;
    const swr::Scene room = swr::retroRoom(), street = swr::isoStreet();
    const rt::PtScene pr = rt::ptRetroRoom(), pst = rt::ptIsoStreet();
    for (const auto& [ps, base] : {std::pair<const rt::PtScene*, const swr::Scene*>{&pr, &room}, {&pst, &street}}) {
        const Ref R = cpuReference(*ps, *base, W, H);
        const HybridFrame g = hybridGpu(dev, *ps, *base, W, H);
        if (!g.error.empty()) { P.error = g.error; return P; }
        const HyCounts k = compareHybrid(R, g, W);
        const bool h1 = h1Pass(k), h2 = h2Pass(k);
        all = all && h1 && h2;
        P.results += (P.results.empty() ? "" : ",\n") + hyJson(ps->name.c_str(), "hybrid against cpu", k, h1, h2);
        // Controls: no origin offset must break H1; a tilted normal must break H2 (addendum 2, H2 method note 1).
        const HybridFrame c1 = hybridGpu(dev, *ps, *base, W, H, kHybridNoOffset), c2 = hybridGpu(dev, *ps, *base, W, H, kHybridTiltNormal);
        if (!c1.error.empty() || !c2.error.empty()) { P.error = c1.error + c2.error; return P; }
        const HyCounts k1 = compareHybrid(R, c1, W), k2 = compareHybrid(R, c2, W);
        all = all && !h1Pass(k1) && !h2Pass(k2);
        P.results += ",\n" + hyJson(ps->name.c_str(), "control: no origin offset", k1, h1Pass(k1), h2Pass(k1));
        P.results += ",\n" + hyJson(ps->name.c_str(), "control: normal tilted 0.05 rad", k2, h1Pass(k2), h2Pass(k2));
    }
    P.allPass = all;
    return P;
}

}  // namespace raw::gpu_check
