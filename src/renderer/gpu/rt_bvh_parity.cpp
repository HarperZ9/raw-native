// The compute BVH against the CPU reference: checks B1 to B3 of evidence/rt-r2-bounds.json.
#include "raw/renderer/rt_gpu.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
namespace raw::gpu_check {
namespace {
struct Rng {
    std::uint64_t s;
    float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return float(double(s >> 11) * (1.0 / 9007199254740992.0)); }
};
struct Scan { float t1{-1}, t2{-1}; int tri{-1}; };   // nearest and second-nearest hit of a full scan
std::vector<Scan> bruteForce(const std::vector<Tri>& tris, const std::vector<Ray>& rays) {
    std::vector<Scan> out(rays.size());
    parallelRows(int(rays.size()), int(std::max(1u, std::thread::hardware_concurrency())), [&](int k) {
        Scan s;
        float best = 1e30f, second = 1e30f;
        for (std::size_t i = 0; i < tris.size(); ++i) {
            float t, u, v;
            if (!intersectTri(rays[std::size_t(k)], tris[i], t, u, v)) continue;
            if (t < best) { second = best; best = t; s.tri = int(i); }
            else if (t < second) second = t;
        }
        if (s.tri >= 0) { s.t1 = best; s.t2 = second; }
        out[std::size_t(k)] = s;
    });
    return out;
}
std::vector<Ray> makeRays(const swr::Scene* cam, std::size_t n, std::uint64_t seed) {
    Rng r{seed};
    std::vector<Ray> rays(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (cam && k % 2) {
            const Vec3 f = normalize(cam->target - cam->eye), s = normalize(cross(f, cam->up)), u = cross(s, f);
            rays[k] = {cam->eye, normalize(f + s * (r.next() - 0.5f) + u * (r.next() - 0.5f))};
        } else {
            rays[k] = {{24 * r.next() - 12, 24 * r.next() - 12, 24 * r.next() - 12}, normalize(Vec3{r.next() - 0.5f, r.next() - 0.5f, r.next() - 0.5f})};
        }
    }
    return rays;
}
std::uint32_t bits(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
// FNV-1a over every node's fields, for comparing trees across runs and processes (diagnosis).
std::uint64_t treeHash(const rt::Tree& t) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint32_t v) { for (int i = 0; i < 4; ++i) { h ^= (v >> (8 * i)) & 255u; h *= 1099511628211ull; } };
    for (const rt::Node& x : t.nodes) {
        mix(std::uint32_t(x.left)); mix(std::uint32_t(x.right)); mix(std::uint32_t(x.tri));
        for (int k = 0; k < 3; ++k) { mix(bits(x.lo[k])); mix(bits(x.hi[k])); }
    }
    return h;
}
long treeDiff(const rt::Tree& a, const rt::Tree& b) {
    if (a.nodes.size() != b.nodes.size() || a.root != b.root) return long(std::max(a.nodes.size(), b.nodes.size())) + 1;
    long d = 0;
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const rt::Node &x = a.nodes[i], &y = b.nodes[i];
        bool same = x.left == y.left && x.right == y.right && x.tri == y.tri;
        for (int k = 0; k < 3; ++k) same = same && bits(x.lo[k]) == bits(y.lo[k]) && bits(x.hi[k]) == bits(y.hi[k]);
        d += !same;
    }
    return d;
}
struct Case {
    std::string name;
    std::size_t tris{0}, rays{0};
    long nodeDiff{0}, cpuOfGpuTreeDiff{0}, gpuTriDiff{0}, gpuTieExempt{0}, gpuTOver{0}, anyDiff{0}, overflows{0}, hits{0};
    double sahRatio{0}, gpuMs{0};
    std::uint64_t cpuHash{0}, gpuHash{0};
    long leafOrderDiff{0}, firstDiffNode{-1};   // diagnosis: the sort stage, and where the clustering first departs
    std::uint32_t rounds{0};
    bool pass{false};
};
Case runScene(rhi::Device& dev, const char* name, const std::vector<Tri>& tris, const swr::Scene* cam, std::size_t nRays, std::string& err) {
    Case c;
    c.name = name; c.tris = tris.size(); c.rays = nRays;
    const rt::Tree ref = rt::buildPloc(tris);
    const RtBuild g = buildPlocGpu(dev, tris);
    if (!g.error.empty()) { err = g.error; return c; }
    c.nodeDiff = treeDiff(ref, g.tree); c.gpuMs = g.ms; c.rounds = g.tree.rounds;
    c.cpuHash = treeHash(ref); c.gpuHash = treeHash(g.tree);
    for (std::size_t i = 0; i < ref.nodes.size() && i < g.tree.nodes.size(); ++i) {
        const rt::Node &x = ref.nodes[i], &y = g.tree.nodes[i];
        if (i < tris.size() && x.tri != y.tri) ++c.leafOrderDiff;
        bool same = x.left == y.left && x.right == y.right && x.tri == y.tri;
        for (int k = 0; k < 3; ++k) same = same && bits(x.lo[k]) == bits(y.lo[k]) && bits(x.hi[k]) == bits(y.hi[k]);
        if (!same && c.firstDiffNode < 0) c.firstDiffNode = long(i);
    }
    c.sahRatio = rt::sahCost(g.tree) / rt::binnedSahCost(tris);
    const std::vector<Ray> rays = makeRays(cam, nRays, 0xb2b2ULL + tris.size());
    const std::vector<Scan> ref1 = bruteForce(tris, rays);
    std::vector<float> far(rays.size(), 1e30f), anyMax(rays.size());
    Rng r{77};
    for (float& x : anyMax) x = 8.0f * r.next();
    const RtTrace ct = traceGpu(dev, g.tree, tris, rays, far, false, robustFarScale());
    const RtTrace at = traceGpu(dev, g.tree, tris, rays, anyMax, true, robustFarScale());
    if (!ct.error.empty() || !at.error.empty()) { err = !ct.error.empty() ? ct.error : at.error; return c; }
    c.overflows = ct.overflows + at.overflows;
    for (std::size_t k = 0; k < rays.size(); ++k) {
        const Scan& s = ref1[k];
        c.hits += s.tri >= 0;
        Hit h;
        const bool hc = rt::closest(g.tree, tris, rays[k], 1e30f, h);
        if (hc != (s.tri >= 0) || (hc && (h.tri != s.tri || h.t != s.t1))) ++c.cpuOfGpuTreeDiff;
        if (ct.tri[k] != s.tri) {
            if (s.tri >= 0 && ct.tri[k] >= 0 && s.t2 > 0 && s.t2 - s.t1 <= 1e-5f * s.t1) ++c.gpuTieExempt;
            else ++c.gpuTriDiff;
        } else if (s.tri >= 0 && std::fabs(ct.t[k] - s.t1) > 1e-5f * s.t1) {
            ++c.gpuTOver;
        }
        const bool anyRef = s.tri >= 0 && s.t1 < anyMax[k];
        if ((at.tri[k] >= 0) != anyRef) ++c.anyDiff;
    }
    c.pass = c.nodeDiff == 0 && c.cpuOfGpuTreeDiff == 0 && c.gpuTriDiff == 0 && c.gpuTOver == 0 && c.anyDiff == 0 &&
             c.overflows == 0 && c.sahRatio <= 1.25;
    return c;
}
}  // namespace

