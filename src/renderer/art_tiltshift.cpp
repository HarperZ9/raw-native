// P23's lens, circles of confusion, the screen-band control and the looks: see
// raw/renderer/art_tiltshift.hpp.
#include "raw/renderer/art_tiltshift.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
constexpr double kPi = 3.141592653589793;
double dot(const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
D3 sub(const D3& a, const D3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
D3 cross(const D3& a, const D3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
D3 norm(const D3& a) { const double l = std::sqrt(dot(a, a)); return {a[0] / l, a[1] / l, a[2] / l}; }
D3 axpy(double s, const D3& a, const D3& b) { return {s * a[0] + b[0], s * a[1] + b[1], s * a[2] + b[2]}; }
// Rodrigues: v rotated by angle t about the unit axis k.
D3 rotate(const D3& v, const D3& k, double t) {
    const D3 kv = cross(k, v);
    const double c = std::cos(t), s = std::sin(t), kd = dot(k, v) * (1 - c);
    return {v[0] * c + kv[0] * s + k[0] * kd, v[1] * c + kv[1] * s + k[1] * kd, v[2] * c + kv[2] * s + k[2] * kd};
}
}  // namespace

LensGeom resolveLens(const Lens& l, const View& v) {
    LensGeom g;
    g.eye = v.eye;
    g.fwd = norm(sub(v.target, v.eye));
    const D3 up{0, 1, 0}, right = norm(cross(g.fwd, up));
    D3 a = dot(l.axis, l.axis) > 0 ? norm({l.axis[0], 0.0, l.axis[2]}) : right;
    if (l.rollDeg != 0) a = norm(rotate(a, g.fwd, l.rollDeg * kPi / 180));
    // The plane's second direction: from the horizontal across the axis toward upright by the tilt.
    const D3 across = norm(cross(up, a));
    const double t = l.tiltDeg * kPi / 180;
    const D3 u = axpy(std::cos(t), across, {up[0] * std::sin(t), up[1] * std::sin(t), up[2] * std::sin(t)});
    g.n = norm(cross(a, u));
    g.d = dot(g.n, l.focus);
    g.fpx = v.focalPx();
    g.fmm = 12.0 / std::tan(0.5 * v.fovy);
    g.mmPerUnit = l.unitMm / l.K;
    g.N = l.fNumber;
    g.cMax = l.cMax1080 * v.h / 1080.0;
    return g;
}

double cocAt(const LensGeom& g, const D3& dir, double dist) {
    const double cosA = dot(dir, g.fwd), dn = dot(dir, g.n);
    const double tPlane = std::abs(dn) > 1e-12 ? (g.d - dot(g.eye, g.n)) / dn : -1;
    const bool skyZ = dist < 0, skyS = tPlane <= 0;
    double c = 0;
    if (skyZ && skyS) return 0;
    const double S = skyS ? 0 : std::max(tPlane * cosA * g.mmPerUnit, 2 * g.fmm);
    if (skyZ) c = g.fpx * g.fmm / (g.N * (S - g.fmm));
    else {
        const double Z = dist * cosA * g.mmPerUnit;
        c = skyS ? g.fpx * g.fmm / (g.N * Z) : g.fpx * g.fmm * std::abs(Z - S) / (g.N * Z * (S - g.fmm));
    }
    return std::min(c, g.cMax);
}

double cocAtPoint(const LensGeom& g, const D3& p) {
    const D3 e = sub(p, g.eye);
    const double dist = std::sqrt(dot(e, e));
    return cocAt(g, {e[0] / dist, e[1] / dist, e[2] / dist}, dist);
}

double hingeTiltDeg(const LensGeom& g) {
    // The plane through the lens parallel to the sensor is fwd . (x - eye) = 0. Its line of
    // intersection with the plane of focus lies at distance J from the lens centre: the distance from
    // the eye to that line, within the lens plane, is |d - n . eye| / sqrt(1 - (n . fwd)^2).
    const double nf = dot(g.n, g.fwd), s = std::sqrt(std::max(0.0, 1 - nf * nf));
    if (s < 1e-12) return 0;
    const double J = std::abs(g.d - dot(g.n, g.eye)) / s * g.mmPerUnit;
    return J <= g.fmm ? 90.0 : std::asin(g.fmm / J) * 180 / kPi;
}

std::vector<float> cocMap(const Frame& f, const LensGeom& g) {
    const std::size_t n = std::size_t(f.w) * std::size_t(f.h);
    std::vector<float> o(n * 2, 0.0f);
    for (std::size_t p = 0; p < n; ++p) {
        const D3 d{f.dir[p * 3], f.dir[p * 3 + 1], f.dir[p * 3 + 2]};
        const double dist = f.dist[p];
        o[p * 2] = float(cocAt(g, d, dist < 0 ? -1.0 : dist));
        o[p * 2 + 1] = dist < 0 ? 1e6f : float(dist * dot(d, g.fwd));
    }
    return o;
}

std::vector<float> bandCoc(const std::vector<float>& lens, const LensGeom& g, const View& v, const D3& focus, double c0) {
    const int w = v.w, h = v.h;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    long want = 0;
    for (std::size_t p = 0; p < n; ++p) want += lens[p * 2] <= c0;
    // The band is centred on the focus point's screen row.
    const D3 right = norm(cross(g.fwd, {0, 1, 0})), up = cross(right, g.fwd), q = sub(focus, g.eye);
    const double yF = -dot(q, up) * g.fpx / dot(q, g.fwd) + 0.5 * h, ramp = 0.15 * h, reach = ramp * c0 / g.cMax;
    auto inFocus = [&](double half) { long c = 0; for (int y = 0; y < h; ++y) c += std::abs(y + 0.5 - yF) - half <= reach ? w : 0; return c; };
    double lo = -reach - 1, hi = double(h);
    for (int it = 0; it < 60; ++it) { const double mid = 0.5 * (lo + hi); (inFocus(mid) < want ? lo : hi) = mid; }
    std::vector<float> o(lens);
    for (std::size_t p = 0; p < n; ++p) {
        const double y = double(p / std::size_t(w)) + 0.5;
        o[p * 2] = float(g.cMax * std::clamp((std::abs(y - yF) - hi) / ramp, 0.0, 1.0));
    }
    return o;
}

TiltLook tiltLook(const std::string& name) {
    TiltLook L;
    L.name = name;
    if (name == "daily") { L.K = 8; L.fNumber = 2.0; L.tiltDeg = 70; L.vignette = 0.25; L.grain = 0.02; }
    else if (name == "town_map") { L.K = 40; L.fNumber = 1.4; L.tiltDeg = 90; L.streetAxis = true; L.vignette = 0.45; L.mech = 0.3; L.grain = 0.015; }
    else if (name == "record") { L.K = 40; L.fNumber = 1.4; L.tiltDeg = 90; L.streetAxis = true; L.vignette = 0.3; L.misreg = 2; L.halftone = 0.55; L.grain = 0.03; L.print = 0.3; }
    else if (name == "memory") { L.K = 12; L.fNumber = 2.0; L.tiltDeg = 75; L.vignette = 0.5; L.mech = 0.4; L.print = 0.8; L.weave = 0.6; L.grain = 0.05; L.halation = 0.4; }
    else if (name == "dream") { L.K = 60; L.fNumber = 1.2; L.tiltDeg = 20; L.rollDeg = 35; L.vignette = 0.7; L.ca = 4; L.grain = 0.03; L.sat = 0.55; L.hueDeg = 20; }
    else if (name == "night") { L.K = 20; L.fNumber = 1.8; L.tiltDeg = 80; L.streetAxis = true; L.vignette = 0.4; L.mech = 0.3; L.halation = 1.0; L.star = 1.0; L.starRotDeg = 8; L.grain = 0.03; L.ca = 0.8; }
    return L;
}

TiltLook lerpLook(const TiltLook& a, const TiltLook& b, double t) {
    auto m = [t](double x, double y) { return x + (y - x) * t; };
    TiltLook o = t < 0.5 ? a : b;
    o.name = a.name + ">" + b.name;
    // K and the f-number blend in their logarithms, so a morph moves evenly in stops and in scale.
    o.K = std::exp(m(std::log(a.K), std::log(b.K))); o.fNumber = std::exp(m(std::log(a.fNumber), std::log(b.fNumber)));
    o.tiltDeg = m(a.tiltDeg, b.tiltDeg); o.rollDeg = m(a.rollDeg, b.rollDeg);
    o.vignette = m(a.vignette, b.vignette); o.mech = m(a.mech, b.mech); o.halation = m(a.halation, b.halation); o.star = m(a.star, b.star);
    o.starRotDeg = m(a.starRotDeg, b.starRotDeg); o.ca = m(a.ca, b.ca); o.grain = m(a.grain, b.grain); o.weave = m(a.weave, b.weave);
    o.print = m(a.print, b.print); o.misreg = m(a.misreg, b.misreg); o.halftone = m(a.halftone, b.halftone);
    o.sat = m(a.sat, b.sat); o.hueDeg = m(a.hueDeg, b.hueDeg);
    return o;
}

Lens lensOf(const TiltLook& look, const D3& focus, const D3& streetAxis) {
    Lens l;
    l.focus = focus;
    l.axis = look.streetAxis ? streetAxis : D3{0, 0, 0};
    l.tiltDeg = look.tiltDeg; l.rollDeg = look.rollDeg; l.K = look.K; l.fNumber = look.fNumber;
    return l;
}

}  // namespace raw::art
