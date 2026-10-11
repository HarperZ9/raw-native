#pragma once
// Coplanar overlap scan for the owned assets (evidence/m3-assets-bounds.json, method note 1 of
// 2026-10-11). Two triangles of an asset that lie in one plane, face the same way and overlap
// with positive area z-fight in every renderer. Triangles facing opposite ways in one plane are
// counted apart: those are the contact faces of solids that touch, seen by no camera outside them.
#include "raw/tools/owned_assets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>
#include <vector>
namespace raw::coplanar {

struct Result { long sameFacing{0}, oppositeFacing{0}; double sameArea{0}; };

namespace detail {
struct P2 { double x, y; };
struct Tri { P2 v[3]; double lo[2], hi[2]; int facing; const char* part; };
inline double cross(P2 o, P2 a, P2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
// Area of the intersection of two counter-clockwise triangles (Sutherland-Hodgman clipping).
inline double overlap(const Tri& a, const Tri& b) {
    std::vector<P2> poly(a.v, a.v + 3);
    for (int e = 0; e < 3 && !poly.empty(); ++e) {
        const P2 p = b.v[e], q = b.v[(e + 1) % 3];
        std::vector<P2> out;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const P2 s = poly[i], t = poly[(i + 1) % poly.size()];
            const double ds = cross(p, q, s), dt = cross(p, q, t);
            if (ds >= 0) out.push_back(s);
            if ((ds >= 0) != (dt >= 0)) { const double k = ds / (ds - dt); out.push_back({s.x + (t.x - s.x) * k, s.y + (t.y - s.y) * k}); }
        }
        poly.swap(out);
    }
    double area = 0;
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) area += cross(poly[0], poly[i], poly[i + 1]);
    return 0.5 * area;
}
}  // namespace detail

// Pairs of coplanar overlapping triangles (overlap above minArea square units) in the asset.
inline Result scan(const owned::Asset& a, double minArea = 1e-8) {
    using namespace detail;
    std::map<std::tuple<int, int, int, long long>, std::vector<Tri>> planes;
    for (const owned::Part& p : a.parts)
        for (std::size_t i = 0; i + 2 < p.indices.size(); i += 3) {
            const Vec3 A = p.positions[p.indices[i]], B = p.positions[p.indices[i + 1]], C = p.positions[p.indices[i + 2]];
            const double ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z, vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
            double n[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
            const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (l <= 0) continue;
            for (double& c : n) c /= l;
            int axis = 0;   // the dominant axis; the plane is keyed by the unsigned normal and offset
            for (int k = 1; k < 3; ++k) if (std::fabs(n[k]) > std::fabs(n[axis])) axis = k;
            const int facing = n[axis] > 0 ? 1 : -1;
            const double un[3] = {n[0] * facing, n[1] * facing, n[2] * facing}, d = un[0] * A.x + un[1] * A.y + un[2] * A.z;
            const auto key = std::make_tuple(int(std::lround(un[0] * 1e4)), int(std::lround(un[1] * 1e4)), int(std::lround(un[2] * 1e4)), (long long)std::llround(d * 1e5));
            const int a0 = (axis + 1) % 3, a1 = (axis + 2) % 3;
            const double P[3][3] = {{A.x, A.y, A.z}, {B.x, B.y, B.z}, {C.x, C.y, C.z}};
            Tri t;
            for (int k = 0; k < 3; ++k) t.v[k] = {P[k][a0], P[k][a1]};
            if (cross(t.v[0], t.v[1], t.v[2]) < 0) std::swap(t.v[1], t.v[2]);
            for (int c = 0; c < 2; ++c) {
                t.lo[c] = std::min({c ? t.v[0].y : t.v[0].x, c ? t.v[1].y : t.v[1].x, c ? t.v[2].y : t.v[2].x});
                t.hi[c] = std::max({c ? t.v[0].y : t.v[0].x, c ? t.v[1].y : t.v[1].x, c ? t.v[2].y : t.v[2].x});
            }
            t.facing = facing; t.part = p.name.c_str();
            planes[key].push_back(t);
        }
    Result r;
    for (auto& kv : planes) {
        std::vector<Tri>& ts = kv.second;
        std::sort(ts.begin(), ts.end(), [](const Tri& x, const Tri& y) { return x.lo[0] < y.lo[0]; });
        for (std::size_t i = 0; i < ts.size(); ++i)
            for (std::size_t j = i + 1; j < ts.size() && ts[j].lo[0] < ts[i].hi[0]; ++j) {
                if (ts[j].hi[1] <= ts[i].lo[1] || ts[j].lo[1] >= ts[i].hi[1]) continue;
                const double o = overlap(ts[i], ts[j]);
                if (o <= minArea) continue;
                if (ts[i].facing == ts[j].facing) {
                    ++r.sameFacing; r.sameArea += o;
                    if (std::getenv("RAW_NATIVE_COPLANAR_DUMP"))   // diagnosis
                        std::fprintf(stderr, "plane n %d %d %d d %lld: %s [%.3f %.3f] x [%.3f %.3f] and %s [%.3f %.3f] x [%.3f %.3f], overlap %.5f\n", std::get<0>(kv.first),
                                     std::get<1>(kv.first), std::get<2>(kv.first), std::get<3>(kv.first), ts[i].part, ts[i].lo[0], ts[i].hi[0], ts[i].lo[1], ts[i].hi[1],
                                     ts[j].part, ts[j].lo[0], ts[j].hi[0], ts[j].lo[1], ts[j].hi[1], o);
                }
                else ++r.oppositeFacing;
            }
    }
    return r;
}

}  // namespace raw::coplanar
