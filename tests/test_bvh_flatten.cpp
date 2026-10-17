// The BVH as the GPU rtao pass reads it (Bvh::flatten; evidence/m3-gpu-ao-bvh-bounds.json, s4).
// A CPU walk of the flattened arrays, written as the WGSL walks them, must answer every
// occlusion query as the scan does; the deepest stack on raw-hero and raw-hall must fit 64.
#include "raw/renderer/bvh.hpp"
#include "raw/scene/scene.hpp"
#include "raw/tools/owned_assets.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace raw;
namespace {
std::vector<Tri> trianglesOf(const Scene& s){
    std::vector<Tri> t;
    for (const Mesh& m : s.meshes)
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
            t.push_back({m.positions[m.indices[i]], m.positions[m.indices[i + 1]], m.positions[m.indices[i + 2]]});
    return t;
}
float invDir(float x){ return x != 0.0f ? 1.0f / x : (x >= 0.0f ? 1e30f : -1e30f); }
// The WGSL bvhBlocked, with intersectTri standing in for the pass's hit() (the same test).
bool flatBlocked(const std::vector<float>& N, const std::vector<float>& P, const Ray& r, float maxDist, int& deepest){
    const float ix = invDir(r.d.x), iy = invDir(r.d.y), iz = invDir(r.d.z);
    uint32_t stack[64]; uint32_t sp = 1; stack[0] = 0;
    while (sp){
        const uint32_t n = stack[--sp];
        const float a0 = (N[n * 8] - r.o.x) * ix, b0 = (N[n * 8 + 4] - r.o.x) * ix;
        const float a1 = (N[n * 8 + 1] - r.o.y) * iy, b1 = (N[n * 8 + 5] - r.o.y) * iy;
        const float a2 = (N[n * 8 + 2] - r.o.z) * iz, b2 = (N[n * 8 + 6] - r.o.z) * iz;
        const float t0 = std::fmax(std::fmax(std::fmax(0.0f, std::fmin(a0, b0)), std::fmin(a1, b1)), std::fmin(a2, b2));
        const float t1 = std::fmin(std::fmin(std::fmin(maxDist, std::fmax(a0, b0)), std::fmax(a1, b1)), std::fmax(a2, b2));
        if (!(t0 <= t1)) continue;
        const uint32_t first = uint32_t(N[n * 8 + 3]), count = uint32_t(N[n * 8 + 7]);
        if (count){
            for (uint32_t k = 0; k < count; ++k){
                const float* q = P.data() + 9ull * (first + k);
                const Tri t{{q[0], q[1], q[2]}, {q[3], q[4], q[5]}, {q[6], q[7], q[8]}};
                float tt, u, v;
                if (intersectTri(r, t, tt, u, v) && tt < maxDist) return true;
            }
            continue;
        }
        if (sp + 2 > 64){ deepest = 65; return false; }
        stack[sp] = first + 1; stack[sp + 1] = first; sp += 2;
        deepest = std::max(deepest, int(sp));
    }
    return false;
}
struct Rng { uint64_t s; float next(){ s ^= s << 13; s ^= s >> 7; s ^= s << 17; return float(double(s >> 11) * (1.0 / 9007199254740992.0)); } };

void check(const char* name, const Scene& s, int rays){
    const std::vector<Tri> tris = trianglesOf(s);
    Bvh b; b.build(tris);
    std::vector<float> N, P;
    b.flatten(N, P);
    CHECK(N.size() == b.nodeCount() * 8 && P.size() == tris.size() * 9);
    const int maxStack = b.maxStack();
    Rng g{0x9E3779B97F4A7C15ull};
    AABB box; for (const Tri& t : tris){ box.grow(t.a); box.grow(t.b); box.grow(t.c); }
    int differ = 0, deepest = 0;
    for (int i = 0; i < rays; ++i){
        const Vec3 o{box.mn.x + (box.mx.x - box.mn.x) * g.next(), box.mn.y + (box.mx.y - box.mn.y) * g.next(), box.mn.z + (box.mx.z - box.mn.z) * g.next()};
        const Vec3 d = normalize(Vec3{g.next() - 0.5f, g.next() - 0.5f, g.next() - 0.5f});
        const float maxDist = 2.0f * g.next();
        bool scan = false;
        for (const Tri& t : tris){ float tt, u, v; if (intersectTri(Ray{o, d}, t, tt, u, v) && tt < maxDist){ scan = true; break; } }
        differ += flatBlocked(N, P, Ray{o, d}, maxDist, deepest) != scan;
    }
    std::printf("%s: %zu triangles, %zu nodes, max stack %d (deepest seen %d), %d of %d rays differ from the scan\n",
                name, tris.size(), b.nodeCount(), maxStack, deepest, differ, rays);
    CHECK(differ == 0);
    CHECK(maxStack <= 64 && deepest <= maxStack);
}
}  // namespace

int main(){
    check("test scene", buildTestScene(64, 64), 20000);
    check("raw-hero", owned::toScene(owned::hero()), 3000);
    check("raw-hall", owned::toScene(owned::hall()), 400);
    return raw_test_summary();
}
