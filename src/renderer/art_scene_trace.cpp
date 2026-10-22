// The art scene's ray caster: see raw/renderer/art_scene.hpp. Six analytic primitives, nearest hit
// in float64.
#include "raw/renderer/art_scene.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
struct Box { D3 lo, hi; int obj, mat; D3 albedo; };
struct Sphere { D3 c; double r; int obj, mat; D3 albedo; };
struct Cylinder { double x, z, r, y0, y1; int obj, mat; D3 albedo; };

constexpr Box kWall{{-6.0, 0.0, -4.6}, {6.0, 4.0, -4.0}, 2, 4, {0.58, 0.36, 0.27}};
constexpr Box kPlinth{{-1.6, 0.0, -1.2}, {1.6, 0.9, 1.2}, 3, 2, {0.72, 0.70, 0.65}};
constexpr Sphere kBig{{0.0, 1.75, 0.0}, 0.85, 4, 3, {0.86, 0.83, 0.77}};
constexpr Sphere kSmall{{2.6, 0.6, 1.0}, 0.6, 5, 5, {0.28, 0.40, 0.62}};
constexpr Cylinder kColumn{-2.8, -1.4, 0.45, 0.0, 3.2, 6, 2, {0.66, 0.64, 0.60}};
constexpr double kGroundHalf = 30.0;
constexpr D3 kGroundAlbedo{0.34, 0.32, 0.26};

double dot(const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void consider(Hit& best, double t, const D3& n, int obj, int mat, const D3& albedo) {
    if (t > 1e-9 && (best.t < 0 || t < best.t)) { best.t = t; best.n = n; best.obj = obj; best.mat = mat; best.albedo = albedo; }
}
void ground(const D3& o, const D3& d, Hit& best) {
    if (std::abs(d[1]) < 1e-15) return;
    const double t = -o[1] / d[1];
    const double x = o[0] + t * d[0], z = o[2] + t * d[2];
    if (std::abs(x) <= kGroundHalf && std::abs(z) <= kGroundHalf) consider(best, t, {0, 1, 0}, 1, 1, kGroundAlbedo);
}
void box(const Box& b, const D3& o, const D3& d, Hit& best) {
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
    consider(best, t0, n, b.obj, b.mat, b.albedo);
}
void sphere(const Sphere& s, const D3& o, const D3& d, Hit& best) {
    const D3 q{o[0] - s.c[0], o[1] - s.c[1], o[2] - s.c[2]};
    const double b = dot(q, d), c = dot(q, q) - s.r * s.r, disc = b * b - c;
    if (disc < 0) return;
    const double sq = std::sqrt(disc);
    double t = -b - sq;
    if (t <= 1e-9) t = -b + sq;
    if (t <= 1e-9) return;
    const D3 n{(q[0] + t * d[0]) / s.r, (q[1] + t * d[1]) / s.r, (q[2] + t * d[2]) / s.r};
    consider(best, t, n, s.obj, s.mat, s.albedo);
}
void cylinder(const Cylinder& c, const D3& o, const D3& d, Hit& best) {
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
}  // namespace

Hit trace(const D3& o, const D3& d) {
    Hit best;
    ground(o, d, best);
    box(kWall, o, d, best);
    box(kPlinth, o, d, best);
    sphere(kBig, o, d, best);
    sphere(kSmall, o, d, best);
    cylinder(kColumn, o, d, best);
    return best;
}

bool occluded(const D3& o, const D3& d, double tMax) {
    const Hit h = trace(o, d);
    return h.t > 0 && h.t < tMax;
}

}  // namespace raw::art
