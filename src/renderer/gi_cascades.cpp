// The GI reference's merge, spherical harmonics, gather and driver: see
// raw/renderer/gi_cascades.hpp. gi.wgsl's gi_merge, gi_sh and gi_gather are the GPU twins.
#include "gi_internal.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
namespace raw::gi {
namespace detail {
namespace {
struct Corner { std::uint32_t slot; double w; };
// The probes of cascade C with bucket b at the eight cells around q, weights trilinear and
// renormalised over the ones that exist.
int corners(const Cascade& C, Vec3 q, std::uint32_t b, Corner out[8]){
    const Grid& G = C.grid;
    const double g[3] = {(double(q.x) - G.origin.x) / G.cell - 0.5, (double(q.y) - G.origin.y) / G.cell - 0.5,
                         (double(q.z) - G.origin.z) / G.cell - 0.5};
    const std::uint32_t n[3] = {G.nx, G.ny, G.nz};
    double i0[3], fr[3];
    for (int a = 0; a < 3; ++a){ i0[a] = std::floor(g[a]); fr[a] = g[a] - i0[a]; }
    int k = 0;
    double sum = 0.0;
    for (int c = 0; c < 8; ++c){
        double w = 1.0;
        std::int64_t ix[3];
        bool inside = true;
        for (int a = 0; a < 3; ++a){
            const int d = (c >> a) & 1;
            ix[a] = std::int64_t(i0[a]) + d;
            inside = inside && ix[a] >= 0 && ix[a] < std::int64_t(n[a]);
            w *= d ? fr[a] : 1.0 - fr[a];
        }
        if (!inside || w <= 0.0) continue;
        const std::uint64_t key = ((std::uint64_t(ix[2]) * G.ny + std::uint64_t(ix[1])) * G.nx + std::uint64_t(ix[0])) * 6u + b;
        const std::uint32_t slot = C.slotOfKey[std::size_t(key)];
        if (slot == kNone) continue;
        out[k++] = {slot, w}; sum += w;
    }
    for (int i = 0; i < k; ++i) out[i].w /= sum;
    return k;
}
// The merged radiance of cascade N in the direction of bin (i, j) of a coarser-angle cascade: the
// mean of its four children, interpolated over the corners.
void farOf(const Cascade& N, const Corner* cr, int nc, std::uint32_t i, std::uint32_t j, double out[3]){
    out[0] = out[1] = out[2] = 0.0;
    const std::size_t bins = std::size_t(N.bins) * N.bins;
    for (int k = 0; k < nc; ++k)
        for (std::uint32_t q = 0; q < 4; ++q){
            const std::size_t bin = std::size_t(2 * j + (q >> 1)) * N.bins + (2 * i + (q & 1));
            const double* m = &N.merged[(std::size_t(cr[k].slot) * bins + bin) * 3];
            for (int c = 0; c < 3; ++c) out[c] += 0.25 * cr[k].w * m[c];
        }
}
void mergeOne(Cascade& C, const Cascade* N, std::size_t s){
    const std::uint32_t key = C.keyOfSlot[s];
    Corner cr[8];
    const int nc = N ? corners(*N, C.grid.centre(key), key % 6u, cr) : 0;
    const std::size_t bins = std::size_t(C.bins) * C.bins;
    for (std::uint32_t j = 0; j < C.bins; ++j)
        for (std::uint32_t i = 0; i < C.bins; ++i){
            double far[3] = {0.0, 0.0, 0.0};
            if (N) farOf(*N, cr, nc, i, j, far);
            const std::size_t b = s * bins + std::size_t(j) * C.bins + i;
            const std::uint32_t* a = &C.acc[b * 5];
            for (int c = 0; c < 3; ++c){
                double r = far[c];
                if (a[0]) r = double(a[2 + c]) / kFixed / double(a[0]) + (1.0 - double(a[1]) / double(a[0])) * far[c];
                C.merged[b * 3 + std::size_t(c)] = r;
            }
        }
}
// Real spherical harmonics up to l = 2 at a unit direction.
void shBasis(double x, double y, double z, double Y[9]){
    Y[0] = 0.282095;
    Y[1] = 0.488603 * y; Y[2] = 0.488603 * z; Y[3] = 0.488603 * x;
    Y[4] = 1.092548 * x * y; Y[5] = 1.092548 * y * z; Y[6] = 0.315392 * (3.0 * z * z - 1.0);
    Y[7] = 1.092548 * x * z; Y[8] = 0.546274 * (x * x - y * y);
}
}  // namespace

void mergeCascades(std::vector<Cascade>& cs){
    for (int c = kCascades - 1; c >= 0; --c){
        Cascade& C = cs[std::size_t(c)];
        const Cascade* N = c + 1 < kCascades ? &cs[std::size_t(c + 1)] : nullptr;
        for (std::size_t s = 0; s < C.keyOfSlot.size(); ++s) mergeOne(C, N, s);
    }
}

std::vector<SubDir> subDirections(std::uint32_t n){
    constexpr int kSub = 4;
    std::vector<SubDir> out;
    double total = 0.0;
    const double step = 2.0 / double(n * kSub);
    for (std::uint32_t b = 0; b < n * n; ++b)
        for (int k = 0; k < kSub * kSub; ++k){
            const double u = -1.0 + (double((b % n) * kSub + std::uint32_t(k % kSub)) + 0.5) * step;
            const double v = -1.0 + (double((b / n) * kSub + std::uint32_t(k / kSub)) + 0.5) * step;
            double x = u, y = v, z = 1.0 - std::fabs(u) - std::fabs(v);
            if (z < 0.0){ x = (1.0 - std::fabs(v)) * (u < 0.0 ? -1.0 : 1.0); y = (1.0 - std::fabs(u)) * (v < 0.0 ? -1.0 : 1.0); }
            const double l = std::sqrt(x * x + y * y + z * z), w = 1.0 / (l * l * l);
            out.push_back({b, x / l, y / l, z / l, w});
            total += w;
        }
    for (SubDir& d : out) d.w *= 4.0 * 3.14159265358979323846 / total;
    return out;
}

std::vector<double> shOf(const Cascade& c0){
    const std::vector<SubDir> sub = subDirections(c0.bins);
    const std::size_t bins = std::size_t(c0.bins) * c0.bins;
    std::vector<double> sh(c0.keyOfSlot.size() * 27, 0.0);
    for (std::size_t s = 0; s < c0.keyOfSlot.size(); ++s)
        for (const SubDir& d : sub){
            double Y[9];
            shBasis(d.x, d.y, d.z, Y);
            const double* m = &c0.merged[(s * bins + d.bin) * 3];
            for (int l = 0; l < 9; ++l)
                for (int c = 0; c < 3; ++c) sh[s * 27 + std::size_t(l) * 3 + std::size_t(c)] += m[c] * Y[l] * d.w;
        }
    return sh;
}

void gather(const GBuffer& g, const Cascade& c0, const std::vector<double>& sh, std::vector<double>& out, int threads){
    // Ramamoorthi and Hanrahan (2001): irradiance = sum A_l L_lm Y_lm(n); I = irradiance / pi.
    const double A[9] = {1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25};
    out.assign(std::size_t(g.w) * std::size_t(g.h) * 3, 0.0);
    parallelRows(g.h, threads, [&](int y){
        for (int x = 0; x < g.w; ++x){
            if (!g.mask.at(x, y)) continue;
            const Vec3 n = g.normal.at(x, y);
            Corner cr[8];
            const int nc = corners(c0, g.position.at(x, y), bucketOf(n), cr);
            double Y[9];
            shBasis(n.x, n.y, n.z, Y);
            double* o = &out[(std::size_t(y) * std::size_t(g.w) + std::size_t(x)) * 3];
            for (int k = 0; k < nc; ++k)
                for (int l = 0; l < 9; ++l)
                    for (int c = 0; c < 3; ++c) o[c] += cr[k].w * A[l] * Y[l] * sh[std::size_t(cr[k].slot) * 27 + std::size_t(l) * 3 + std::size_t(c)];
            for (int c = 0; c < 3; ++c) o[c] = std::max(0.0, o[c]);
        }
    });
}
}  // namespace detail

Result cascadesCpu(const Scene3& s, const GBuffer& g, const Params& p, int threads){
    Result r;
    if (gridOf(s, std::min(p.cacheCell, p.cell0)).keys() > (1ull << 26)){ r.error = "the scene's box is too large for the dense GI tables"; return r; }
    detail::Cache cache = detail::cacheOf(s, p);
    std::vector<Cascade> cs = probesOf(s, g, p);
    std::vector<detail::RayRec> rays;
    for (int f = 0; f < p.frames; ++f){
        detail::traceFrame(s, g, p, f, cache, rays, threads);
        detail::resolveCache(cache);
        if (f >= p.warmup) detail::splitRays(g, p, cache, rays, cs, threads);
        for (const detail::RayRec& x : rays) r.raysTraced += g.mask.at(int(x.px), int(x.py)) ? 1u : 0u;
    }
    detail::mergeCascades(cs);
    detail::gather(g, cs[0], detail::shOf(cs[0]), r.indirect64, threads);
    r.indirect.resize(std::size_t(g.w) * std::size_t(g.h));
    for (std::size_t i = 0; i < r.indirect.size(); ++i)
        r.indirect[i] = {float(r.indirect64[3 * i]), float(r.indirect64[3 * i + 1]), float(r.indirect64[3 * i + 2])};
    for (std::uint32_t seen : cache.seen) r.cacheEntries += seen ? 1u : 0u;
    for (int c = 0; c < kCascades; ++c) r.slots[c] = std::uint32_t(cs[std::size_t(c)].keyOfSlot.size());
    return r;
}

Buffer<Vec3> shadeModel3(const GBuffer& g, const Buffer<float>& sky, const Buffer<float>& sun, const raw::Scene& s,
                         const std::vector<Vec3>& indirect){
    Buffer<Vec3> img; img.resize(g.w, g.h);
    const Vec3 ldir = s.lights.empty() ? Vec3{0, -1, 0} : s.lights[0].dir;
    const float li = s.lights.empty() ? 1.0f : s.lights[0].intensity;
    for (int y = 0; y < g.h; ++y) for (int x = 0; x < g.w; ++x){
        if (!g.mask.at(x, y)){ img.at(x, y) = {0, 0, 0}; continue; }
        const Vec3 n = g.normal.at(x, y), a = g.albedo.at(x, y), I = indirect[std::size_t(y) * std::size_t(g.w) + std::size_t(x)];
        const float ndl = std::max(0.0f, dot(n, ldir * -1.0f)) * li;
        const float lit = 0.2f * sky.at(x, y) + ndl * sun.at(x, y);
        img.at(x, y) = {a.x * (lit + I.x), a.y * (lit + I.y), a.z * (lit + I.z)};
    }
    return img;
}

}  // namespace raw::gi
