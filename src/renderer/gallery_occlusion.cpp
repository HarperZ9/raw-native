// The gallery's visibility pass (evidence/gallery-occlusion-bounds.json): see raw/renderer/gallery.hpp.
#include "raw/renderer/gallery.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
namespace raw::gallery {
namespace {
using lighting::D3;
constexpr double kPi = 3.14159265358979323846;
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
D3 unit(D3 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
struct Occluder { D3 c; double r2; };
// The spheres that can block anything above p's tangent plane, its own sphere excluded.
std::vector<Occluder> occludersOf(const Gallery& g, const lighting::Sample& s) {
    std::vector<Occluder> o;
    for (const Gallery::Sphere& sp : g.spheres) {
        const D3 oc{sp.centre.x - s.p.x, sp.centre.y - s.p.y, sp.centre.z - s.p.z};
        const double d2 = dot(oc, oc);
        if (d2 <= sp.radius * sp.radius * (1.0 + 1e-9) + 1e-12) continue;          // the sample's own sphere
        if (dot(oc, s.n) < -sp.radius) continue;                                      // wholly below the tangent plane
        o.push_back({sp.centre, sp.radius * sp.radius});
    }
    return o;
}
bool blocked(const std::vector<Occluder>& os, D3 p, D3 d) {
    for (const Occluder& o : os) {
        const D3 oc{o.c.x - p.x, o.c.y - p.y, o.c.z - p.z};
        const double b = dot(oc, d);
        if (b <= 0.0) continue;
        if (b * b - (dot(oc, oc) - o.r2) >= 0.0) return true;
    }
    return false;
}
struct Frame { D3 t, b, n; };
Frame frameOf(const lighting::Sample& s) { const D3 n = unit(s.n), t = unit(s.t); return {t, cross(n, t), n}; }
D3 toWorld(const Frame& f, D3 v) { return {f.t.x * v.x + f.b.x * v.y + f.n.x * v.z, f.t.y * v.x + f.b.y * v.y + f.n.y * v.z, f.t.z * v.x + f.b.z * v.y + f.n.z * v.z}; }
D3 toLocal(const Frame& f, D3 w) { return {dot(w, f.t), dot(w, f.b), dot(w, f.n)}; }
// VNDF (Heitz 2018), isotropic.
D3 vndf(D3 wo, double a, double u1, double u2) {
    const D3 vh = unit({a * wo.x, a * wo.y, wo.z});
    const double l2 = vh.x * vh.x + vh.y * vh.y;
    const D3 t1 = l2 > 0.0 ? D3{-vh.y / std::sqrt(l2), vh.x / std::sqrt(l2), 0.0} : D3{1.0, 0.0, 0.0}, t2 = cross(vh, t1);
    const double r = std::sqrt(u1), ph = 2.0 * kPi * u2, sp = 0.5 * (1.0 + vh.z);
    const double p1 = r * std::cos(ph), p2 = (1.0 - sp) * std::sqrt(std::max(0.0, 1.0 - p1 * p1)) + sp * r * std::sin(ph);
    const double p3 = std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2));
    return unit({a * (t1.x * p1 + t2.x * p2 + vh.x * p3), a * (t1.y * p1 + t2.y * p2 + vh.y * p3), std::max(1e-12, t1.z * p1 + t2.z * p2 + vh.z * p3)});
}
}  // namespace

double diffuseVisibility(const Gallery& g, const lighting::Sample& s, int n) {
    const std::vector<Occluder> os = occludersOf(g, s);
    if (os.empty()) return 1.0;
    const Frame f = frameOf(s);
    const int k = std::max(1, int(std::lround(std::sqrt(double(n)))));
    int open = 0;
    for (int i = 0; i < k; ++i) for (int j = 0; j < k; ++j) {   // stratified cosine-weighted directions
        const double u1 = (i + 0.5) / k, u2 = (j + 0.5) / k, r = std::sqrt(u1), ph = 2.0 * kPi * u2;
        open += !blocked(os, s.p, toWorld(f, {r * std::cos(ph), r * std::sin(ph), std::sqrt(1.0 - u1)}));
    }
    return double(open) / double(k * k);
}

double specularVisibility(const Gallery& g, const lighting::Sample& s, double roughness, int n) {
    const std::vector<Occluder> os = occludersOf(g, s);
    if (os.empty()) return 1.0;
    const Frame f = frameOf(s);
    const D3 wo = toLocal(f, unit(s.v));
    if (wo.z <= 0.0) return 1.0;
    const double r = std::max(roughness, pbr::kMinRoughness), a = r * r;
    const int k = std::max(1, int(std::lround(std::sqrt(double(n)))));
    int open = 0, valid = 0;
    for (int i = 0; i < k; ++i) for (int j = 0; j < k; ++j) {   // stratified VNDF reflections: the lobe's own weighting
        const D3 h = vndf(wo, a, (i + 0.5) / k, (j + 0.5) / k);
        const double oh = dot(wo, h);
        const D3 wi{2.0 * oh * h.x - wo.x, 2.0 * oh * h.y - wo.y, 2.0 * oh * h.z - wo.z};
        if (wi.z <= 0.0) continue;
        ++valid;
        open += !blocked(os, s.p, toWorld(f, wi));
    }
    return valid ? double(open) / double(valid) : 1.0;
}

void occlusion(Gallery& g, int diffuseDirs, int specularDirs, int threads) {
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    const int rows = int((g.samples.size() + 1023) / 1024);
    parallelRows(rows, threads, [&](int row) {
        for (std::size_t k = std::size_t(row) * 1024; k < std::min(g.samples.size(), std::size_t(row + 1) * 1024); ++k) {
            lighting::Sample& s = g.samples[k];
            s.ao = diffuseVisibility(g, s, diffuseDirs);
            s.so = specularVisibility(g, s, s.m.roughness, specularDirs);
            s.soCoat = s.m.clearcoat > 0.0 ? specularVisibility(g, s, s.m.clearcoatRoughness, specularDirs) : 1.0;
        }
    });
}

}  // namespace raw::gallery
