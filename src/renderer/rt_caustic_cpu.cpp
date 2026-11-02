// The spectral caustic tracer's CPU reference (raw/renderer/rt_caustic.hpp). hw_caustic.hlsl mirrors it.
#include "raw/renderer/bvh.hpp"
#include "rt_caustic_math.hpp"
#include <algorithm>
#include <atomic>
#include <thread>
namespace raw::rt {
namespace {
using namespace caustic;
struct Ctx {
    const CausticScene& s;
    const CausticDesc& d;
    Packed P;
    Bvh bvh;
    Cam cam;
    std::uint32_t flags{0};
    std::uint64_t seedL{0};
    const float* T() const { return P.tables.data(); }
};
struct Surf { bool hit{false}; float t{0}; Vec3 p, ng, ns; std::uint32_t mat{0}; };
Surf closest(const Ctx& c, Vec3 o, Vec3 d) {
    Surf s;
    Hit h;
    if (!c.bvh.closest(Ray{o, d}, 3.0e38f, h)) return s;
    const std::size_t t = std::size_t(h.tri);
    const float* v = &c.s.tris[t * 9];
    const Vec3 a{v[0], v[1], v[2]}, b{v[3], v[4], v[5]}, cc{v[6], v[7], v[8]};
    const float* g = &c.P.geo[t * 12];
    const float w = 1.0f - h.u - h.v;
    s.hit = true; s.t = h.t; s.p = o + d * h.t;
    s.ng = normalize(cross(b - a, cc - a));
    s.ns = normalize(Vec3{g[0], g[1], g[2]} * w + Vec3{g[3], g[4], g[5]} * h.u + Vec3{g[6], g[7], g[8]} * h.v);
    s.mat = std::uint32_t(g[9]);
    return s;
}
bool occluded(const Ctx& c, Vec3 o, Vec3 d, float tMax) { return c.bvh.occluded(Ray{o, d}, tMax); }
const float* light(const Ctx& c, std::uint32_t l) { return c.T() + c.P.lightOff + l * kLightFloats; }
// Radiance leaving lamp l along w (unit, away from the lamp): power S_A inside the cone, else 0.
float lampLe(const Ctx& c, std::uint32_t l, Vec3 w, float lambda) {
    const float* L = light(c, l);
    const float cs = L[9] * w.x + L[10] * w.y + L[11] * w.z;
    return (cs > 0.0f && cs >= L[13]) ? L[15] * tableAt(c.T(), kIllum, 1, 0, lambda) : 0.0f;
}
float kindOf(const Ctx& c, std::uint32_t m) { return c.T()[c.P.matOff + m * kMatFloats]; }
float albedoOf(const Ctx& c, std::uint32_t m) { return c.T()[c.P.matOff + m * kMatFloats + 1]; }
float indexOf(const Ctx& c, std::uint32_t m, float lambda) {
    const std::uint32_t g = std::uint32_t(c.T()[c.P.matOff + m * kMatFloats + 2]);
    return indexAt(c.T(), c.P.glassOff, g, lambda, c.flags, float(c.d.controls.indexOffset));
}
// Next-event estimation from a diffuse point to one lamp.
float nee(const Ctx& c, const Surf& s, Vec3 n, float lambda, pt::R2 u, float pick) {
    const std::uint32_t l = std::min(std::uint32_t(pick * float(c.P.lights)), c.P.lights - 1);
    const float* L = light(c, l);
    const Vec3 q = Vec3{L[0], L[1], L[2]} + Vec3{L[3], L[4], L[5]} * u.a + Vec3{L[6], L[7], L[8]} * u.b;
    const Vec3 to = q - s.p;
    const float dist = length(to);
    const Vec3 wl = to * (1.0f / dist);
    const float cosX = dot(n, wl), cosL = -(L[9] * wl.x + L[10] * wl.y + L[11] * wl.z);
    if (!(cosX > 0.0f) || !(cosL > 0.0f)) return 0.0f;
    const float le = lampLe(c, l, wl * -1.0f, lambda);
    if (le <= 0.0f || occluded(c, offsetP(s.p, n, wl), wl, dist * (1.0f - 1e-3f))) return 0.0f;
    return le * cosX * cosL / (dist * dist) * L[12] * float(c.P.lights);
}
// One camera sample: the radiance at one wavelength; lambda returned for the colour weights.
float cameraSample(const Ctx& c, std::uint32_t pix, int x, int y, std::uint32_t smp, float& lambda) {
    const std::uint64_t seed = c.d.seed;
    const pt::R2 j = caustic::rand2(seed, pix, smp, 0), rl = caustic::rand2(seed, pix, smp, kLambdaDim);
    lambda = kLambda0 + kLambdaSpan * rl.a;
    Vec3 ro = c.cam.eye, rd = camDir(c.cam, float(x) + j.a, float(y) + j.b, c.d.width, c.d.height);
    const bool split = (c.flags & F_SPLIT) != 0u;
    float tp = 1.0f, L = 0.0f;
    bool diffuseSeen = false;
    for (std::uint32_t b = 0; b <= c.d.maxBounces; ++b) {
        const Surf s = closest(c, ro, rd);
        if (!s.hit) break;
        const float kind = kindOf(c, s.mat);
        const Vec3 ng = dot(s.ng, rd) < 0.0f ? s.ng : s.ng * -1.0f;
        if (kind == 2.0f) {
            if (!split || !diffuseSeen) L += tp * lampLe(c, std::uint32_t(c.T()[c.P.matOff + s.mat * kMatFloats + 3]), rd * -1.0f, lambda);
            break;
        }
        const pt::R2 u = caustic::rand2(seed, pix, smp, 2 + 3 * b), v = caustic::rand2(seed, pix, smp, 3 + 3 * b), w = caustic::rand2(seed, pix, smp, 4 + 3 * b);
        if (kind == 1.0f) {
            const Vec3 nd = glassStep(rd, s.ns, s.ng, indexOf(c, s.mat, lambda), u.a);
            ro = offsetP(s.p, s.ng, nd); rd = nd;
            continue;
        }
        const float rho = albedoOf(c, s.mat);
        if (split) L += tp * (rho / kPi) * nee(c, s, ng, lambda, v, w.a);
        diffuseSeen = true;
        const Vec3 wi = cosineDir(ng, u.a, u.b);
        tp *= rho;
        if (b >= 3) {
            const float q = std::min(0.95f, tp);
            if (w.b >= q) break;
            tp /= q;
        }
        ro = offsetP(s.p, ng, wi); rd = wi;
    }
    return L;
}
// One light path: every diffuse vertex after a glass interaction joined to the camera.
// Photon `slot` of round `round` (a round is one photon a pixel): its random numbers are those of the
// hardware light pass, which dispatches one round at a time.
void lightPath(const Ctx& c, std::uint32_t k, std::uint32_t round, double invTotal, std::vector<double>& img) {
    const pt::R2 r0 = caustic::rand2(c.seedL, k, round, 0), r1 = caustic::rand2(c.seedL, k, round, 1), r2 = caustic::rand2(c.seedL, k, round, 2);
    const std::uint32_t l = std::min(std::uint32_t(r0.a * float(c.P.lights)), c.P.lights - 1);
    const float lambda = kLambda0 + kLambdaSpan * caustic::rand2(c.seedL, k, round, kLambdaDim).a;
    const float* L = light(c, l);
    const Vec3 nl{L[9], L[10], L[11]};
    const Vec3 q = Vec3{L[0], L[1], L[2]} + Vec3{L[3], L[4], L[5]} * r1.a + Vec3{L[6], L[7], L[8]} * r1.b;
    const float s2 = r2.a * L[14], phi = 2.0f * kPi * r2.b, ct = std::sqrt(1.0f - s2), st = std::sqrt(s2);
    const pt::Frame f = pt::basis(nl);
    Vec3 rd = normalize(f.t * (st * std::cos(phi)) + f.b * (st * std::sin(phi)) + nl * ct), ro = offsetP(q, nl, rd);
    float beta = L[15] * tableAt(c.T(), kIllum, 1, 0, lambda) * L[12] * kPi * L[14] * float(c.P.lights) * kLambdaSpan;
    const float apix = (2.0f * c.cam.tanHalf * c.cam.aspect / float(c.d.width)) * (2.0f * c.cam.tanHalf / float(c.d.height));
    bool sawGlass = false;
    for (std::uint32_t b = 0; b <= c.d.maxBounces; ++b) {
        const Surf s = closest(c, ro, rd);
        if (!s.hit) break;
        const float kind = kindOf(c, s.mat);
        if (kind == 2.0f) break;
        const pt::R2 u = caustic::rand2(c.seedL, k, round, 3 + 3 * b), v = caustic::rand2(c.seedL, k, round, 4 + 3 * b);
        if (kind == 1.0f) {
            const Vec3 nd = glassStep(rd, s.ns, s.ng, indexOf(c, s.mat, lambda), u.a);
            ro = offsetP(s.p, s.ng, nd); rd = nd; sawGlass = true;
            continue;
        }
        if (!sawGlass) break;   // light-adjacent vertex diffuse: the camera paths' next-event estimate covers it
        const Vec3 ng = dot(s.ng, rd) < 0.0f ? s.ng : s.ng * -1.0f;
        const float rho = albedoOf(c, s.mat);
        const Vec3 tc = c.cam.eye - s.p;
        const float dist = length(tc);
        const Vec3 wc = tc * (1.0f / dist);
        const float cosX = dot(ng, wc), z = -dot(wc, c.cam.fwd);
        if (cosX > 0.0f && z > 0.0f) {
            const float nx = -dot(wc, c.cam.side) / (z * c.cam.tanHalf * c.cam.aspect), ny = -dot(wc, c.cam.up) / (z * c.cam.tanHalf);
            const float fx = (nx * 0.5f + 0.5f) * float(c.d.width), fy = (0.5f - ny * 0.5f) * float(c.d.height);
            if (fx >= 0.0f && fy >= 0.0f && fx < float(c.d.width) && fy < float(c.d.height) &&
                !occluded(c, offsetP(s.p, ng, wc), wc, dist * (1.0f - 1e-3f))) {
                const float cs = (c.flags & F_DROPCOS) != 0u ? 1.0f : cosX;
                const double v0 = double(beta) * (rho / kPi) * cs / (double(dist) * dist * double(z) * z * z * apix) * invTotal;
                const std::size_t px = std::size_t(int(fy)) * std::size_t(c.d.width) + std::size_t(int(fx));
                for (std::uint32_t ch = 0; ch < 3; ++ch) img[px * 3 + ch] += v0 * tableAt(c.T(), kCmf, 3, ch, lambda);
            }
        }
        const Vec3 wi = cosineDir(ng, u.a, u.b);
        beta *= rho;
        if (b >= 3) {
            const float qq = std::min(0.95f, rho);
            if (v.b >= qq) break;
            beta /= qq;
        }
        ro = offsetP(s.p, ng, wi); rd = wi;
    }
}
template<class F> void parallel(int threads, std::uint32_t n, F&& f) {
    std::atomic<std::uint32_t> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] { for (std::uint32_t i; (i = next.fetch_add(1)) < n;) f(t, i); });
    for (auto& th : pool) th.join();
}
void buildBvh(Ctx& c) {
    std::vector<Tri> tris(c.s.material.size());
    for (std::size_t t = 0; t < tris.size(); ++t) {
        const float* v = &c.s.tris[t * 9];
        tris[t] = {{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, {v[6], v[7], v[8]}};
    }
    c.bvh.build(std::move(tris));
}
}  // namespace

