#pragma once
// Private to the art scenes (art_scene_trace.cpp, art_scene_town.cpp): the analytic primitives'
// nearest-hit tests in float64, shared so both scenes intersect the same way.
#include "raw/renderer/art_scene.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art::prims {

struct Box { D3 lo, hi; int obj, mat; D3 albedo; D3 emit{0, 0, 0}; };
struct Sphere { D3 c; double r; int obj, mat; D3 albedo; D3 emit{0, 0, 0}; };
struct Cylinder { double x, z, r, y0, y1; int obj, mat; D3 albedo; };

inline double dot(const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

inline void consider(Hit& best, double t, const D3& n, int obj, int mat, const D3& albedo, const D3& emit = {0, 0, 0}) {
    if (t > 1e-9 && (best.t < 0 || t < best.t)) { best.t = t; best.n = n; best.obj = obj; best.mat = mat; best.albedo = albedo; best.emit = emit; }
}
inline void box(const Box& b, const D3& o, const D3& d, Hit& best) {
    double t0 = -1e300, t1 = 1e300;
    int axis = -1;
    double sign = 0;
    for (int k = 0; k < 3; ++k) {
        if (std::abs(d[std::size_t(k)]) < 1e-15) {
            if (o[std::size_t(k)] < b.lo[std::size_t(k)] || o[std::size_t(k)] > b.hi[std::size_t(k)]) return;
            continue;
        }
        const double inv = 1.0 / d[std::size_t(k)];
        double ta = (b.lo[std::size_t(k)] - o[std::size_t(k)]) * inv, tb = (b.hi[std::size_t(k)] - o[std::size_t(k)]) * inv;
        double s = -1;
        if (ta > tb) { std::swap(ta, tb); s = 1; }
        if (ta > t0) { t0 = ta; axis = k; sign = s; }
        t1 = std::min(t1, tb);
        if (t0 > t1) return;
    }
    if (axis < 0 || t0 <= 1e-9) return;
    D3 n{0, 0, 0};
    n[std::size_t(axis)] = sign;
    consider(best, t0, n, b.obj, b.mat, b.albedo, b.emit);
}
inline void sphere(const Sphere& s, const D3& o, const D3& d, Hit& best) {
    const D3 q{o[0] - s.c[0], o[1] - s.c[1], o[2] - s.c[2]};
    const double b = dot(q, d), c = dot(q, q) - s.r * s.r, disc = b * b - c;
    if (disc < 0) return;
    const double sq = std::sqrt(disc);
    double t = -b - sq;
    if (t <= 1e-9) t = -b + sq;
    if (t <= 1e-9) return;
    const D3 n{(q[0] + t * d[0]) / s.r, (q[1] + t * d[1]) / s.r, (q[2] + t * d[2]) / s.r};
    consider(best, t, n, s.obj, s.mat, s.albedo, s.emit);
}
inline void cylinder(const Cylinder& c, const D3& o, const D3& d, Hit& best) {
    const double qx = o[0] - c.x, qz = o[2] - c.z;
    const double a = d[0] * d[0] + d[2] * d[2], b = qx * d[0] + qz * d[2], cc = qx * qx + qz * qz - c.r * c.r;
    if (a > 1e-15) {
        const double disc = b * b - a * cc;
        if (disc >= 0) {
            const double sq = std::sqrt(disc);
            for (double t : {(-b - sq) / a, (-b + sq) / a}) {
                const double y = o[1] + t * d[1];
                if (t > 1e-9 && y >= c.y0 && y <= c.y1) { consider(best, t, {(qx + t * d[0]) / c.r, 0, (qz + t * d[2]) / c.r}, c.obj, c.mat, c.albedo); break; }
            }
        }
    }
    if (std::abs(d[1]) > 1e-15) {
        const double t = (c.y1 - o[1]) / d[1], x = qx + t * d[0], z = qz + t * d[2];
        if (x * x + z * z <= c.r * c.r) consider(best, t, {0, 1, 0}, c.obj, c.mat, c.albedo);
    }
}

}  // namespace raw::art::prims
