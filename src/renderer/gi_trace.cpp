// The GI reference's rays and its bounce cache: see raw/renderer/gi_cascades.hpp and
// src/renderer/gi_internal.hpp. gi.wgsl's gi_trace, gi_resolve and gi_split are the GPU twins.
#include "gi_internal.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
namespace raw::gi::detail {
namespace {
struct HitInfo { bool hit{false}; float t{1e30f}; Vec3 p, n; std::uint32_t tri{0}; };
// The nearest hit, its point and its interpolated normal turned to face the ray's origin.
HitInfo closest(const Scene3& s, Vec3 o, Vec3 d){
    HitInfo r;
    Hit h;
    if (!rt::closest(s.tree, s.tris, Ray{o, d}, 1e30f, h)) return r;
    const std::size_t t = std::size_t(h.tri);
    const float w = 1.0f - h.u - h.v;
    Vec3 n = normalize(s.nrm[3 * t] * w + s.nrm[3 * t + 1] * h.u + s.nrm[3 * t + 2] * h.v);
    if (dot(n, d) > 0.0f) n = n * -1.0f;
    r.hit = true; r.t = h.t; r.p = o + d * h.t; r.n = n; r.tri = std::uint32_t(h.tri);
    return r;
}
void add(std::vector<std::uint32_t>& v, std::size_t i, std::uint32_t x){
    std::atomic_ref<std::uint32_t>(v[i]).fetch_add(x, std::memory_order_relaxed);
}
// One first hit's irradiance estimate (model 3's bracket): the sun through one shadow ray, plus
// the radiance one cosine bounce ray brings back (the sky on escape, albedo x E on a hit).
void estimate(const Scene3& s, const Params& p, const Cache& cache, const HitInfo& h1, std::uint32_t px,
              std::uint32_t py, int f, double est[3]){
    const Vec3 o = h1.p + h1.n * 1e-3f;
    double sunTerm = 0.0;
    const float ndl = dot(h1.n, s.toLight);
    if (ndl > 0.0f){
        Hit sh;
        if (!rt::closest(s.tree, s.tris, Ray{o, s.toLight}, 1e30f, sh)) sunTerm = double(s.li) * double(ndl);
    }
    const Vec3 d2 = cosineDir(h1.n, hash01(px, py, seedOf(f, 1, 0)), hash01(px, py, seedOf(f, 1, 1)));
    const HitInfo h2 = closest(s, o, d2);
    double lin[3] = {p.sky, p.sky, p.sky};
    if (h2.hit){
        const std::size_t k = cache.grid.key(h2.p, bucketOf(h2.n));
        const Vec3 a = s.albedo[h2.tri];
        lin[0] = double(a.x) * cache.E[3 * k]; lin[1] = double(a.y) * cache.E[3 * k + 1]; lin[2] = double(a.z) * cache.E[3 * k + 2];
    }
    for (int c = 0; c < 3; ++c) est[c] = lin[c] + sunTerm;
}
}  // namespace

Cache cacheOf(const Scene3& s, const Params& p){
    Cache c;
    c.grid = gridOf(s, p.cacheCell);
    const std::size_t n = std::size_t(c.grid.keys());
    c.sums.assign(4 * n, 0u); c.E.assign(3 * n, 0.0); c.seen.assign(n, 0u);
    return c;
}

void traceFrame(const Scene3& s, const GBuffer& g, const Params& p, int f, Cache& cache, std::vector<RayRec>& rays, int threads){
    const int rw = raysAcross(g.w, p.stride), rh = raysAcross(g.h, p.stride);
    rays.assign(std::size_t(rw) * std::size_t(rh), RayRec{});
    parallelRows(rh, threads, [&](int ry){
        for (int rx = 0; rx < rw; ++rx){
            RayRec& r = rays[std::size_t(ry) * std::size_t(rw) + std::size_t(rx)];
            r.px = std::uint32_t(rx * p.stride); r.py = std::uint32_t(ry * p.stride);
            if (!g.mask.at(int(r.px), int(r.py))) continue;
            const Vec3 n = g.normal.at(int(r.px), int(r.py));
            r.dir = cosineDir(n, hash01(r.px, r.py, seedOf(f, 0, 0)), hash01(r.px, r.py, seedOf(f, 0, 1)));
            const HitInfo h1 = closest(s, g.position.at(int(r.px), int(r.py)) + n * 1e-3f, r.dir);
            if (!h1.hit) continue;
            r.t = h1.t; r.key = cache.grid.key(h1.p, bucketOf(h1.n)); r.albedo = s.albedo[h1.tri];
            double est[3];
            estimate(s, p, cache, h1, r.px, r.py, f, est);
            for (int c = 0; c < 3; ++c) add(cache.sums, 4 * std::size_t(r.key) + std::size_t(c), quantise(est[c]));
            add(cache.sums, 4 * std::size_t(r.key) + 3, 1u);
        }
    });
}

void resolveCache(Cache& cache){
    for (std::size_t k = 0; k < cache.seen.size(); ++k){
        const std::uint32_t n = cache.sums[4 * k + 3];
        if (!n) continue;
        const double w = std::max(1.0 / double(cache.seen[k] + 1u), 0.125);
        for (int c = 0; c < 3; ++c){
            const double m = double(cache.sums[4 * k + std::size_t(c)]) / kFixed / double(n);
            cache.E[3 * k + std::size_t(c)] += w * (m - cache.E[3 * k + std::size_t(c)]);
            cache.sums[4 * k + std::size_t(c)] = 0u;
        }
        cache.sums[4 * k + 3] = 0u;
        ++cache.seen[k];
    }
}

void splitRays(const GBuffer& g, const Params& p, const Cache& cache, const std::vector<RayRec>& rays,
               std::vector<Cascade>& cs, int threads){
    const int rw = raysAcross(g.w, p.stride), rh = raysAcross(g.h, p.stride);
    parallelRows(rh, threads, [&](int ry){
        for (int rx = 0; rx < rw; ++rx){
            const RayRec& r = rays[std::size_t(ry) * std::size_t(rw) + std::size_t(rx)];
            if (!g.mask.at(int(r.px), int(r.py))) continue;
            std::uint32_t L[3] = {0u, 0u, 0u};
            if (r.key != kNone){
                const double a[3] = {r.albedo.x, r.albedo.y, r.albedo.z};
                for (int c = 0; c < 3; ++c) L[c] = quantise(a[c] * cache.E[3 * std::size_t(r.key) + std::size_t(c)]);
            }
            const Vec3 pos = g.position.at(int(r.px), int(r.py));
            const std::uint32_t b = bucketOf(g.normal.at(int(r.px), int(r.py)));
            for (int c = 0; c < kCascades; ++c){
                if (!(r.t >= p.radius[c])) break;
                Cascade& C = cs[std::size_t(c)];
                const std::uint32_t slot = C.slotOfKey[C.grid.key(pos, b)];
                const std::size_t at = (std::size_t(slot) * C.bins * C.bins + octBin(r.dir, C.bins)) * 5;
                add(C.acc, at, 1u);
                if (r.t < p.radius[c + 1]){
                    add(C.acc, at + 1, 1u);
                    for (int k = 0; k < 3; ++k) add(C.acc, at + 2 + std::size_t(k), L[k]);
                }
            }
        }
    });
}

}  // namespace raw::gi::detail
