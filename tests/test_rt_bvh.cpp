// RT stage R2, the BVH's CPU half (evidence/rt-r2-bounds.json): the PLOC tree is well formed,
// its CPU traversal returns exactly the brute-force scan's answer on 100,000 rays a scene (B2),
// and its SAH cost against the M3 binned-SAH build (B3, reported here; gated on the GPU tree).
//   test_rt_bvh [out.json]
#include "raw/renderer/rt_bvh.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/core/parallel.hpp"
#include <thread>
#include "check.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
using namespace raw;

namespace {
std::string js;
bool full = false;   // with an output path: 100,000 rays on every scene; ctest uses 10,000 on dense_block
struct Rng {
    std::uint64_t s;
    float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return float(double(s >> 11) * (1.0 / 9007199254740992.0)); }
};
bool scan(const std::vector<Tri>& tris, const Ray& r, float tMax, Hit& h) {
    bool any = false;
    float best = tMax;
    for (std::size_t i = 0; i < tris.size(); ++i) {
        float t, u, v;
        if (intersectTri(r, tris[i], t, u, v) && t < best) { best = t; h = {t, u, v, int(i)}; any = true; }
    }
    return any;
}
// Every triangle in exactly one leaf; every internal box contains its children's boxes.
bool wellFormed(const rt::Tree& t, std::size_t n, int& depth) {
    std::vector<int> seen(n, 0);
    std::vector<std::pair<int, int>> st{{t.root, 1}};
    depth = 0;
    while (!st.empty()) {
        const auto [id, d] = st.back();
        st.pop_back();
        depth = std::max(depth, d);
        const rt::Node& x = t.nodes[std::size_t(id)];
        if (x.tri >= 0) { ++seen[std::size_t(x.tri)]; continue; }
        for (int c : {x.left, x.right}) {
            const rt::Node& y = t.nodes[std::size_t(c)];
            for (int k = 0; k < 3; ++k) if (y.lo[k] < x.lo[k] || y.hi[k] > x.hi[k]) return false;
            st.push_back({c, d + 1});
        }
    }
    for (int s : seen) if (s != 1) return false;
    return true;
}
void scene(const char* name, const std::vector<Tri>& tris, const swr::Scene* cam) {
    const auto t0 = std::chrono::steady_clock::now();
    const rt::Tree tree = rt::buildPloc(tris);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    int depth = 0;
    const bool ok = wellFormed(tree, tris.size(), depth);
    std::size_t m3Nodes = 0;
    const double sah = rt::sahCost(tree), m3 = rt::binnedSahCost(tris, &m3Nodes);
    Bvh m3bvh;
    m3bvh.build(tris);
    Rng r{0x5eedULL + tris.size()};
    const int rays = (!full && tris.size() > 20000) ? 10000 : 100000;
    std::vector<Ray> rs(static_cast<std::size_t>(rays) + 0);
    for (int k = 0; k < rays; ++k) {
        if (cam && k % 2) {
            const Vec3 f = normalize(cam->target - cam->eye), s = normalize(cross(f, cam->up)), u = cross(s, f);
            rs[std::size_t(k)] = {cam->eye, normalize(f + s * (r.next() - 0.5f) + u * (r.next() - 0.5f))};
        } else {
            rs[std::size_t(k)] = {{24 * r.next() - 12, 24 * r.next() - 12, 24 * r.next() - 12}, normalize(Vec3{r.next() - 0.5f, r.next() - 0.5f, r.next() - 0.5f})};
        }
    }
    std::vector<char> differ(std::size_t(rays), 0), hit(std::size_t(rays), 0);
    parallelRows(rays, int(std::max(1u, std::thread::hardware_concurrency())), [&](int k) {
        Hit a, b;
        const Ray& ray = rs[std::size_t(k)];
        const bool ha = rt::closest(tree, tris, ray, 1e30f, a), hb = scan(tris, ray, 1e30f, b);
        hit[std::size_t(k)] = hb;
        differ[std::size_t(k)] = ha != hb || (ha && (a.t != b.t || a.tri != b.tri));
    });
    long bad = 0, hits = 0;
    for (int k = 0; k < rays; ++k) { bad += differ[std::size_t(k)]; hits += hit[std::size_t(k)]; }
    std::printf("B %-12s %7zu tris: PLOC %.0f ms, %u rounds, depth %d, well formed %d | traversal %d rays, %ld hits, %ld differ | SAH %.2f vs M3 %.2f ratio %.3f (port nodes %zu, Bvh nodes %zu)\n",
                name, tris.size(), ms, tree.rounds, depth, ok, rays, hits, bad, sah, m3, sah / m3, m3Nodes, m3bvh.nodeCount());
    CHECK(ok);
    CHECK(bad == 0);
    CHECK(hits > rays / 20);
    CHECK(m3Nodes == m3bvh.nodeCount());
    char b[600];
    std::snprintf(b, sizeof b, "{\"scene\": \"%s\", \"triangles\": %zu, \"cpu_build_ms\": %.1f, \"rounds\": %u, \"depth\": %d, \"well_formed\": %s, "
                  "\"rays\": %d, \"hits\": %ld, \"traversal_differences\": %ld, \"sah\": %.4f, \"m3_binned_sah\": %.4f, \"sah_ratio\": %.4f, "
                  "\"port_node_count\": %zu, \"bvh_node_count\": %zu}",
                  name, tris.size(), ms, tree.rounds, depth, ok ? "true" : "false", rays, hits, bad, sah, m3, sah / m3, m3Nodes, m3bvh.nodeCount());
    js += (js.empty() ? "  " : ",\n  ") + std::string(b);
}
}  // namespace

int main(int argc, char** argv) {
    full = argc > 1;
    for (const swr::Scene& s : swr::ownedScenes()) scene(s.name.c_str(), rt::trianglesOf(s.geo), &s);
    const swr::Scene dense = rt::denseBlock();
    scene("dense_block", rt::trianglesOf(dense.geo), &dense);
    scene("soup", rt::soup(20000, 7), nullptr);
    if (argc > 1)
        std::ofstream(argv[1]) << "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT stage R2, the BVH's CPU half (B2 CPU traversal, B3 reported)\",\n"
                               << " \"pass\": " << (raw_test_failures() == 0 ? "true" : "false") << ",\n \"results\": [\n" << js << "\n ]\n}\n";
    return raw_test_summary();
}
