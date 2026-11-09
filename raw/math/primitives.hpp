#pragma once
#include "raw/math/vec.hpp"
#include <algorithm>
#include <cmath>
namespace raw {
struct Ray { Vec3 o, d; };
struct Tri { Vec3 a, b, c; };
struct AABB {
    Vec3 mn{ 1e30f,  1e30f,  1e30f};
    Vec3 mx{-1e30f, -1e30f, -1e30f};
    void grow(Vec3 p){
        mn = {std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};
        mx = {std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)}; }
};
// The ray's frame of the watertight test: the components permuted so the largest |d| is z,
// then x and y swapped when that component is negative, which keeps the edge functions' winding.
inline Vec3 watertightFrame(Vec3 v, int kz, bool swap) {
    const Vec3 p = kz == 0 ? Vec3{v.y, v.z, v.x} : kz == 1 ? Vec3{v.z, v.x, v.y} : v;
    return swap ? Vec3{p.y, p.x, p.z} : p;
}
// Watertight ray-triangle intersection (Woop, Benthin and Wald, "Watertight Ray/Triangle
// Intersection", JCGT 2(1), 2013). The vertices move into the ray's sheared frame and the signs
// of the three 2D edge functions decide the hit. Two triangles that share an edge evaluate it
// with the same products, so a ray through that edge or a shared vertex hits at least one of
// them. An edge function of exactly 0 counts as inside: both neighbours report the hit and the
// closest-hit order breaks the tie. The paper's float64 recomputation of a zero edge function is
// left out, so the CPU and the GPU passes (which have no float64) run the same test.
// Two-sided. Returns true and fills t (distance, t > 1e-4) and u, v (the barycentric weights of
// b and c, the order of the Moeller-Trumbore test it replaced on 2026-10-31).
// The shaders' hit_tri (pt.wgsl, rt.wgsl) and hit (passes.wgsl) mirror these operations.
inline bool intersectTri(const Ray& r, const Tri& tri, float& t, float& u, float& v){
    const float ax = std::fabs(r.d.x), ay = std::fabs(r.d.y), az = std::fabs(r.d.z);
    const int kz = ax > ay ? (ax > az ? 0 : 2) : (ay > az ? 1 : 2);
    Vec3 d = watertightFrame(r.d, kz, false);
    const bool sw = d.z < 0.0f;
    if (sw) d = Vec3{d.y, d.x, d.z};
    const float sx = d.x / d.z, sy = d.y / d.z, sz = 1.0f / d.z;
    const Vec3 A = watertightFrame(tri.a - r.o, kz, sw), B = watertightFrame(tri.b - r.o, kz, sw), C = watertightFrame(tri.c - r.o, kz, sw);
    const float Ax = A.x - sx * A.z, Ay = A.y - sy * A.z;
    const float Bx = B.x - sx * B.z, By = B.y - sy * B.z;
    const float Cx = C.x - sx * C.z, Cy = C.y - sy * C.z;
    const float U = Cx * By - Cy * Bx, V = Ax * Cy - Ay * Cx, W = Bx * Ay - By * Ax;
    if ((U < 0.0f || V < 0.0f || W < 0.0f) && (U > 0.0f || V > 0.0f || W > 0.0f)) return false;
    const float det = U + V + W;
    if (det == 0.0f) return false;
    // The signs decided the hit. t, u and v come from Moeller-Trumbore's operations, whose t carries
    // less rounding than the sheared sum (and which every ray the old test accepted keeps, to the
    // bit); the sheared values only where its determinant is unusable, the rays the old test lost.
    const Vec3 e1 = tri.b - tri.a, e2 = tri.c - tri.a, p = cross(r.d, e2);
    const float dm = dot(e1, p);
    if (dm > -1e-7f && dm < 1e-7f) {
        const float T = U * (sz * A.z) + V * (sz * B.z) + W * (sz * C.z), inv = 1.0f / det;
        t = T * inv; u = V * inv; v = W * inv;
        return t > 1e-4f;
    }
    const float inv = 1.0f / dm;
    const Vec3 tv = r.o - tri.a, q = cross(tv, e1);
    u = std::max(0.0f, dot(tv, p) * inv);
    v = std::max(0.0f, dot(r.d, q) * inv);
    t = dot(e2, q) * inv;
    const float s = u + v;
    if (s > 1.0f) { u /= s; v /= s; }
    return t > 1e-4f;
}
}