int causticGlassPixels(const CausticScene& s, int w, int h) {
    CausticDesc d;
    d.width = w; d.height = h;
    Ctx c{s, d, pack(s)};
    buildBvh(c);
    c.cam = camOf(s.camera, w, h);
    int n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Surf q = closest(c, c.cam.eye, camDir(c.cam, float(x) + 0.5f, float(y) + 0.5f, w, h));
            n += q.hit && kindOf(c, q.mat) == 1.0f;
        }
    return n;
}
CausticImage causticCpu(const CausticScene& s, const CausticDesc& d, int threads) {
    CausticImage im;
    im.width = d.width; im.height = d.height;
    Ctx c{s, d, pack(s)};
    buildBvh(c);
    c.cam = camOf(s.camera, d.width, d.height);
    c.flags = (d.split ? F_SPLIT : 0u) | (d.controls.noDispersion ? F_NODISP : 0u) | (d.controls.dropSplatCosine ? F_DROPCOS : 0u);
    c.seedL = d.seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    const std::size_t npx = std::size_t(d.width) * std::size_t(d.height);
    im.cameraXyz.assign(npx * 3, 0.0);
    parallel(threads, std::uint32_t(d.height), [&](int, std::uint32_t y) {
        for (int x = 0; x < d.width; ++x) {
            const std::uint32_t pix = y * std::uint32_t(d.width) + std::uint32_t(x);
            for (std::uint32_t k = 0; k < d.spp; ++k) {
                float lambda = 0;
                const double L = cameraSample(c, pix, x, int(y), k, lambda) * double(kLambdaSpan) / double(d.spp);
                for (std::uint32_t ch = 0; ch < 3; ++ch) im.cameraXyz[pix * 3 + ch] += L * tableAt(c.T(), kCmf, 3, ch, lambda);
            }
        }
    });
    im.lightXyz.assign(npx * 3, 0.0);
    if (d.split && d.photonsPerPixel > 0) {
        const std::uint64_t total = std::uint64_t(npx) * d.photonsPerPixel;
        const int lt = int(std::max<std::size_t>(1, std::min<std::size_t>(std::size_t(threads), (std::size_t(1) << 29) / (npx * 24))));
        std::vector<std::vector<double>> part(std::size_t(lt), std::vector<double>(npx * 3, 0.0));
        const std::uint32_t chunk = 4096, chunks = std::uint32_t((total + chunk - 1) / chunk);
        parallel(lt, chunks, [&](int t, std::uint32_t i) {
            const std::uint64_t e = std::min<std::uint64_t>(total, std::uint64_t(i + 1) * chunk);
            for (std::uint64_t k = std::uint64_t(i) * chunk; k < e; ++k) lightPath(c, std::uint32_t(k % npx), std::uint32_t(k / npx), 1.0 / double(total), part[std::size_t(t)]);
        });
        for (const auto& p : part) for (std::size_t q = 0; q < p.size(); ++q) im.lightXyz[q] += p[q];
    }
    im.xyz.resize(npx * 3);
    for (std::size_t q = 0; q < im.xyz.size(); ++q) im.xyz[q] = im.cameraXyz[q] + im.lightXyz[q];
    return im;
}

}  // namespace raw::rt
