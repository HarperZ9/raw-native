// The art scene's lighting, views, frames and motion: see raw/renderer/art_scene.hpp.
#include "raw/renderer/art_scene.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
constexpr double kPi = 3.141592653589793;
double dot(const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
D3 sub(const D3& a, const D3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
D3 cross(const D3& a, const D3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
D3 norm(const D3& a) { const double l = std::sqrt(dot(a, a)); return {a[0] / l, a[1] / l, a[2] / l}; }
struct Basis { D3 fwd, right, up; };
Basis basis(const View& v) {
    const D3 f = norm(sub(v.target, v.eye)), r = norm(cross(f, {0, 1, 0}));
    return {f, r, cross(r, f)};
}
D3 skyColour(const Lighting& L, const D3& d) {
    const double t = std::clamp(d[1] * 1.6 + 0.1, 0.0, 1.0);
    return {L.skyHorizon[0] + (L.skyTop[0] - L.skyHorizon[0]) * t, L.skyHorizon[1] + (L.skyTop[1] - L.skyHorizon[1]) * t,
            L.skyHorizon[2] + (L.skyTop[2] - L.skyHorizon[2]) * t};
}
struct Terms { double sun{0}, lamp{0}, amb{0}; };
Terms lightTerms(const Lighting& L, const D3& p, const D3& n) {
    Terms t;
    const D3 o{p[0] + n[0] * 1e-6, p[1] + n[1] * 1e-6, p[2] + n[2] * 1e-6};
    const double nl = dot(n, L.sun.dir);
    if (nl > 0 && !occluded(o, L.sun.dir, 1e30)) t.sun = nl;
    if (L.lamp.on) {
        const D3 toL = sub(L.lamp.pos, p);
        const double d2 = dot(toL, toL), d = std::sqrt(d2);
        const D3 l{toL[0] / d, toL[1] / d, toL[2] / d};
        const double nl2 = dot(n, l), cone = -dot(l, L.lamp.axis);
        const double s = std::clamp((cone - L.lamp.cosOuter) / (L.lamp.cosInner - L.lamp.cosOuter), 0.0, 1.0);
        if (nl2 > 0 && s > 0 && !occluded(o, l, d)) t.lamp = nl2 * s * s * (3 - 2 * s) / d2;
    }
    t.amb = 0.5 + 0.5 * n[1];
    return t;
}
D3 shade(const Lighting& L, const D3& o, const D3& d, const Hit& h) {
    if (h.t < 0) return skyColour(L, d);
    const D3 p{o[0] + h.t * d[0], o[1] + h.t * d[1], o[2] + h.t * d[2]};
    const Terms t = lightTerms(L, p, h.n);
    D3 c{};
    for (int k = 0; k < 3; ++k) {
        const std::size_t i = std::size_t(k);
        const double amb = L.ground[i] + (L.skyTop[i] - L.ground[i]) * t.amb;
        c[i] = h.albedo[i] * (L.sun.colour[i] * t.sun + L.lamp.colour[i] * t.lamp + amb);
    }
    return c;
}
void writePixel(Frame& f, const View& v, const Lighting& L, int x, int y) {
    const std::size_t p = std::size_t(y) * std::size_t(f.w) + std::size_t(x);
    D3 acc{0, 0, 0};
    for (int s = 0; s < 4; ++s) {
        const D3 d = rayDir(v, x + 0.25 + 0.5 * (s & 1), y + 0.25 + 0.5 * (s >> 1));
        const D3 c = shade(L, v.eye, d, trace(v.eye, d));
        for (int k = 0; k < 3; ++k) acc[std::size_t(k)] += c[std::size_t(k)] / 4;
    }
    for (int k = 0; k < 3; ++k) f.lit[p * 4 + std::size_t(k)] = float(acc[std::size_t(k)]);
    f.lit[p * 4 + 3] = 1.0f;
    const D3 d = rayDir(v, x + 0.5, y + 0.5);
    const Hit h = trace(v.eye, d);
    for (int k = 0; k < 3; ++k) f.dir[p * 3 + std::size_t(k)] = float(d[std::size_t(k)]);
    if (h.t < 0) { f.dist[p] = -1.0f; return; }
    const D3 P{v.eye[0] + h.t * d[0], v.eye[1] + h.t * d[1], v.eye[2] + h.t * d[2]};
    const Terms t = lightTerms(L, P, h.n);
    for (int k = 0; k < 3; ++k) {
        f.P[p * 3 + std::size_t(k)] = float(P[std::size_t(k)]);
        f.N[p * 3 + std::size_t(k)] = float(h.n[std::size_t(k)]);
        f.albedo[p * 3 + std::size_t(k)] = float(h.albedo[std::size_t(k)]);
    }
    f.dist[p] = float(h.t); f.sun[p] = float(t.sun); f.lamp[p] = float(t.lamp); f.amb[p] = float(t.amb);
    f.obj[p] = std::uint8_t(h.obj); f.mat[p] = std::uint8_t(h.mat);
}
}  // namespace

Lighting daylight() {
    return {{norm({-0.45, 0.78, 0.42}), {2.6, 2.45, 2.2}}, {}, {0.42, 0.55, 0.80}, {0.80, 0.82, 0.85}, {0.24, 0.21, 0.17}};
}
Lighting dusk() {
    Lamp lamp{{2.2, 3.6, 2.4}, norm({-2.2, -2.8, -2.4}), 0.848, 0.927, {6.0, 4.2, 2.4}, true};
    return {{norm({0.85, 0.18, -0.3}), {2.2, 1.1, 0.5}}, lamp, {0.18, 0.20, 0.38}, {0.95, 0.55, 0.35}, {0.12, 0.08, 0.06}};
}
Lighting night() {
    Lamp lamp{{2.2, 3.6, 2.4}, norm({-2.2, -2.8, -2.4}), 0.848, 0.927, {14.0, 10.0, 6.0}, true};
    return {{norm({0.3, 0.8, 0.5}), {0.05, 0.07, 0.12}}, lamp, {0.01, 0.015, 0.03}, {0.03, 0.035, 0.05}, {0.005, 0.005, 0.006}};
}

double View::focalPx() const { return 0.5 * h / std::tan(0.5 * fovy); }

View orbitView(int frame, int w, int h, double step) {
    const double a = (20.0 + step * frame) * kPi / 180.0;
    View v;
    v.eye = {kOrbitRadius * std::sin(a), 4.2, kOrbitRadius * std::cos(a)};
    v.target = {0, 1, 0};
    v.fovy = 0.75; v.w = w; v.h = h;
    return v;
}

D3 rayDir(const View& v, double px, double py) {
    const Basis b = basis(v);
    const double f = v.focalPx(), x = (px - 0.5 * v.w) / f, y = -(py - 0.5 * v.h) / f;
    return norm({b.fwd[0] + x * b.right[0] + y * b.up[0], b.fwd[1] + x * b.right[1] + y * b.up[1], b.fwd[2] + x * b.right[2] + y * b.up[2]});
}

Frame renderFrame(const View& v, const Lighting& L, int threads) {
    Frame f;
    f.w = v.w; f.h = v.h;
    const std::size_t n = std::size_t(v.w) * std::size_t(v.h);
    f.P.assign(n * 3, 0.0f); f.N.assign(n * 3, 0.0f); f.albedo.assign(n * 3, 0.0f); f.dir.assign(n * 3, 0.0f);
    f.dist.assign(n, -1.0f); f.sun.assign(n, 0.0f); f.lamp.assign(n, 0.0f); f.amb.assign(n, 0.0f);
    f.lit.assign(n * 4, 0.0f); f.obj.assign(n, 0); f.mat.assign(n, 0);
    parallelRows(v.h, threads, [&](int y) { for (int x = 0; x < v.w; ++x) writePixel(f, v, L, x, y); });
    return f;
}

std::vector<float> motionTo(const Frame& cur, const View& prev) {
    const Basis b = basis(prev);
    const double f = prev.focalPx();
    const std::size_t n = std::size_t(cur.w) * std::size_t(cur.h);
    std::vector<float> mv(n * 2, 0.0f);
    for (std::size_t p = 0; p < n; ++p) {
        const bool sky = cur.dist[p] < 0;
        const D3 q = sky ? D3{cur.dir[p * 3], cur.dir[p * 3 + 1], cur.dir[p * 3 + 2]}
                         : sub({cur.P[p * 3], cur.P[p * 3 + 1], cur.P[p * 3 + 2]}, prev.eye);
        const double z = dot(q, b.fwd);
        if (z <= 1e-9) continue;
        const double px = dot(q, b.right) * f / z + 0.5 * prev.w - 0.5, py = -dot(q, b.up) * f / z + 0.5 * prev.h - 0.5;
        mv[p * 2] = float(double(p % std::size_t(cur.w)) - px);
        mv[p * 2 + 1] = float(double(p / std::size_t(cur.w)) - py);
    }
    return mv;
}

std::vector<std::uint8_t> historyValid(const Frame& cur, const Frame& prev, const View& prevView, const std::vector<float>& mv) {
    const std::size_t n = std::size_t(cur.w) * std::size_t(cur.h);
    std::vector<std::uint8_t> ok(n, 0);
    for (std::size_t p = 0; p < n; ++p) {
        const double px = double(p % std::size_t(cur.w)) - mv[p * 2], py = double(p / std::size_t(cur.w)) - mv[p * 2 + 1];
        const int qx = int(std::floor(px + 0.5)), qy = int(std::floor(py + 0.5));
        if (qx < 0 || qy < 0 || qx >= prev.w || qy >= prev.h) continue;
        const std::size_t q = std::size_t(qy) * std::size_t(prev.w) + std::size_t(qx);
        if (prev.obj[q] != cur.obj[p]) continue;
        if (cur.dist[p] < 0) { ok[p] = prev.dist[q] < 0; continue; }
        const D3 e = sub({cur.P[p * 3], cur.P[p * 3 + 1], cur.P[p * 3 + 2]}, prevView.eye);
        const double want = std::sqrt(dot(e, e));
        ok[p] = std::abs(double(prev.dist[q]) - want) <= 0.02 * want;
    }
    return ok;
}

}  // namespace raw::art
