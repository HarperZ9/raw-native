// Screen-space mirror reflections and their ray-cast reference: see raw/renderer/post.hpp.
#include "raw/renderer/post.hpp"
#include "raw/renderer/bvh.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
namespace raw::post {
namespace {
int threads() { return int(std::max(1u, std::thread::hardware_concurrency())); }
}  // namespace

// The reflected view ray, from the surface to at most maxDistance (stopping short of the near
// plane), is marched linearly in screen space with 1 / depth interpolated, so each step's depth
// is perspective-correct. A step behind the depth buffer by less than the thickness is a hit,
// refined by bisection on "behind".
std::vector<int> ssr(const ViewGBuffer& v, const SsrParams& prm) {
    std::vector<int> out(std::size_t(v.w) * v.h, -1);
    parallelRows(v.h, threads(), [&](int y) {
        for (int x = 0; x < v.w; ++x) {
            const std::size_t i = std::size_t(y) * v.w + x;
            if (v.depth[i] < 0.0f) continue;
            const double P[3] = {v.pos[i * 3], v.pos[i * 3 + 1], v.pos[i * 3 + 2]};
            const double pl = std::sqrt(P[0] * P[0] + P[1] * P[1] + P[2] * P[2]);
            const double d[3] = {P[0] / pl, P[1] / pl, P[2] / pl};
            double N[3] = {v.nrm[i * 3], v.nrm[i * 3 + 1], v.nrm[i * 3 + 2]};
            if (prm.viewNormal) { N[0] = -d[0]; N[1] = -d[1]; N[2] = -d[2]; }
            const double dn = d[0] * N[0] + d[1] * N[1] + d[2] * N[2];
            const double r[3] = {d[0] - 2.0 * dn * N[0], d[1] - 2.0 * dn * N[1], d[2] - 2.0 * dn * N[2]};
            double len = prm.maxDistance;
            if (r[2] > 1e-9) len = std::min(len, (-v.nearZ - P[2]) / r[2] * 0.999);
            if (len <= 0.0) continue;
            const double E[3] = {P[0] + r[0] * len, P[1] + r[1] * len, P[2] + r[2] * len};
            double sx0, sy0, sx1, sy1;
            if (!project(v, P, sx0, sy0) || !project(v, E, sx1, sy1)) continue;
            const double k0 = -1.0 / P[2];
            double k1 = -1.0 / E[2];
            // Clip the screen segment to the screen rectangle, so every step lands on screen.
            double tEnd = 1.0;
            const double lim = 1e-3;   // method note 7: survives float32 at the screen edge
            const auto clip = [&](double s0, double s1, double hi) {
                if (s1 > hi - lim && s1 != s0) tEnd = std::min(tEnd, (hi - lim - s0) / (s1 - s0));
                if (s1 < 0.0 && s1 != s0) tEnd = std::min(tEnd, (0.0 - s0) / (s1 - s0));
            };
            clip(sx0, sx1, double(v.w)); clip(sy0, sy1, double(v.h));
            if (tEnd <= 0.0) continue;
            sx1 = sx0 + (sx1 - sx0) * tEnd; sy1 = sy0 + (sy1 - sy0) * tEnd; k1 = k0 + (k1 - k0) * tEnd;
            // The pixel at parameter t, whether the ray is behind its surface, and whether within the thickness.
            const auto probe = [&](double t, bool& behind, bool& within) {
                const int px = int(std::floor(sx0 + (sx1 - sx0) * t)), py = int(std::floor(sy0 + (sy1 - sy0) * t));
                behind = within = false;
                if (px < 0 || py < 0 || px >= v.w || py >= v.h) return -2;
                const std::size_t j = std::size_t(py) * v.w + px;
                const double D = v.depth[j];
                if (D < 0.0) return int(j);
                const double ray = 1.0 / (k0 + (k1 - k0) * t);
                behind = ray > D;
                within = behind && ray - D < prm.thickness;
                return int(j);
            };
            double prev = 0.0;
            for (int s = 1; s <= prm.steps; ++s) {
                const double t = double(s) / prm.steps;
                bool behind, within;
                const int j = probe(t, behind, within);
                if (j == -2) break;
                if (behind && j != int(i)) {
                    double lo = prev, hi = t;
                    for (int b = 0; b < prm.refine; ++b) {
                        const double mid = 0.5 * (lo + hi);
                        bool bm, wm;
                        probe(mid, bm, wm);
                        (bm ? hi : lo) = mid;
                    }
                    bool bh, wh;
                    const int k = probe(hi, bh, wh);
                    if (wh && k >= 0 && k != int(i)) { out[i] = k; break; }
                }
                prev = t;
            }
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
