// The BVH returns exactly what a scan of every triangle returns (raw/renderer/bvh.hpp):
// the same nearest triangle and distance, and the same occlusion answer, on 110,000
// seeded rays (10,000 through a 20,000-triangle soup, 100,000 through the built-in test scene).
#include "raw/renderer/bvh.hpp"
#include "raw/scene/scene.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace raw;

namespace {
struct Rng {
    std::uint64_t s;
    float next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return float(double(s >> 11) * (1.0 / 9007199254740992.0)); }
};
Vec3 rnd(Rng& r, float lo, float hi) { return {lo + (hi - lo) * r.next(), lo + (hi - lo) * r.next(), lo + (hi - lo) * r.next()}; }
bool scanClosest(const std::vector<Tri>& tris, const Ray& r, float tMax, Hit& h) {
    bool any = false;
    float best = tMax;
    for (std::size_t i = 0; i < tris.size(); ++i) {
        float t, u, v;
        if (intersectTri(r, tris[i], t, u, v) && t < best) { best = t; h = {t, u, v, int(i)}; any = true; }
    }
    return any;
}
int compare(const std::vector<Tri>& tris, int rays, std::uint64_t seed) {
    Bvh b;
    b.build(tris);
    Rng r{seed};
    int bad = 0;
    for (int k = 0; k < rays; ++k) {
        const Ray ray{rnd(r, -12.0f, 12.0f), normalize(rnd(r, -1.0f, 1.0f))};
        const float tMax = k % 3 ? 1e30f : 4.0f * r.next();
        Hit a, c;
        const bool ha = b.closest(ray, tMax, a), hc = scanClosest(tris, ray, tMax, c);
        if (ha != hc || (ha && (a.t != c.t || a.tri != c.tri))) ++bad;
        bool scanOcc = false;
        for (const Tri& t : tris) { float tt, u, v; if (intersectTri(ray, t, tt, u, v) && tt < tMax) { scanOcc = true; break; } }
        if (b.occluded(ray, tMax) != scanOcc) ++bad;
    }
    return bad;
}
}  // namespace

int main() {
    Rng r{7};
    std::vector<Tri> soup;
    for (int i = 0; i < 20000; ++i) {
        const Vec3 c = rnd(r, -10.0f, 10.0f);
        soup.push_back({c + rnd(r, -0.6f, 0.6f), c + rnd(r, -0.6f, 0.6f), c + rnd(r, -0.6f, 0.6f)});
    }
    const int badSoup = compare(soup, 10000, 11);
    std::vector<Tri> scene;
    const Scene s = buildTestScene(64, 64);
    for (const Mesh& m : s.meshes)
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
            scene.push_back({m.positions[std::size_t(m.indices[i])], m.positions[std::size_t(m.indices[i + 1])], m.positions[std::size_t(m.indices[i + 2])]});
    const int badScene = compare(scene, 100000, 13);
    std::printf("bvh: %d and %d disagreements with the scan (soup of %zu triangles, test scene of %zu)\n", badSoup, badScene, soup.size(), scene.size());
    CHECK(badSoup == 0);
    CHECK(badScene == 0);
    return raw_test_summary();
}
