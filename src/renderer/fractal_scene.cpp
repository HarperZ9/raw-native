// The courtyard scene: see raw/renderer/fractal_scene.hpp.
#include "raw/renderer/fractal_scene.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace raw::fractal {
namespace {
using V3 = style::V3;
V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 add(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 mul(const V3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
V3 unit(const V3& a) { return mul(a, 1 / std::sqrt(dot(a, a))); }

constexpr double kHalf = 18, kWallH = 14;
struct Hit { double t{1e30}; V3 n{0, 1, 0}; int id{0}; };
void take(Hit& h, double t, const V3& n, int id) { if (t > 1e-6 && t < h.t) h = {t, n, id}; }

void sphere(Hit& h, const V3& o, const V3& d, const V3& c, double r, int id) {
    const V3 oc = sub(o, c);
    const double b = dot(oc, d), q = dot(oc, oc) - r * r, disc = b * b - q;
    if (disc < 0) return;
    const double s = std::sqrt(disc), t = (-b - s) > 1e-6 ? -b - s : -b + s;
    if (t > 1e-6 && t < h.t) take(h, t, unit(sub(add(o, mul(d, t)), c)), id);
}
void box(Hit& h, const V3& o, const V3& d, const V3& lo, const V3& hi, int id) {
    double t0 = 1e-6, t1 = 1e30;
    int axis = -1;
    double sgn = 1;
    for (std::size_t a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 1e-12) { if (o[a] < lo[a] || o[a] > hi[a]) return; continue; }
        double ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a], s = -1;
        if (ta > tb) { std::swap(ta, tb); s = 1; }
        if (ta > t0) { t0 = ta; axis = int(a); sgn = s; }
        t1 = std::min(t1, tb);
        if (t0 > t1) return;
    }
    if (axis < 0) return;
    V3 n{0, 0, 0};
    n[std::size_t(axis)] = sgn;
    take(h, t0, n, id);
}
void column(Hit& h, const V3& o, const V3& d, double cx, double cz, double r, double y1, int id) {
    const double ox = o[0] - cx, oz = o[2] - cz, a = d[0] * d[0] + d[2] * d[2], b = ox * d[0] + oz * d[2], c = ox * ox + oz * oz - r * r;
    const double disc = b * b - a * c;
    if (a > 1e-12 && disc >= 0) {
        for (double t : {(-b - std::sqrt(disc)) / a, (-b + std::sqrt(disc)) / a}) {
            const double y = o[1] + d[1] * t;
            if (t > 1e-6 && y >= 0 && y <= y1) { take(h, t, unit({ox + d[0] * t, 0, oz + d[2] * t}), id); break; }
        }
    }
    if (std::abs(d[1]) > 1e-12) {
        const double t = (y1 - o[1]) / d[1], x = ox + d[0] * t, z = oz + d[2] * t;
        if (t > 1e-6 && x * x + z * z <= r * r) take(h, t, {0, 1, 0}, id);
    }
}
void wall(Hit& h, const V3& o, const V3& d, std::size_t axis, double at, double sign, int id) {
    if (std::abs(d[axis]) < 1e-12) return;
    const double t = (at - o[axis]) / d[axis];
    if (t <= 1e-6 || t >= h.t) return;
    const V3 p = add(o, mul(d, t));
    if (p[1] < 0 || p[1] > kWallH || std::abs(p[0]) > kHalf + 1e-9 || std::abs(p[2]) > kHalf + 1e-9) return;
    V3 n{0, 0, 0};
    n[axis] = sign;
    take(h, t, n, id);
}
Hit cast(const V3& o, const V3& d, bool occludersOnly = false) {
    Hit h;
    box(h, o, d, {-1.5, 0, -1.5}, {1.5, 0.8, 1.5}, 6);
    sphere(h, o, d, {0, 2.0, 0}, 1.2, 7);
    sphere(h, o, d, {3.2, 0.6, 1.5}, 0.6, 8);
    column(h, o, d, -4, -2, 0.7, 5, 9);
    if (occludersOnly) return h;
    if (d[1] < 0) {
        const double t = -o[1] / d[1];
        if (t > 1e-6 && t < h.t && std::abs(o[0] + d[0] * t) < kHalf && std::abs(o[2] + d[2] * t) < kHalf) take(h, t, {0, 1, 0}, 1);
    }
    wall(h, o, d, 0, kHalf, -1, 2); wall(h, o, d, 0, -kHalf, 1, 3);
    wall(h, o, d, 2, kHalf, -1, 4); wall(h, o, d, 2, -kHalf, 1, 5);
    return h;
}
double baseAlbedo(int id) {
    static const double a[10] = {0.9, 0.75, 0.6, 0.6, 0.55, 0.55, 0.7, 0.8, 0.5, 0.85};
    return a[id];
}
struct Basis { V3 fwd, right, up; double tanH, aspect; };
Basis basis(const Camera& c, int w, int h) {
    Basis b;
    b.fwd = unit(sub(c.target, c.eye));
    b.right = unit(cross(b.fwd, {0, 1, 0}));
    b.up = cross(b.right, b.fwd);
    b.tanH = std::tan(c.fov / 2);
    b.aspect = double(w) / double(h);
    return b;
}
}  // namespace

