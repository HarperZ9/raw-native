// Volumetric clouds: density, lighting helpers, the raymarcher and the scenes (raw/renderer/volume.hpp).
#include "raw/renderer/volume.hpp"
#include "raw/core/parallel.hpp"
#include "raw/renderer/noise.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
namespace raw::volume {
namespace {
constexpr double kPi = 3.14159265358979323846;
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
D3 unit(D3 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
D3 at(D3 o, D3 d, double t) { return {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t}; }
double smooth(double e0, double e1, double x) { const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
}  // namespace

double density(const Cloud& c, D3 p) {
    if (p.x < c.lo.x || p.y < c.lo.y || p.z < c.lo.z || p.x > c.hi.x || p.y > c.hi.y || p.z > c.hi.z) return 0.0;
    double cover;
    if (c.noiseBlob) {   // the control: the same bounds and height span, coverage from fBm alone
        const D3 m{0.5 * (c.lo.x + c.hi.x), 0.5 * (c.lo.y + c.hi.y), 0.5 * (c.lo.z + c.hi.z)};
        const D3 e{0.5 * (c.hi.x - c.lo.x), 0.5 * (c.hi.y - c.lo.y), 0.5 * (c.hi.z - c.lo.z)};
        const double q = std::sqrt(((p.x - m.x) / e.x) * ((p.x - m.x) / e.x) + ((p.y - m.y) / e.y) * ((p.y - m.y) / e.y) + ((p.z - m.z) / e.z) * ((p.z - m.z) / e.z));
        const double f = noise::fbm(noise::Base::Perlin, float(p.x * 1.2), float(p.y * 1.2), float(p.z * 1.2), c.seed);
        cover = std::clamp(0.55 + 0.9 * f - 0.6 * q, 0.0, 1.0);
    } else {
        double keep = 1.0;
        for (const Lobe& l : c.lobes) {
            const D3 v{p.x - l.c.x, p.y - l.c.y, p.z - l.c.z};
            const double f = 1.0 - std::sqrt(dot(v, v)) / l.r;
            if (f > 0.0) keep *= 1.0 - l.w * smooth(0.0, 1.0, f);
        }
        cover = 1.0 - keep;
    }
    const double h = std::clamp((p.y - c.base) / (c.top - c.base), 0.0, 1.0);
    cover *= smooth(0.0, 0.08, h) * (1.0 - smooth(0.92, 1.0, h));   // a flat base; the lobes round the top
    if (cover <= 0.0) return 0.0;
    const double wor = std::clamp(double(noise::worley3(float(p.x * c.worleyScale), float(p.y * c.worleyScale), float(p.z * c.worleyScale), c.seed).f1), 0.0, 1.0);
    const double bil = 0.5 + 0.5 * double(noise::billow(noise::Base::Perlin, float(p.x * c.billowScale), float(p.y * c.billowScale), float(p.z * c.billowScale), c.seed + 11u));
    const double e = c.worleyAmount * wor + c.billowAmount * bil;
    return std::clamp((cover - e) / std::max(1e-6, 1.0 - e), 0.0, 1.0);
}

Rgb environment(const Lighting& l, D3 d) {
    if (l.uniformEnv >= 0.0) return {l.uniformEnv, l.uniformEnv, l.uniformEnv};
    if (!l.skyOn) return {};
    return sky::preetham(l.sky, d);
}
Rgb sunIlluminance(const Lighting& l) { return l.sunOn && l.uniformEnv < 0.0 ? sky::sunIlluminance(l.sky) : Rgb{}; }
double phase(double ct, double g1, double g2, double mix) {
    const auto hg = [](double g, double c) { const double d = 1.0 + g * g - 2.0 * g * c; return (1.0 - g * g) / (4.0 * kPi * d * std::sqrt(d)); };
    return mix * hg(g1, ct) + (1.0 - mix) * hg(g2, ct);
}

namespace {
bool clip(const Cloud& c, D3 o, D3 d, double& t0, double& t1) {
    t0 = 0.0; t1 = 1e30;
    const double lo[3] = {c.lo.x, c.lo.y, c.lo.z}, hi[3] = {c.hi.x, c.hi.y, c.hi.z}, oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(dd[k]) < 1e-12) { if (oo[k] < lo[k] || oo[k] > hi[k]) return false; continue; }
        double a = (lo[k] - oo[k]) / dd[k], b = (hi[k] - oo[k]) / dd[k];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
    }
    return t1 > t0;
}
double opticalDepthToSun(const Cloud& c, D3 p, D3 s, int steps) {
    double t0, t1;
    if (!clip(c, p, s, t0, t1)) return 0.0;
    const double dt = (t1 - t0) / steps;
    double tau = 0.0;
    for (int i = 0; i < steps; ++i) tau += c.sigma * density(c, at(p, s, t0 + (i + 0.5) * dt)) * dt;
    return tau;
}
D3 rayDir(const Camera& cam, int w, int h, int x, int y) {
    const D3 f = unit({cam.target.x - cam.eye.x, cam.target.y - cam.eye.y, cam.target.z - cam.eye.z});
    const D3 r = unit(cross(f, {0, 1, 0})), u = cross(r, f);
    const double th = std::tan(0.5 * cam.fovy), a = double(w) / h;
    const double nx = (x + 0.5) / w * 2.0 - 1.0, ny = 1.0 - (y + 0.5) / h * 2.0;
    return unit({f.x + r.x * nx * th * a + u.x * ny * th, f.y + r.y * nx * th * a + u.y * ny * th, f.z + r.z * nx * th * a + u.z * ny * th});
}
}  // namespace

D3 cameraRay(const Camera& cam, int w, int h, int x, int y) { return rayDir(cam, w, h, x, y); }

std::vector<Rgb> march(const Cloud& c, const Lighting& l, const Camera& cam, int w, int h, const MarchDesc& d, std::vector<double>* alpha, int threads) {
    std::vector<Rgb> out(std::size_t(w) * h);
    if (alpha) alpha->assign(out.size(), 0.0);
    const D3 s = unit(l.sky.sun);
    const Rgb E = sunIlluminance(l);
    Rgb amb{};
    if (d.ambient) {   // the sky's mean radiance over the upper hemisphere, a constant ambient
        const int n = 16;
        for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) {
            const double ct = (i + 0.5) / n, st = std::sqrt(1.0 - ct * ct), ph = 2.0 * kPi * (j + 0.5) / n;
            const Rgb e = environment(l, {st * std::cos(ph), ct, st * std::sin(ph)});
            amb = {amb.r + e.r / (n * n), amb.g + e.g / (n * n), amb.b + e.b / (n * n)};
        }
        amb = {amb.r * l.ambientScale, amb.g * l.ambientScale, amb.b * l.ambientScale};
    }
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    parallelRows(h, threads, [&](int y) {
        for (int x = 0; x < w; ++x) {
            const D3 dir = rayDir(cam, w, h, x, y);
            double T = 1.0;
            Rgb L{};
            double t0, t1;
            if (clip(c, cam.eye, dir, t0, t1)) {
                const double ct = dot(dir, s);
                for (double t = t0 + 0.5 * d.step; t < t1 && T > 1e-4; t += d.step) {
                    const D3 p = at(cam.eye, dir, t);
                    const double rho = density(c, p);
                    if (rho <= 0.0) continue;
                    const double st = c.sigma * rho, ss = c.albedo * st;
                    Rgb S{};
                    if (E.r + E.g + E.b > 0.0) {
                        const double tau = opticalDepthToSun(c, p, s, d.sunSteps);
                        double a = 1.0, b = 1.0, k = 1.0, sum = 0.0;
                        for (int o = 0; o < std::max(1, d.octaves); ++o) {   // octave o: extinction a^o, contribution b^o, eccentricity k^o
                            sum += b * std::exp(-a * tau) * phase(ct, d.g1 * k, d.g2 * k, d.mix);
                            a *= 0.5; b *= 0.5; k *= 0.5;
                        }
                        const double powder = d.powder ? 1.0 - std::exp(-2.0 * tau) : 1.0;
                        S = {E.r * sum * powder, E.g * sum * powder, E.b * sum * powder};
                    }
                    if (d.ambient) {
                        const double hh = std::clamp((p.y - c.base) / (c.top - c.base), 0.0, 1.0), k = 0.2 + 0.8 * hh;
                        S = {S.r + amb.r * k, S.g + amb.g * k, S.b + amb.b * k};
                    }
                    const double seg = (1.0 - std::exp(-st * d.step)) / st;   // the step's exact integral of T(s)
                    L = {L.r + T * ss * S.r * seg, L.g + T * ss * S.g * seg, L.b + T * ss * S.b * seg};
                    T *= std::exp(-st * d.step);
                }
            }
            const Rgb bg = environment(l, dir);
            const std::size_t i = std::size_t(y) * w + x;
            out[i] = {L.r + T * bg.r, L.g + T * bg.g, L.b + T * bg.b};
            if (alpha) (*alpha)[i] = 1.0 - T;
        }
    });
    return out;
}

