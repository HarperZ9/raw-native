// The GI scene, the dense key tables, directions and the probe allocation: see
// raw/renderer/gi_cascades.hpp. Every float32 expression here has a twin in gi.wgsl, in the same
// operation order.
#include "raw/renderer/gi_cascades.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gi {

Scene3 sceneOf(const Scene& s){
    Scene3 o;
    AABB box;
    for (const Mesh& m : s.meshes){
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3){
            Vec3 v[3], n[3];
            for (int k = 0; k < 3; ++k){
                const std::size_t j = std::size_t(m.indices[i + std::size_t(k)]);
                v[k] = m.positions[j];
                n[k] = j < m.normals.size() ? m.normals[j] : Vec3{0, 1, 0};
                box.grow(v[k]);
            }
            o.tris.push_back({v[0], v[1], v[2]});
            o.nrm.insert(o.nrm.end(), {n[0], n[1], n[2]});
            o.albedo.push_back(m.material.albedo);
        }
    }
    o.tree = rt::buildPloc(o.tris);
    const Vec3 dir = s.lights.empty() ? Vec3{0, -1, 0} : s.lights[0].dir;
    o.toLight = normalize(dir * -1.0f);
    o.li = s.lights.empty() ? 1.0f : s.lights[0].intensity;
    const Vec3 pad{0.5f, 0.5f, 0.5f};
    o.lo = o.tris.empty() ? Vec3{} : box.mn - pad;
    o.hi = o.tris.empty() ? Vec3{} : box.mx + pad;
    return o;
}

Grid gridOf(const Scene3& s, float cell){
    Grid g;
    g.origin = s.lo; g.cell = cell;
    const auto n = [&](float a, float b){ return std::uint32_t(std::max(1.0f, std::ceil((b - a) / cell))); };
    g.nx = n(s.lo.x, s.hi.x); g.ny = n(s.lo.y, s.hi.y); g.nz = n(s.lo.z, s.hi.z);
    return g;
}
std::uint32_t Grid::key(Vec3 p, std::uint32_t bucket) const {
    const auto c = [&](float v, float o, std::uint32_t n){
        const float f = std::floor((v - o) / cell);
        return std::uint32_t(std::clamp(f, 0.0f, float(n - 1)));
    };
    const std::uint32_t x = c(p.x, origin.x, nx), y = c(p.y, origin.y, ny), z = c(p.z, origin.z, nz);
    return ((z * ny + y) * nx + x) * 6u + bucket;
}
Vec3 Grid::centre(std::uint32_t key) const {
    const std::uint32_t cellIndex = key / 6u, x = cellIndex % nx, y = (cellIndex / nx) % ny, z = cellIndex / (nx * ny);
    return {origin.x + (float(x) + 0.5f) * cell, origin.y + (float(y) + 0.5f) * cell, origin.z + (float(z) + 0.5f) * cell};
}
std::uint32_t bucketOf(Vec3 n){
    const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (ax >= ay && ax >= az) return n.x < 0.0f ? 1u : 0u;
    if (ay >= az) return n.y < 0.0f ? 3u : 2u;
    return n.z < 0.0f ? 5u : 4u;
}
namespace {
float signOf(float v){ return v < 0.0f ? -1.0f : 1.0f; }
}
std::uint32_t octBin(Vec3 d, std::uint32_t n){
    const float l1 = std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z);
    float u = d.x / l1, v = d.y / l1;
    if (d.z < 0.0f){
        const float a = (1.0f - std::fabs(v)) * signOf(u), b = (1.0f - std::fabs(u)) * signOf(v);
        u = a; v = b;
    }
    const float fn = float(n);
    const std::uint32_t i = std::uint32_t(std::clamp(std::floor((u * 0.5f + 0.5f) * fn), 0.0f, fn - 1.0f));
    const std::uint32_t j = std::uint32_t(std::clamp(std::floor((v * 0.5f + 0.5f) * fn), 0.0f, fn - 1.0f));
    return j * n + i;
}
Vec3 octDir(float u, float v){
    const float z = 1.0f - std::fabs(u) - std::fabs(v);
    float x = u, y = v;
    if (z < 0.0f){ x = (1.0f - std::fabs(v)) * signOf(u); y = (1.0f - std::fabs(u)) * signOf(v); }
    return normalize(Vec3{x, y, z});
}
float hash01(std::uint32_t x, std::uint32_t y, std::uint32_t s){
    std::uint32_t h = x * 374761393u + y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}
// cos and sin of 2 pi u for u in [0, 1): the octant's centre angle rotated by a degree-7 Taylor
// polynomial over at most pi / 8, so the CPU and the GPU (gi_common.wgsl turn()) compute the same
// float32 expression instead of two libraries' trigonometry (evidence/p6-radiance-cascades-runs.json).
void turn(float u, float& c, float& s){
    static const float cA[8] = {0.92387953f, 0.38268343f, -0.38268343f, -0.92387953f, -0.92387953f, -0.38268343f, 0.38268343f, 0.92387953f};
    static const float sA[8] = {0.38268343f, 0.92387953f, 0.92387953f, 0.38268343f, -0.38268343f, -0.92387953f, -0.92387953f, -0.38268343f};
    const float e = u * 8.0f, k = std::floor(e);
    const int o = std::clamp(int(k), 0, 7);
    const float x = (e - k - 0.5f) * 0.78539816f, x2 = x * x;
    const float c0 = 1.0f + x2 * (-0.5f + x2 * (0.041666668f + x2 * -0.0013888889f));
    const float s0 = x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333338f + x2 * -0.00019841270f)));
    c = cA[o] * c0 - sA[o] * s0;
    s = sA[o] * c0 + cA[o] * s0;
}
Vec3 cosineDir(Vec3 n, float u1, float u2){
    const Vec3 a = std::fabs(n.x) > 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 t = normalize(cross(a, n)), b = cross(n, t);
    float cp, sp;
    turn(u2, cp, sp);
    const float r = std::sqrt(u1);
    const float lx = r * cp, ly = r * sp, lz = std::sqrt(std::max(0.0f, 1.0f - u1));
    return normalize(t * lx + b * ly + n * lz);
}

std::vector<Cascade> probesOf(const Scene3& s, const GBuffer& g, const Params& p){
    std::vector<Cascade> cs(kCascades);
    for (int c = 0; c < kCascades; ++c){
        Cascade& C = cs[std::size_t(c)];
        C.grid = gridOf(s, p.cell0 * float(1 << c));
        C.bins = std::uint32_t(p.bins0 << c);
        C.slotOfKey.assign(std::size_t(C.grid.keys()), kNone);
        for (int i = 0; i < g.w * g.h; ++i)
            if (g.mask.px[std::size_t(i)]) C.slotOfKey[C.grid.key(g.position.px[std::size_t(i)], bucketOf(g.normal.px[std::size_t(i)]))] = 0u;
        for (std::size_t k = 0; k < C.slotOfKey.size(); ++k)
            if (C.slotOfKey[k] != kNone){ C.slotOfKey[k] = std::uint32_t(C.keyOfSlot.size()); C.keyOfSlot.push_back(std::uint32_t(k)); }
        const std::size_t cells = C.keyOfSlot.size() * C.bins * C.bins;
        C.acc.assign(cells * 5, 0u);
        C.merged.assign(cells * 3, 0.0);
    }
    return cs;
}

}  // namespace raw::gi