Camera orbitCamera(int frame, double radius) {
    const double a = (20.0 + frame) * std::numbers::pi / 180;
    return {{radius * std::cos(a), 4.2, radius * std::sin(a)}, {0, 1, 0}, 0.75};
}

std::vector<style::DitherLight> lights() {
    return {style::makeRadial({3, 6, 2}, 22, 2.5, 1.6), style::makeRadial({-6, 4, -5}, 16, 3, 1.0)};
}

Frame renderGBuffer(const Camera& cam, int w, int h) {
    Frame f;
    f.w = w; f.h = h; f.cam = cam;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    style::DitherGBuffer& g = f.g;
    g.w = w; g.h = h;
    g.albedo.assign(n, 0.9f); g.depth.assign(n, 1000.0f); g.kind.assign(n, 0);
    for (auto* v : {&g.wpos, &g.nrm, &g.apos, &g.anrm}) v->assign(n * 3, 0.0f);
    g.vis.assign(n * 4, 0.0f);
    f.id.assign(n, 0);
    const Basis b = basis(cam, w, h);
    const std::vector<style::DitherLight> L = lights();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            const double u = (x + 0.5) / w * 2 - 1, v = 1 - (y + 0.5) / h * 2;
            const V3 d = unit(add(b.fwd, add(mul(b.right, u * b.tanH * b.aspect), mul(b.up, v * b.tanH))));
            const Hit hit = cast(cam.eye, d);
            if (hit.id == 0) continue;
            const V3 P = add(cam.eye, mul(d, hit.t));
            const double wob = 0.75 + 0.25 * std::sin(0.7 * P[0]) * std::sin(0.9 * P[2] + 0.5 * P[1]);
            g.kind[i] = 2; f.id[i] = std::uint8_t(hit.id);
            g.albedo[i] = float(baseAlbedo(hit.id) * wob);
            g.depth[i] = float(dot(sub(P, cam.eye), b.fwd));
            for (std::size_t c = 0; c < 3; ++c) { g.wpos[i * 3 + c] = g.apos[i * 3 + c] = float(P[c]); g.nrm[i * 3 + c] = g.anrm[i * 3 + c] = float(hit.n[c]); }
            for (std::size_t k = 0; k < L.size(); ++k) {
                const V3 to = sub(L[k].pos, P);
                const double dist = std::sqrt(dot(to, to));
                const Hit s = cast(add(P, mul(hit.n, 1e-4)), mul(to, 1 / dist), true);
                g.vis[i * 4 + k] = s.t < dist ? 0.0f : 1.0f;
            }
        }
    return f;
}

std::vector<Frame> orbitFrames(int frames, int w, int h) {
    std::vector<Frame> f;
    for (int t = 0; t < frames; ++t) f.push_back(renderGBuffer(orbitCamera(t), w, h));
    return f;
}
style::DitherPlan orbitPlan(const std::string& preset, style::Anchor anchor, int h, double px) {
    style::DitherPlan p = style::ditherPreset(preset, h, orbitCamera(0).fov);
    p.anchor = anchor; p.px = px; p.cell = 0.1;
    return p;
}

Motion motion(const Frame& cur, const Frame& prev) {
    Motion m;
    const std::size_t n = std::size_t(cur.w) * std::size_t(cur.h);
    m.mv.assign(n * 2, 0.0f); m.valid.assign(n, 0);
    const Basis b = basis(prev.cam, cur.w, cur.h);
    for (int y = 0; y < cur.h; ++y)
        for (int x = 0; x < cur.w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(cur.w) + std::size_t(x);
            if (!cur.id[i]) continue;
            const V3 P{cur.g.wpos[i * 3], cur.g.wpos[i * 3 + 1], cur.g.wpos[i * 3 + 2]};
            const V3 rel = sub(P, prev.cam.eye);
            const double z = dot(rel, b.fwd);
            if (z <= 1e-6) continue;
            const double px = (dot(rel, b.right) / (z * b.tanH * b.aspect) * 0.5 + 0.5) * cur.w - 0.5;
            const double py = (0.5 - dot(rel, b.up) / (z * b.tanH) * 0.5) * cur.h - 0.5;
            m.mv[i * 2] = float(x - px); m.mv[i * 2 + 1] = float(y - py);
            const int qx = int(std::floor(px + 0.5)), qy = int(std::floor(py + 0.5));
            if (qx < 0 || qy < 0 || qx >= cur.w || qy >= cur.h) continue;
            const std::size_t q = std::size_t(qy) * std::size_t(cur.w) + std::size_t(qx);
            m.valid[i] = prev.id[q] == cur.id[i] && std::abs(double(prev.g.depth[q]) - z) <= 0.02 * z;
        }
    return m;
}

}  // namespace raw::fractal
