// The CPU path tracer: see raw/renderer/rt_pathtrace.hpp. Float32, the operations of
// src/renderer/gpu/shaders/pt.wgsl, row-parallel with per-pixel random numbers.
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/core/parallel.hpp"
#include "rt_pt_math.hpp"
#include <thread>
namespace raw::rt {
namespace {
using pt::Bsdf;
Vec3 mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
float maxc(Vec3 a) { return std::max(a.x, std::max(a.y, a.z)); }
struct Cam { Vec3 eye, f, s, u; float th, aspect; };
Cam camOf(const PtCamera& c, int w, int h) {
    Cam k;
    k.eye = c.eye; k.f = normalize(c.target - c.eye); k.s = normalize(cross(k.f, c.up)); k.u = cross(k.s, k.f);
    k.th = std::tan(c.fovy * 0.5f); k.aspect = float(w) / float(h);
    return k;
}
Vec3 dirOf(const Cam& c, float px, float py, int w, int h) {
    const float nx = px / float(w) * 2.0f - 1.0f, ny = 1.0f - py / float(h) * 2.0f;
    return normalize(c.f + c.s * (nx * c.th * c.aspect) + c.u * (ny * c.th));
}
struct Surf { Vec3 p, n, ng, base; const PtMaterial* m; float t; int tri; };
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
        if (dot(out.ng, r.d) > 0.0f) out.ng = out.ng * -1.0f;   // face the incoming ray
        if (dot(out.n, out.ng) < 0.0f) out.n = out.n * -1.0f;
        return true;
    }
    bool visible(Vec3 p, Vec3 dir, float tMax) const { return !occluded(s.tree, tris, {p, dir}, tMax); }
    static Vec3 offset(const Surf& x, Vec3 dir) {
        const float e = 2e-4f * (1.0f + std::max(std::fabs(x.p.x), std::max(std::fabs(x.p.y), std::fabs(x.p.z))));
        return x.p + x.ng * (dot(x.ng, dir) > 0.0f ? e : -e);
    }
    // Next-event estimation at x: the sun and one emitter sample, MIS-weighted.
    Vec3 nee(const Surf& x, const pt::Frame& fr, const Bsdf& bs, Vec3 woL, std::uint32_t px, std::uint32_t smp, std::uint32_t base) const {
        Vec3 L{0, 0, 0};
        if (maxc(s.sunIrradiance) > 0.0f) {
            const Vec3 l = normalize(s.sunDir);
            const pt::Eval e = pt::evalBsdf(bs, woL, pt::toLocal(fr, l), d.controls.dropLambertCosine);
            if (maxc(e.fcos) > 0.0f && visible(offset(x, l), l, 1e30f)) L = L + mul(e.fcos, s.sunIrradiance);
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
        const Vec3 l = to * (1.0f / dist);
        const float cosL = std::fabs(dot(normalize(cross(t.b - t.a, t.c - t.a)), l));
        if (cosL <= 0.0f) return L;
        const float pl = dist * dist / (cosL * s.emitterArea);
        const pt::Eval e = pt::evalBsdf(bs, woL, pt::toLocal(fr, l), d.controls.dropLambertCosine);
        if (maxc(e.fcos) <= 0.0f || !visible(offset(x, l), l, dist * (1.0f - 1e-3f))) return L;
        const float w = d.controls.misWeightOne ? 1.0f : pl * pl / (pl * pl + e.pdf * e.pdf);
        const Vec3 Le = s.materials[s.geo.triTexture[s.emitters[k]]].emission;
        return L + mul(e.fcos, Le) * (w / pl);
    }
};
}  // namespace

