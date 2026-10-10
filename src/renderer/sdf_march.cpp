// Sphere tracing and the shading terms, float64: see raw/renderer/sdf.hpp.
#include "raw/renderer/sdf.hpp"
#include <algorithm>
#include <cmath>
namespace raw::sdf {
namespace {
D3 at(D3 o, D3 d, double t) { return {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t}; }
double dotd(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 unit(D3 a) { const double l = std::sqrt(dotd(a, a)); return {a.x / l, a.y / l, a.z / l}; }
// Method note 1 of evidence/rt-r3-bounds.json: bracket the surface by the sign of the distance
// and bisect, so the hit's error no longer grows as 1 / cos(theta) at grazing angles. Without a
// negative distance ahead (the Mandelbox estimator, tangent near-misses) t stays as marched.
double refine(const Program& p, D3 o, D3 d, double t, double e) {
    double t2 = t, step = e;
    bool inside = false;
    for (int k = 0; k < 64 && !inside; ++k) { t2 += step; inside = eval(p, at(o, d, t2)).d < 0.0; }       // method note 2
    for (int k = 0; k < 16 && !inside; ++k) { t2 += step; step *= 2.0; inside = eval(p, at(o, d, t2)).d < 0.0; }
    if (!inside) return t;
    double a = t, b = t2;
    for (int i = 0; i < 40; ++i) { const double m = 0.5 * (a + b); (eval(p, at(o, d, m)).d > 0.0 ? a : b) = m; }
    return 0.5 * (a + b);
}
}  // namespace

MarchHit march(const Program& p, D3 o, D3 d, double tMax, double eps, bool doRefine) {
    MarchHit h;
    const double step = p.fractal ? 0.6 : 1.0;
    double t = 0.0;
    for (int i = 0; i < 512; ++i) {
        h.steps = i + 1;
        const Sample s = eval(p, at(o, d, t));
        h.closest = std::min(h.closest, s.d / (1.0 + t));
        if (s.d < eps * (1.0 + t)) {
            h.hit = true; h.mat = s.mat;
            h.t = doRefine ? refine(p, o, d, t, eps * (1.0 + t)) : t;
            h.n = normal(p, at(o, d, h.t), h.t);
            return h;
        }
        t += s.d * step;
        if (t > tMax) break;
    }
    return h;
}

D3 normal(const Program& p, D3 x, double t) {
    const double e = 1e-4 * (1.0 + t);
    const double a = eval(p, {x.x + e, x.y - e, x.z - e}).d, b = eval(p, {x.x - e, x.y - e, x.z + e}).d;
    const double c = eval(p, {x.x - e, x.y + e, x.z - e}).d, d = eval(p, {x.x + e, x.y + e, x.z + e}).d;
    return unit({a - b - c + d, -a - b + c + d, -a + b - c + d});
}

double softShadow(const Program& p, D3 x, D3 l, double k) {
    double res = 1.0, t = 0.02;
    for (int i = 0; i < 128; ++i) {
        const double h = eval(p, at(x, l, t)).d;
        res = std::min(res, k * h / t);
        if (res < 1e-3) return 0.0;
        t += std::clamp(h, 0.01, 0.5);
        if (t > 30.0) break;
    }
    return std::clamp(res, 0.0, 1.0);
}

double ambientOcclusion(const Program& p, D3 x, D3 n) {
    double occ = 0.0, sca = 1.0;
    for (int i = 0; i < 5; ++i) {
        const double h = 0.02 + 0.12 * double(i);
        occ += (h - eval(p, at(x, n, h)).d) * sca;
        sca *= 0.85;
    }
    return std::clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

bool hardShadow(const Program& p, D3 x, D3 l) {
    return march(p, at(x, l, 0.02), l, 30.0).hit;
}

double fogHomogeneous(double sigma, double t) { return std::exp(-sigma * t); }
double fogHeight(double a, double b, D3 o, D3 d, double t) {
    const double base = a * std::exp(-b * o.y), k = b * d.y;
    const double optical = std::fabs(k) < 1e-9 ? base * t : base * (1.0 - std::exp(-k * t)) / k;
    return std::exp(-optical);
}
double fogHeightMarch(double a, double b, D3 o, D3 d, double t, int n) {
    const double dt = t / double(n);
    double optical = 0.0;
    for (int i = 0; i < n; ++i) optical += a * std::exp(-b * (o.y + d.y * (double(i) + 0.5) * dt)) * dt;
    return std::exp(-optical);
}

double henyeyGreenstein(double c, double g) {
    return (1.0 - g * g) / (4.0 * 3.14159265358979 * std::pow(1.0 + g * g - 2.0 * g * c, 1.5));
}
double godRays(const Scene& s, D3 o, D3 d, double tEnd, int& visible) {
    const D3 l = unit(d3(s.sunDir));
    const double dt = tEnd / 64.0, phase = henyeyGreenstein(dotd(d, l), 0.6);
    double L = 0.0;
    visible = 0;
    for (int i = 0; i < 64; ++i) {
        const double t = (double(i) + 0.5) * dt;
        const D3 x = at(o, d, t);
        if (hardShadow(s.prog, x, l)) continue;
        ++visible;
        L += s.fogA * std::exp(-s.fogB * x.y) * fogHeight(s.fogA, s.fogB, o, d, t) * phase * dt;
    }
    return L;
}

D3 cameraRay(const Scene& s, int x, int y, int w, int h) {
    const Vec3 f = normalize(s.target - s.eye), sd = normalize(cross(f, s.up)), u = cross(sd, f);
    const float th = std::tan(s.fovy * 0.5f), aspect = float(w) / float(h);
    const float nx = (float(x) + 0.5f) / float(w) * 2.0f - 1.0f, ny = 1.0f - (float(y) + 0.5f) / float(h) * 2.0f;
    return d3(normalize(f + sd * (nx * th * aspect) + u * (ny * th)));
}

}  // namespace raw::sdf