const char* name(Scene s) {
    switch (s) { case Scene::Cumulus: return "cumulus"; case Scene::Towering: return "towering_cumulus"; case Scene::Deck: return "deck_from_above"; default: return "smoke_plume_sunset"; }
}

void scene(Scene sc, Cloud& c, Lighting& l, Camera& cam) {
    c = Cloud{}; l = Lighting{};
    const auto elev = [](double deg, double az) { const double a = deg * kPi / 180.0, b = az * kPi / 180.0; return D3{std::cos(a) * std::cos(b), std::sin(a), std::cos(a) * std::sin(b)}; };
    std::uint32_t z = 12345u;
    const auto rnd = [&] { z = z * 1664525u + 1013904223u; return double(z >> 8) / 16777216.0; };
    // Cauliflower: around each core lobe, smaller lobes on its upper surface.
    const auto puff = [&](D3 core, double r, int n) {
        c.lobes.push_back({core, r, 1.0});
        for (int k = 0; k < n; ++k) {
            const double th = 2.0 * kPi * rnd(), up = 0.15 + 0.85 * rnd(), cs = std::sqrt(std::max(0.0, 1.0 - up * up));
            const D3 d{cs * std::cos(th), up, cs * std::sin(th)};
            c.lobes.push_back({{core.x + d.x * 0.8 * r, core.y + d.y * 0.8 * r, core.z + d.z * 0.8 * r}, r * (0.3 + 0.2 * rnd()), 1.0});
        }
    };
    if (sc == Scene::Cumulus) {
        c.base = 0.5; c.top = 2.9; c.lo = {-2.6, 0.4, -1.9}; c.hi = {2.6, 3.0, 1.9};
        const double pts[7][4] = {{0, 1.3, 0, 0.9}, {-0.9, 1.1, 0.2, 0.7}, {0.9, 1.15, -0.1, 0.75}, {0.2, 1.9, 0.1, 0.7}, {-0.4, 1.7, -0.4, 0.6}, {1.4, 0.95, 0.3, 0.5}, {-1.5, 0.9, -0.2, 0.5}};
        for (const auto& q : pts) puff({q[0], q[1], q[2]}, q[3], 6);
        c.sigma = 18.0; c.worleyScale = 2.4; c.worleyAmount = 0.3; c.billowAmount = 0.08;
        l.sky.turbidity = 2.5; l.sky.sun = elev(35.0, 140.0);
        cam = {{0.5, 1.4, 6.0}, {0.0, 1.5, 0.0}, 0.75};
    } else if (sc == Scene::Towering) {
        c.base = 0.3; c.top = 6.8; c.lo = {-3.0, 0.2, -2.6}; c.hi = {3.0, 6.9, 2.6};
        for (int k = 0; k < 10; ++k) {
            const double y = 1.0 + 0.55 * k, r = 1.25 - 0.04 * k + (k > 7 ? 0.35 : 0.0);   // a wider top
            puff({0.7 * (rnd() - 0.5) + (k > 7 ? 0.5 : 0.0), y, 0.6 * (rnd() - 0.5)}, r * (0.8 + 0.3 * rnd()), 5);
        }
        c.sigma = 18.0; c.worleyScale = 2.0; c.worleyAmount = 0.3; c.billowAmount = 0.08;
        l.sky.turbidity = 2.2; l.sky.sun = elev(25.0, 160.0);
        cam = {{1.0, 2.8, 12.0}, {0.0, 3.4, 0.0}, 0.8};
    } else if (sc == Scene::Deck) {
        c.base = 0.0; c.top = 1.0; c.lo = {-10.0, -0.1, -10.0}; c.hi = {10.0, 1.1, 10.0};
        for (int k = 0; k < 70; ++k) puff({-9.0 + 18.0 * rnd(), 0.4 + 0.15 * rnd(), -9.0 + 18.0 * rnd()}, 0.8 + 0.6 * rnd(), 3);
        c.top = 1.4; c.hi.y = 1.5; c.sigma = 14.0; c.worleyScale = 2.4;
        l.sky.turbidity = 2.5; l.sky.sun = elev(40.0, 60.0);
        cam = {{0.0, 7.0, 9.0}, {0.0, 0.5, 0.0}, 0.9};
    } else {
        c.base = 0.0; c.top = 5.0; c.lo = {-2.5, -0.1, -2.0}; c.hi = {3.0, 5.1, 2.0};
        for (int k = 0; k < 12; ++k) { const double y = 0.3 + 0.4 * k, r = 0.25 + 0.09 * k; c.lobes.push_back({{0.12 * k + 0.2 * (rnd() - 0.5), y, 0.2 * (rnd() - 0.5)}, r, 0.9}); }
        c.albedo = 0.6; c.sigma = 6.0; c.worleyAmount = 0.25; c.billowAmount = 0.2; c.billowScale = 2.0;
        l.sky.turbidity = 4.5; l.sky.sun = elev(4.0, 200.0);
        cam = {{2.0, 2.0, 8.0}, {0.6, 2.2, 0.0}, 0.8};
    }
}

}  // namespace raw::volume
