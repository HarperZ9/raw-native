#pragma once
// Private to src/renderer: the CPU path tracer's scene queries (closest hit with its surface,
// visibility, the ray offset, v2 next-event estimation) and one sample's result, shared by the
// v2 path (rt_pt_cpu.cpp) and the v3 path (rt_pt_v3_trace.cpp). Moved here unchanged from
// rt_pt_cpu.cpp for v3; Surf gained `back`, set before the face flip.
#include "raw/renderer/rt_pathtrace.hpp"
#include "rt_pt_math.hpp"
#include <algorithm>
#include <vector>
namespace raw::rt::ptc {
using pt::Bsdf;
inline Vec3 mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float maxc(Vec3 a) { return std::max(a.x, std::max(a.y, a.z)); }
struct Surf { Vec3 p, n, ng, base; const PtMaterial* m; float t; int tri; bool back{false}; };   // back: met the authored face from behind
struct Ctx {
    const PtScene& s;
    const PathTraceDesc& d;
    std::vector<Tri> tris;
    bool hit(const Ray& r, float tMax, Surf& out) const {
        Hit h;
        if (!closest(s.tree, tris, r, tMax, h)) return false;
        const std::size_t t = std::size_t(h.tri);
        const swr::Geometry& g = s.geo;
        const std::uint32_t i0 = g.idx[t * 3], i1 = g.idx[t * 3 + 1], i2 = g.idx[t * 3 + 2];
        const float b0 = 1.0f - h.u - h.v;
        out.t = h.t; out.tri = h.tri;
        out.p = r.o + r.d * h.t;
        out.ng = normalize(cross(tris[t].b - tris[t].a, tris[t].c - tris[t].a));
        out.n = normalize(g.nrm[i0] * b0 + g.nrm[i1] * h.u + g.nrm[i2] * h.v);
        const float u = g.uv[i0].x * b0 + g.uv[i1].x * h.u + g.uv[i2].x * h.v, v = g.uv[i0].y * b0 + g.uv[i1].y * h.u + g.uv[i2].y * h.v;
        const std::uint32_t tex = g.triTexture[t];
        const swr::TextureSet::Entry& e = s.tex.entries[tex];
        const std::uint32_t tx = std::uint32_t(std::int32_t(std::floor(u * float(e.width)))) & (e.width - 1);
        const std::uint32_t ty = std::uint32_t(std::int32_t(std::floor(v * float(e.height)))) & (e.height - 1);
        const std::uint32_t c = s.tex.texels[e.offset + ty * e.width + tx];
        out.m = &s.materials[tex];
        const float k = out.m->baseFactor / 255.0f;
        out.base = {float(c & 255u) * k, float((c >> 8) & 255u) * k, float((c >> 16) & 255u) * k};
        out.back = dot(out.ng, r.d) > 0.0f;
        if (dot(out.ng, r.d) > 0.0f) out.ng = out.ng * -1.0f;   // face the incoming ray
        if (dot(out.n, out.ng) < 0.0f) out.n = out.n * -1.0f;
        return true;
    }
    bool visible(Vec3 p, Vec3 dir, float tMax) const { return !occluded(s.tree, tris, {p, dir}, tMax); }
    static Vec3 offset(const Surf& x, Vec3 dir) {
        const float e = 2e-4f * (1.0f + std::max(std::fabs(x.p.x), std::max(std::fabs(x.p.y), std::fabs(x.p.z))));
        return x.p + x.ng * (dot(x.ng, dir) > 0.0f ? e : -e);
    }
    // Next-event estimation at x: the sun and one emitter sample, MIS-weighted. Returns the sum;
    // diff gets the Lambert term's share (interface v2), the rest is (f - fd) cos.
    Vec3 nee(const Surf& x, const pt::Frame& fr, const Bsdf& bs, Vec3 woL, std::uint32_t px, std::uint32_t smp, std::uint32_t base, Vec3& diff) const {
        Vec3 L{0, 0, 0};
        diff = {0, 0, 0};
        const bool dc = d.controls.dropLambertCosine;
        if (maxc(s.sunIrradiance) > 0.0f) {
            const Vec3 l = normalize(s.sunDir), lL = pt::toLocal(fr, l);
            const pt::Eval e = pt::evalBsdf(bs, woL, lL, dc);
            if (maxc(e.fcos) > 0.0f && visible(offset(x, l), l, 1e30f)) {
                L = L + mul(e.fcos, s.sunIrradiance);
                diff = diff + mul(pt::lambertCos(bs, woL, lL, dc), s.sunIrradiance);
            }
        }
        if (s.emitters.empty()) return L;
        const pt::R2 a = pt::rand2(d.seed, px, smp, base + 2), b = pt::rand2(d.seed, px, smp, base + 3);
        const float target = a.a * s.emitterArea;
        std::size_t k = std::size_t(std::upper_bound(s.emitterCdf.begin(), s.emitterCdf.end(), target) - s.emitterCdf.begin());
        k = std::min(k, s.emitters.size() - 1);
        const Tri& t = tris[s.emitters[k]];
        const float su = std::sqrt(a.b);
        const Vec3 q = t.a * (1.0f - su) + t.b * (su * (1.0f - b.a)) + t.c * (su * b.a);
        const Vec3 to = q - x.p;
        const float dist = length(to);
        const Vec3 l = to * (1.0f / dist), lL = pt::toLocal(fr, l);
        const float cosL = std::fabs(dot(normalize(cross(t.b - t.a, t.c - t.a)), l));
        if (cosL <= 0.0f) return L;
        const float pl = dist * dist / (cosL * s.emitterArea);
        const pt::Eval e = pt::evalBsdf(bs, woL, lL, dc);
        if (maxc(e.fcos) <= 0.0f || !visible(offset(x, l), l, dist * (1.0f - 1e-3f))) return L;
        const float w = d.controls.misWeightOne ? 1.0f : pl * pl / (pl * pl + e.pdf * e.pdf);
        const Vec3 Le = s.materials[s.geo.triTexture[s.emitters[k]]].emission;
        diff = diff + mul(pt::lambertCos(bs, woL, lL, dc), Le) * (w / pl);
        return L + mul(e.fcos, Le) * (w / pl);
    }
};
// One sample's radiance, its v2 split, and the first hit's albedos and normal (0 on a miss).
struct Sample { Vec3 L{0, 0, 0}, D{0, 0, 0}, S{0, 0, 0}, E{0, 0, 0}, alb{0, 0, 0}, nrm{0, 0, 0}, dAlb{0, 0, 0}, sAlb{0, 0, 0}; };
}  // namespace raw::rt::ptc
