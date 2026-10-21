// The CPU path tracer: see raw/renderer/rt_pathtrace.hpp. Float32, the operations of
// src/renderer/gpu/shaders/pt.wgsl, row-parallel with per-pixel random numbers.
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/core/parallel.hpp"
#include "rt_pt_math.hpp"
#include "rt_pt_ctx.hpp"
#include "rt_pt_v3.hpp"
#include <thread>
namespace raw::rt {
namespace {
using pt::Bsdf;
using ptc::mul; using ptc::maxc; using ptc::Surf; using ptc::Ctx; using ptc::Sample;
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
// The v1 path, unchanged in its float operations on L. After the first hit every contribution
// is also added to R by tw, the throughput relative to the first BSDF sample; R is split at the
// end by the first sample's Lambert weight wd and the rest ws (addendum 5).
Sample tracePath(const Ctx& C, const PtScene& s, const PathTraceDesc& d, Ray ray, std::uint32_t px, std::uint32_t smp) {
    Sample o;
    Vec3 T{1, 1, 1}, tw{1, 1, 1}, R{0, 0, 0}, wd{0, 0, 0}, ws{0, 0, 0};
    float prevPdf = 0.0f;
    const bool dc = d.controls.dropLambertCosine;
    for (std::uint32_t b = 0; b <= d.maxBounces; ++b) {
        Surf x0;
        if (!C.hit(ray, 1e30f, x0)) {
            o.L = o.L + mul(T, s.sky);
            if (b == 0) o.E = s.sky; else R = R + mul(tw, s.sky);
            break;
        }
        const Bsdf bs{x0.base, x0.m->roughness, x0.m->metallic, x0.m->specular};
        if (b == 0) {
            o.alb = x0.base; o.nrm = x0.n;
            o.dAlb = x0.base * (1.0f - x0.m->metallic);
            o.sAlb = pt::specAlbedo(bs, std::clamp(-dot(x0.n, ray.d), 0.0f, 1.0f));
        }
        const Vec3 Le = x0.m->emission;
        if (maxc(Le) > 0.0f) {
            float w = 1.0f;
            if (b > 0 && !d.controls.misWeightOne) {
                const float cosL = std::fabs(dot(x0.ng, ray.d)), pl = x0.t * x0.t / (cosL * s.emitterArea);
                w = prevPdf * prevPdf / (prevPdf * prevPdf + pl * pl);
            }
            o.L = o.L + mul(T, Le) * w;
            if (b == 0) o.E = Le; else R = R + mul(tw, Le) * w;
        }
        if (b == d.maxBounces) break;
        const pt::Frame fr = pt::basis(x0.n);
        const Vec3 woL = pt::toLocal(fr, ray.d * -1.0f);
        if (woL.z <= 0.0f) break;
        const std::uint32_t base = 2 + b * 4;
        Vec3 nd;
        const Vec3 Ln = C.nee(x0, fr, bs, woL, px, smp, base, nd);
        o.L = o.L + mul(T, Ln);
        if (b == 0) { o.D = nd; o.S = Ln - nd; } else R = R + mul(tw, Ln);
        const pt::R2 u = pt::rand2(d.seed, px, smp, base), r = pt::rand2(d.seed, px, smp, base + 1);
        Vec3 wiL;
        if (!pt::sampleBsdf(bs, woL, r.a, u.a, u.b, wiL)) break;
        const pt::Eval e = pt::evalBsdf(bs, woL, wiL, dc);
        if (e.pdf <= 0.0f) break;
        const Vec3 step = e.fcos * (1.0f / e.pdf);
        T = mul(T, step);
        if (b == 0) { wd = pt::lambertCos(bs, woL, wiL, dc) * (1.0f / e.pdf); ws = step - wd; }
        else tw = mul(tw, step);
        prevPdf = e.pdf;
        if (d.russianRoulette && b >= 3) {
            const float q = std::min(0.95f, maxc(T));
            if (r.b >= q) break;
            T = T * (1.0f / q);
            tw = tw * (1.0f / q);
        }
        const Vec3 wi = pt::toWorld(fr, wiL);
        ray = {Ctx::offset(x0, wi), wi};
    }
    o.D = o.D + mul(wd, R);
    if (!d.controls.aovDropSpecularIndirect) o.S = o.S + mul(ws, R);
    if (d.controls.aovEmissionToDiffuse) { o.D = o.D + o.E; o.E = {0, 0, 0}; }
    return o;
}
void put3(std::vector<float>& v, std::size_t p, Vec3 a) { v[p * 3] = a.x; v[p * 3 + 1] = a.y; v[p * 3 + 2] = a.z; }
struct Moments {
    double s1{0}, s2{0};
    void add(double l) { s1 += l; s2 += l * l; }
    float var(std::uint32_t n) const { return n > 1 ? float((s2 - s1 * s1 / n) / (n - 1) / n) : 0.0f; }
};
// The pixel-centre ray's depth, triangle and motion.
void centre(const Ctx& C, const Cam& cam, const Cam& prev, int x, int y, int W, int H, std::size_t p, PathTraceOutput& O) {
    Surf c;
    if (!C.hit({cam.eye, dirOf(cam, float(x) + 0.5f, float(y) + 0.5f, W, H)}, 1e30f, c)) return;
    O.depth[p] = dot(c.p - cam.eye, cam.f);
    O.triangle[p] = c.tri;
    const Vec3 v = c.p - prev.eye;
    const float z = dot(v, prev.f);
    const float nx = dot(v, prev.s) / (z * prev.th * prev.aspect), ny = dot(v, prev.u) / (z * prev.th);
    O.motion[p * 2] = (nx * 0.5f + 0.5f) * float(W) - (float(x) + 0.5f);
    O.motion[p * 2 + 1] = (1.0f - (ny * 0.5f + 0.5f)) * float(H) - (float(y) + 0.5f);
}
}  // namespace

PathTraceOutput pathTraceCpu(const PtScene& s, const PathTraceDesc& d, int threads) {
    PathTraceOutput O;
    const int W = d.width, H = d.height;
    O.width = W; O.height = H; O.spp = d.spp;
    const std::size_t n = std::size_t(W) * std::size_t(H);
    O.radiance.assign(n * 3, 0.0f); O.albedo.assign(n * 3, 0.0f); O.normal.assign(n * 3, 0.0f);
    O.depth.assign(n, 0.0f); O.variance.assign(n, 0.0f); O.motion.assign(n * 2, 0.0f); O.triangle.assign(n, -1);
    for (auto* v : {&O.diffuse, &O.specular, &O.emission, &O.diffuseAlbedo, &O.specularAlbedo}) v->assign(n * 3, 0.0f);
    O.diffuseVariance.assign(n, 0.0f); O.specularVariance.assign(n, 0.0f);
    Ctx C{s, d, trianglesOf(s.geo)};
    const bool v3on = usesV3(s);   // a v2 scene keeps the v2 path, byte for byte
    const Cam cam = camOf(d.camera, W, H), prev = camOf(d.previous, W, H);
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    parallelRows(H, threads, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const std::uint32_t px = std::uint32_t(y * W + x);
            Vec3 sum{0, 0, 0}, alb{0, 0, 0}, nrm{0, 0, 0}, sd{0, 0, 0}, ss{0, 0, 0}, se{0, 0, 0}, sda{0, 0, 0}, ssa{0, 0, 0};
            Moments mL, mD, mS;
            for (std::uint32_t k = 0; k < d.spp; ++k) {
                const std::uint32_t smp = d.sppBegin + k;
                const pt::R2 j = d.jitter ? pt::rand2(d.seed, px, smp, 0) : pt::R2{0.5f, 0.5f};
                const Ray ray{cam.eye, dirOf(cam, float(x) + j.a, float(y) + j.b, W, H)};
                const Sample o = v3on ? v3::tracePath(C, s, d, ray, px, smp) : tracePath(C, s, d, ray, px, smp);
                sum = sum + o.L; alb = alb + o.alb; nrm = nrm + o.nrm;
                sd = sd + o.D; ss = ss + o.S; se = se + o.E; sda = sda + o.dAlb; ssa = ssa + o.sAlb;
                mL.add(pt::luminance(o.L)); mD.add(pt::luminance(o.D)); mS.add(pt::luminance(o.S));
            }
            const float inv = d.spp ? 1.0f / float(d.spp) : 0.0f;
            const std::size_t p = std::size_t(px);
            put3(O.radiance, p, sum * inv); put3(O.albedo, p, alb * inv); put3(O.normal, p, nrm * inv);
            put3(O.diffuse, p, sd * inv); put3(O.specular, p, ss * inv); put3(O.emission, p, se * inv);
            put3(O.diffuseAlbedo, p, sda * inv); put3(O.specularAlbedo, p, ssa * inv);
            O.variance[p] = mL.var(d.spp); O.diffuseVariance[p] = mD.var(d.spp); O.specularVariance[p] = mS.var(d.spp);
            centre(C, cam, prev, x, y, W, H, p, O);
        }
    });
    return O;
}

}  // namespace raw::rt