PathTraceOutput pathTraceCpu(const PtScene& s, const PathTraceDesc& d, int threads) {
    PathTraceOutput O;
    const int W = d.width, H = d.height;
    O.width = W; O.height = H; O.spp = d.spp;
    const std::size_t n = std::size_t(W) * std::size_t(H);
    O.radiance.assign(n * 3, 0.0f); O.albedo.assign(n * 3, 0.0f); O.normal.assign(n * 3, 0.0f);
    O.depth.assign(n, 0.0f); O.variance.assign(n, 0.0f); O.motion.assign(n * 2, 0.0f); O.triangle.assign(n, -1);
    Ctx C{s, d, trianglesOf(s.geo)};
    const Cam cam = camOf(d.camera, W, H), prev = camOf(d.previous, W, H);
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    parallelRows(H, threads, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const std::uint32_t px = std::uint32_t(y * W + x);
            Vec3 sum{0, 0, 0}, alb{0, 0, 0}, nrm{0, 0, 0};
            double s1 = 0.0, s2 = 0.0;
            for (std::uint32_t k = 0; k < d.spp; ++k) {
                const std::uint32_t smp = d.sppBegin + k;
                const pt::R2 j = d.jitter ? pt::rand2(d.seed, px, smp, 0) : pt::R2{0.5f, 0.5f};
                Ray ray{cam.eye, dirOf(cam, float(x) + j.a, float(y) + j.b, W, H)};
                Vec3 T{1, 1, 1}, L{0, 0, 0};
                float prevPdf = 0.0f;
                for (std::uint32_t b = 0; b <= d.maxBounces; ++b) {
                    Surf x0;
                    if (!C.hit(ray, 1e30f, x0)) { L = L + mul(T, s.sky); break; }
                    if (b == 0) { alb = alb + x0.base; nrm = nrm + x0.n; }
                    const Vec3 Le = x0.m->emission;
                    if (maxc(Le) > 0.0f) {
                        float w = 1.0f;
                        if (b > 0 && !d.controls.misWeightOne) {
                            const float cosL = std::fabs(dot(x0.ng, ray.d)), pl = x0.t * x0.t / (cosL * s.emitterArea);
                            w = prevPdf * prevPdf / (prevPdf * prevPdf + pl * pl);
                        }
                        L = L + mul(T, Le) * w;
                    }
                    if (b == d.maxBounces) break;
                    const pt::Frame fr = pt::basis(x0.n);
                    const Vec3 woL = pt::toLocal(fr, ray.d * -1.0f);
                    if (woL.z <= 0.0f) break;
                    const Bsdf bs{x0.base, x0.m->roughness, x0.m->metallic, x0.m->specular};
                    const std::uint32_t base = 2 + b * 4;
                    L = L + mul(T, C.nee(x0, fr, bs, woL, px, smp, base));
                    const pt::R2 u = pt::rand2(d.seed, px, smp, base), r = pt::rand2(d.seed, px, smp, base + 1);
                    Vec3 wiL;
                    if (!pt::sampleBsdf(bs, woL, r.a, u.a, u.b, wiL)) break;
                    const pt::Eval e = pt::evalBsdf(bs, woL, wiL, d.controls.dropLambertCosine);
                    if (e.pdf <= 0.0f) break;
                    T = mul(T, e.fcos * (1.0f / e.pdf));
                    prevPdf = e.pdf;
                    if (d.russianRoulette && b >= 3) {
                        const float q = std::min(0.95f, maxc(T));
                        if (r.b >= q) break;
                        T = T * (1.0f / q);
                    }
                    const Vec3 wi = pt::toWorld(fr, wiL);
                    ray = {Ctx::offset(x0, wi), wi};
                }
                sum = sum + L;
                const double l = pt::luminance(L);
                s1 += l; s2 += l * l;
            }
            const float inv = d.spp ? 1.0f / float(d.spp) : 0.0f;
            const std::size_t p = std::size_t(px);
            const Vec3 m = sum * inv, a = alb * inv, nn = nrm * inv;
            O.radiance[p * 3] = m.x; O.radiance[p * 3 + 1] = m.y; O.radiance[p * 3 + 2] = m.z;
            O.albedo[p * 3] = a.x; O.albedo[p * 3 + 1] = a.y; O.albedo[p * 3 + 2] = a.z;
            O.normal[p * 3] = nn.x; O.normal[p * 3 + 1] = nn.y; O.normal[p * 3 + 2] = nn.z;
            if (d.spp > 1) O.variance[p] = float((s2 - s1 * s1 / d.spp) / (d.spp - 1) / d.spp);
            Surf c;
            if (C.hit({cam.eye, dirOf(cam, float(x) + 0.5f, float(y) + 0.5f, W, H)}, 1e30f, c)) {
                O.depth[p] = dot(c.p - cam.eye, cam.f);
                O.triangle[p] = c.tri;
                const Vec3 v = c.p - prev.eye;
                const float z = dot(v, prev.f);
                const float nx = dot(v, prev.s) / (z * prev.th * prev.aspect), ny = dot(v, prev.u) / (z * prev.th);
                O.motion[p * 2] = (nx * 0.5f + 0.5f) * float(W) - (float(x) + 0.5f);
                O.motion[p * 2 + 1] = (1.0f - (ny * 0.5f + 0.5f)) * float(H) - (float(y) + 0.5f);
            }
        }
    });
    return O;
}

}  // namespace raw::rt