std::string RtBvhParity::json() const {
    return "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R2, the compute BVH: B1 to B3 (evidence/rt-r2-bounds.json)\",\n"
           " \"backend\": \"" + backend + "\",\n \"adapter\": \"" + adapter + "\",\n \"error\": \"" + error + "\",\n"
           " \"quick\": " + std::string(quick ? "true" : "false") + ",\n \"control_radius_8_nodes_differing\": " + std::to_string(controlRadiusDiffNodes) + ",\n"
           " \"control_short_far_hits_missed\": " + std::to_string(controlShortFarMissed) + ",\n"
           " \"pass\": " + (pass() ? "true" : "false") + ",\n \"scenes\": [\n" + results + "\n ]\n}\n";
}

RtBvhParity rtBvhParity(rhi::Device& dev, bool quick) {
    RtBvhParity R;
    R.quick = quick;
    const std::size_t nRays = quick ? 20000 : 100000;
    R.backend = dev.backendName(); R.adapter = dev.adapter().description;
    std::vector<swr::Scene> scenes = swr::ownedScenes();
    scenes.push_back(rt::denseBlock());
    bool all = true;
    const auto record = [&](const Case& c) {
        char b[1000];
        std::snprintf(b, sizeof b, "  {\"scene\": \"%s\", \"triangles\": %zu, \"gpu_build_ms_with_transfers\": %.1f, \"rounds\": %u, \"nodes_differing\": %ld, "
                      "\"cpu_traversal_of_gpu_tree_differences\": %ld, \"rays\": %zu, \"hits\": %ld, \"gpu_triangle_differences\": %ld, "
                      "\"gpu_tie_exempt\": %ld, \"gpu_t_over_bound\": %ld, \"any_hit_differences\": %ld, \"stack_overflows\": %ld, \"sah_ratio\": %.4f, \"cpu_tree_hash\": \"%016llx\", \"gpu_tree_hash\": \"%016llx\", \"leaf_order_differences\": %ld, \"first_differing_node\": %ld, \"pass\": %s}",
                      c.name.c_str(), c.tris, c.gpuMs, c.rounds, c.nodeDiff, c.cpuOfGpuTreeDiff, c.rays, c.hits, c.gpuTriDiff, c.gpuTieExempt, c.gpuTOver,
                      c.anyDiff, c.overflows, c.sahRatio, (unsigned long long)c.cpuHash, (unsigned long long)c.gpuHash, c.leafOrderDiff, c.firstDiffNode, c.pass ? "true" : "false");
        R.results += (R.results.empty() ? "" : ",\n") + std::string(b);
        all = all && c.pass;
    };
    for (const swr::Scene& s : scenes) {
        const Case c = runScene(dev, s.name.c_str(), rt::trianglesOf(s.geo), &s, nRays, R.error);
        if (!R.error.empty()) return R;
        record(c);
    }
    const Case soupCase = runScene(dev, "soup", rt::soup(20000, 7), nullptr, nRays, R.error);
    if (!R.error.empty()) return R;
    record(soupCase);
    R.allPass = all;
    // Controls: radius 8 on dense_block; the slab exit shortened by 10% on retro_room.
    const std::vector<Tri> dense = rt::trianglesOf(scenes[3].geo);
    const RtBuild r8 = buildPlocGpu(dev, dense, 8);
    if (!r8.error.empty()) { R.error = r8.error; return R; }
    R.controlRadiusDiffNodes = treeDiff(rt::buildPloc(dense), r8.tree);
    const std::vector<Tri> room = rt::trianglesOf(scenes[0].geo);
    const rt::Tree t = rt::buildPloc(room);
    const std::vector<Ray> rays = makeRays(&scenes[0], 100000, 0xb2b2ULL + room.size());
    const RtTrace full = traceGpu(dev, t, room, rays, std::vector<float>(rays.size(), 1e30f), false, robustFarScale());
    const RtTrace shortFar = traceGpu(dev, t, room, rays, std::vector<float>(rays.size(), 1e30f), false, 0.9f);
    if (!full.error.empty() || !shortFar.error.empty()) { R.error = full.error + shortFar.error; return R; }
    for (std::size_t k = 0; k < rays.size(); ++k) R.controlShortFarMissed += full.tri[k] >= 0 && shortFar.tri[k] != full.tri[k];
    return R;
}

}  // namespace raw::gpu_check
