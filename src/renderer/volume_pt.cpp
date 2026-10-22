// The clouds' reference: a volumetric path tracer by delta tracking (raw/renderer/volume.hpp).
//   Woodcock tracking against the majorant sigma (density is in [0, 1]); next-event estimation
//   to the sun with ratio tracking; phase sampling of the dual-lobe Henyey-Greenstein mixture.
#include "raw/renderer/volume.hpp"
#include "raw/core/parallel.hpp"
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
struct Rng { std::uint64_t s; double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s >> 11) * (1.0 / 9007199254740992.0); } };
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
// Ratio tracking: transmittance from p along d to the bounds' exit.
double transmittance(const Cloud& c, D3 p, D3 d, Rng& r) {
    double t0, t1;
    if (!clip(c, p, d, t0, t1)) return 1.0;
    double T = 1.0, t = t0;
    for (;;) {
        t -= std::log(1.0 - r.next()) / c.sigma;
        if (t >= t1) return T;
        T *= 1.0 - density(c, at(p, d, t));
        if (T < 1e-6) return 0.0;
    }
}
// A direction from the phase mixture around the propagation direction d.
D3 samplePhase(D3 d, double g1, double g2, double mix, Rng& r) {
    const double g = r.next() < mix ? g1 : g2, u = r.next(), v = r.next();
    double ct;
    if (std::fabs(g) < 1e-4) ct = 1.0 - 2.0 * u;
    else { const double s = (1.0 - g * g) / (1.0 - g + 2.0 * g * u); ct = (1.0 + g * g - s * s) / (2.0 * g); }
    ct = std::clamp(ct, -1.0, 1.0);
    const double st = std::sqrt(std::max(0.0, 1.0 - ct * ct)), ph = 2.0 * kPi * v;
    const D3 t = std::fabs(d.y) < 0.99 ? unit(cross(d, {0, 1, 0})) : unit(cross(d, {1, 0, 0})), b = cross(d, t);
    return unit({t.x * st * std::cos(ph) + b.x * st * std::sin(ph) + d.x * ct, t.y * st * std::cos(ph) + b.y * st * std::sin(ph) + d.y * ct,
                 t.z * st * std::cos(ph) + b.z * st * std::sin(ph) + d.z * ct});
}
}  // namespace

std::vector<Rgb> pathTrace(const Cloud& c, const Lighting& l, const Camera& cam, int w, int h, int spp, bool single, const MarchDesc& ph,
                           std::vector<double>* var, std::uint64_t seed, int threads) {
    std::vector<Rgb> out(std::size_t(w) * h);
    if (var) var->assign(out.size(), 0.0);
    const D3 s = unit(l.sky.sun);
    const Rgb E = sunIlluminance(l);
    const bool sun = E.r + E.g + E.b > 0.0;
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    parallelRows(h, threads, [&](int y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            Rng r{seed * 0x9E3779B97F4A7C15ull + i * 0xD1B54A32D192ED03ull + 1};
            for (int k = 0; k < 8; ++k) r.next();
            Rgb sum{}; double m1 = 0, m2 = 0;
            for (int n = 0; n < spp; ++n) {
                D3 o = cam.eye, d = cameraRay(cam, w, h, x, y);
                double thr = 1.0;
                Rgb L{};
                for (int bounce = 0; bounce < 256; ++bounce) {
                    double t0, t1;
                    bool scattered = false;
                    if (clip(c, o, d, t0, t1)) {
                        double t = t0;
                        for (;;) {
                            t -= std::log(1.0 - r.next()) / c.sigma;
                            if (t >= t1) break;
                            const D3 p = at(o, d, t);
                            if (r.next() >= density(c, p)) continue;   // a null collision
                            thr *= c.albedo;                           // absorption as weight
                            if (sun) {
                                const double T = transmittance(c, p, s, r), f = phase(dot(d, s), ph.g1, ph.g2, ph.mix);
                                L = {L.r + thr * E.r * f * T, L.g + thr * E.g * f * T, L.b + thr * E.b * f * T};
                            }
                            o = p; scattered = true;
                            break;
                        }
                    }
                    if (!scattered) {   // escape: the environment (in single-scattering mode, only the camera ray sees it)
                        if (bounce == 0 || !single) { const Rgb e = environment(l, d); L = {L.r + thr * e.r, L.g + thr * e.g, L.b + thr * e.b}; }
                        break;
                    }
                    if (single) break;
                    d = samplePhase(d, ph.g1, ph.g2, ph.mix, r);
                    if (bounce > 8) { const double q = std::min(0.95, thr); if (r.next() >= q) break; thr /= q; }
                }
                sum = {sum.r + L.r, sum.g + L.g, sum.b + L.b};
                const double lu = 0.2126 * L.r + 0.7152 * L.g + 0.0722 * L.b;
                m1 += lu; m2 += lu * lu;
            }
            out[i] = {sum.r / spp, sum.g / spp, sum.b / spp};
            if (var && spp > 1) (*var)[i] = std::max(0.0, (m2 - m1 * m1 / spp) / (spp - 1)) / spp;
        }
    });
    return out;
}

}  // namespace raw::volume
