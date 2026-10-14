// Screen-space mirror reflections and their ray-cast reference: see raw/renderer/post.hpp.
#include "raw/renderer/post.hpp"
#include "raw/renderer/bvh.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
namespace raw::post {
namespace {
int threads() { return int(std::max(1u, std::thread::hardware_concurrency())); }

// The reflected ray's screen segment: start and end pixel, and 1 / depth at both ends.
struct Segment { double sx0, sy0, sx1, sy1, k0, k1; double r[3]; };
bool segmentOf(const ViewGBuffer& v, std::size_t i, const SsrParams& prm, Segment& g) {
    const double P[3] = {v.pos[i * 3], v.pos[i * 3 + 1], v.pos[i * 3 + 2]};
    const double pl = std::sqrt(P[0] * P[0] + P[1] * P[1] + P[2] * P[2]);
    const double d[3] = {P[0] / pl, P[1] / pl, P[2] / pl};
    double N[3] = {v.nrm[i * 3], v.nrm[i * 3 + 1], v.nrm[i * 3 + 2]};
    if (prm.viewNormal) { N[0] = -d[0]; N[1] = -d[1]; N[2] = -d[2]; }
    const double dn = d[0] * N[0] + d[1] * N[1] + d[2] * N[2];
    for (int k = 0; k < 3; ++k) g.r[k] = d[k] - 2.0 * dn * N[k];
    double len = prm.maxDistance;
    if (g.r[2] > 1e-9) len = std::min(len, (-v.nearZ - P[2]) / g.r[2] * 0.999);
    if (len <= 0.0) return false;
    const double E[3] = {P[0] + g.r[0] * len, P[1] + g.r[1] * len, P[2] + g.r[2] * len};
    if (!project(v, P, g.sx0, g.sy0) || !project(v, E, g.sx1, g.sy1)) return false;
    g.k0 = -1.0 / P[2];
    g.k1 = -1.0 / E[2];
    // Clip the screen segment to the screen rectangle, so every step lands on screen.
    double tEnd = 1.0;
    const double lim = 1e-3;   // method note 7: survives float32 at the screen edge
    const auto clip = [&](double s0, double s1, double hi) {
        if (s1 > hi - lim && s1 != s0) tEnd = std::min(tEnd, (hi - lim - s0) / (s1 - s0));
        if (s1 < 0.0 && s1 != s0) tEnd = std::min(tEnd, (0.0 - s0) / (s1 - s0));
    };
    clip(g.sx0, g.sx1, double(v.w)); clip(g.sy0, g.sy1, double(v.h));
    if (tEnd <= 0.0) return false;
    g.sx1 = g.sx0 + (g.sx1 - g.sx0) * tEnd; g.sy1 = g.sy0 + (g.sy1 - g.sy0) * tEnd; g.k1 = g.k0 + (g.k1 - g.k0) * tEnd;
    return true;
}
// The pixel at parameter t: whether the ray is behind its surface, within the thickness, and
// inside the solid (behind by at least the thickness, not beyond the back depth).
struct Probe { int j; bool behind, within, inside; };
Probe probeAt(const ViewGBuffer& v, const Segment& g, double t, double thickness, bool useBack) {
    Probe p{-2, false, false, false};
    const int px = int(std::floor(g.sx0 + (g.sx1 - g.sx0) * t)), py = int(std::floor(g.sy0 + (g.sy1 - g.sy0) * t));
    if (px < 0 || py < 0 || px >= v.w || py >= v.h) return p;
    const std::size_t j = std::size_t(py) * v.w + px;
    p.j = int(j);
    const double D = v.depth[j];
    if (D < 0.0) return p;
    const double ray = 1.0 / (g.k0 + (g.k1 - g.k0) * t);
    p.behind = ray > D;
    p.within = p.behind && ray - D < thickness;
    p.inside = useBack && ray >= D + thickness && ray <= v.back[j];
    return p;
}
int marchOne(const ViewGBuffer& v, std::size_t i, const SsrParams& prm, bool useBack) {
    Segment g;
    if (!segmentOf(v, i, prm, g)) return -1;
    const int steps = prm.steps > 0 ? prm.steps
                                    : std::max(1, int(std::ceil(std::max(std::fabs(g.sx1 - g.sx0), std::fabs(g.sy1 - g.sy0)))));
    double prev = 0.0;
    for (int s = 1; s <= steps; ++s) {
        const double t = double(s) / steps;
        const Probe p = probeAt(v, g, t, prm.thickness, useBack);
        if (p.j == -2) break;
        if (p.behind && p.j != int(i)) {
            double lo = prev, hi = t;
            for (int b = 0; b < prm.refine; ++b) {
                const double mid = 0.5 * (lo + hi);
                (probeAt(v, g, mid, prm.thickness, useBack).behind ? hi : lo) = mid;
            }
            const Probe h = probeAt(v, g, hi, prm.thickness, useBack);
            if (h.within && h.j >= 0 && h.j != int(i)) return h.j;
            if (p.inside) return -1;   // hidden entry (method note 14)
        }
        prev = t;
    }
    return -1;
}
}  // namespace

void addBackLayer(ViewGBuffer& v, const Scene& s, const GBuffer& front) {
    RasterOptions ro;
    ro.perspectiveDepth = true;
    ro.peelAfter = &front.depth;
    Buffer<uint8_t> away;
    ro.backFacing = &away;
    const GBuffer peel = rasterize(s, v.w, v.h, nullptr, nullptr, ro);
    v.back = v.depth;
    for (int y = 0; y < v.h; ++y) for (int x = 0; x < v.w; ++x) {
        const std::size_t i = std::size_t(y) * v.w + x;
        if (v.depth[i] >= 0.0f && peel.mask.at(x, y) && away.at(x, y)) v.back[i] = peel.depth.at(x, y);
    }
}

// The reflected view ray is marched in screen space with 1 / depth interpolated, so each step's
// depth is perspective-correct; steps 0 takes one step a pixel of the segment clipped to the
// screen (method note 14). A step behind the depth buffer by less than the thickness is a hit,
// refined by bisection on "behind". With hiddenEntry and a back layer, a step inside a solid
// (behind its front by at least the thickness, not beyond its back face) ends the march as a
// miss: the ray entered that solid through a face the camera cannot see, so its hit is hidden.
std::vector<int> ssr(const ViewGBuffer& v, const SsrParams& prm) {
    std::vector<int> out(std::size_t(v.w) * v.h, -1);
    const bool useBack = prm.hiddenEntry && v.back.size() == v.depth.size();
    parallelRows(v.h, threads(), [&](int y) {
        for (int x = 0; x < v.w; ++x) {
            const std::size_t i = std::size_t(y) * v.w + x;
            if (v.depth[i] >= 0.0f) out[i] = marchOne(v, i, prm, useBack);
        }
    });
    return out;
}

void rayMirror(const Scene& s, const GBuffer& g, std::vector<Vec3>& at, std::vector<uint8_t>& hit) {
    std::vector<Tri> tris;
    for (const Mesh& m : s.meshes) for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
        tris.push_back({m.positions[std::size_t(m.indices[i])], m.positions[std::size_t(m.indices[i + 1])], m.positions[std::size_t(m.indices[i + 2])]});
    Bvh bvh; bvh.build(tris);
    const int w = g.depth.w, h = g.depth.h;
    at.assign(std::size_t(w) * h, Vec3{}); hit.assign(std::size_t(w) * h, 0);
    parallelRows(h, threads(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            if (!g.mask.at(x, y)) continue;
            const Vec3 p = g.position.at(x, y), n = normalize(g.normal.at(x, y)), d = normalize(p - s.camera.eye);
            const Vec3 r = d - n * (2.0f * dot(d, n));
            Hit hh;
            const std::size_t i = std::size_t(y) * w + x;
            if (bvh.closest({p + n * 1e-3f, r}, 1e30f, hh)) { at[i] = p + n * 1e-3f + r * hh.t; hit[i] = 1; }
        }
    });
}

}  // namespace raw::post
