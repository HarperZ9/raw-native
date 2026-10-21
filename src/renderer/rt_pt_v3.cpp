// Path tracer interface v3: the environment's importance sampling and the full glTF material
// at a hit (docs/rt-interface-v3.md). See rt_pt_v3.hpp.
#include "rt_pt_v3.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include <algorithm>
#include <cmath>
namespace raw::rt {

bool usesV3(const PtScene& s) { return !s.lights.empty() || !s.environment.rgb.empty() || !s.fullMaterials.empty(); }

namespace v3 {
namespace {
constexpr double kPi = pbr::kPi;
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
D3 unit(D3 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
D3 add(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 scale(D3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double lum(lighting::Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
// Texel weight: luminance times the texel's solid angle up to a constant, (1 + s^2 + t^2)^-1.5.
double jac(int N, int x, int y) {
    const double sc = 2.0 * (x + 0.5) / N - 1.0, tc = 2.0 * (y + 0.5) / N - 1.0;
    return std::pow(1.0 + sc * sc + tc * tc, 1.5);
}
// Isotropic GGX: D, the Smith Lambda, VNDF sampling (Heitz 2018), in double.
double ggxD(double a, double cz) { const double a2 = a * a, d = cz * cz * (a2 - 1.0) + 1.0; return a2 / (kPi * d * d); }
double lambda(double a, double cz) { const double c2 = cz * cz; return (-1.0 + std::sqrt(1.0 + a * a * (1.0 - c2) / std::max(c2, 1e-300))) * 0.5; }
D3 vndf(D3 wo, double a, double u1, double u2) {
    const D3 vh = unit({a * wo.x, a * wo.y, wo.z});
    const double l2 = vh.x * vh.x + vh.y * vh.y;
    const D3 t1 = l2 > 0.0 ? scale(D3{-vh.y, vh.x, 0.0}, 1.0 / std::sqrt(l2)) : D3{1.0, 0.0, 0.0}, t2 = cross(vh, t1);
    const double r = std::sqrt(u1), ph = 2.0 * kPi * u2, s = 0.5 * (1.0 + vh.z);
    const double p1 = r * std::cos(ph), p2 = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - p1 * p1)) + s * r * std::sin(ph);
    const D3 nh = add(add(scale(t1, p1), scale(t2, p2)), scale(vh, std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2))));
    return unit({a * nh.x, a * nh.y, std::max(1e-12, nh.z)});
}
// Solid-angle pdf of VNDF reflection producing wi (wi above the surface).
double pdfReflect(D3 wo, D3 wi, double a) {
    if (wi.z <= 0.0) return 0.0;
    const D3 h = unit(add(wo, wi));
    if (h.z <= 0.0 || dot(wo, h) <= 0.0) return 0.0;
    return ggxD(a, h.z) / (1.0 + lambda(a, wo.z)) / (4.0 * wo.z);
}
// Solid-angle pdf of VNDF refraction from outside (index 1) into index eta producing wi below.
double pdfRefract(D3 wo, D3 wi, double a, double eta) {
    if (wi.z >= 0.0) return 0.0;
    D3 h = unit(add(wo, scale(wi, eta)));
    if (h.z < 0.0) h = scale(h, -1.0);
    const double oh = dot(wo, h), ih = dot(wi, h);
    if (oh <= 0.0 || ih >= 0.0) return 0.0;
    const double den = oh + eta * ih;
    return ggxD(a, h.z) / (1.0 + lambda(a, wo.z)) * oh / wo.z * std::fabs(ih) * eta * eta / (den * den);
}
}  // namespace

void finish(PtScene& s) {
    s.envCdf.clear(); s.envTotal = 0.0;
    if (!s.fullMaterials.empty() && !s.tables) s.tables = std::make_shared<const pbr::Tables>();
    if (s.environment.rgb.empty()) return;
    const int N = s.environment.size;
    s.envCdf.resize(std::size_t(6) * N * N);
    for (int i = 0; i < 6 * N * N; ++i) {
        const int f = i / (N * N), y = (i / N) % N, x = i % N;
        s.envTotal += (lum(s.environment.texel(0, f, y, x)) + 1e-6) / jac(N, x, y);
        s.envCdf[std::size_t(i)] = s.envTotal;
    }
}

lighting::Rgb envRadiance(const PtScene& s, D3 d) { return s.environment.sample(0, d); }

double envPdf(const PtScene& s, D3 d) {
    if (s.envCdf.empty()) return 0.0;
    const int N = s.environment.size;
    double u, v;
    const int f = lighting::cube::faceOf(d, u, v);
    const int x = std::min(int(u * N), N - 1), y = std::min(int(v * N), N - 1), i = (f * N + y) * N + x;
    const double p = (s.envCdf[std::size_t(i)] - (i ? s.envCdf[std::size_t(i - 1)] : 0.0)) / s.envTotal;
    const double sc = 2.0 * u - 1.0, tc = 2.0 * v - 1.0;
    return p * (N * 0.5) * (N * 0.5) * std::pow(1.0 + sc * sc + tc * tc, 1.5);
}

bool envSample(const PtScene& s, double u1, double u2, D3& dir, double& pdf) {
    if (s.envCdf.empty()) return false;
    const int N = s.environment.size;
    const double target = u1 * s.envTotal;
    const int i = std::min(int(std::lower_bound(s.envCdf.begin(), s.envCdf.end(), target) - s.envCdf.begin()), 6 * N * N - 1);
    const int f = i / (N * N), y = (i / N) % N, x = i % N;
    const double lo = i ? s.envCdf[std::size_t(i - 1)] : 0.0, rem = std::clamp((target - lo) / (s.envCdf[std::size_t(i)] - lo), 0.0, 1.0 - 1e-12);
    dir = lighting::cube::dirOf(f, (x + std::clamp(u2, 0.0, 1.0 - 1e-12)) / N, (y + rem) / N);
    pdf = envPdf(s, dir);
    return pdf > 0.0;
}

Shade shadeAt(const PtScene& s, const ptc::Surf& x, const pbr::Material* full) {
    Shade h;
    h.n = unit(d3(x.n));
    D3 t = cross(h.n, D3{0.0, 1.0, 0.0});
    if (dot(t, t) < 1e-12) t = D3{1.0 - h.n.x * h.n.x, -h.n.x * h.n.y, -h.n.x * h.n.z};
    h.t = unit(t); h.b = cross(h.n, h.t);
    if (!full) { h.v2 = {x.base, x.m->roughness, x.m->metallic, x.m->specular}; return h; }
    h.full = true;
    h.m = *full;
    h.m.baseColor = {full->baseColor.r * x.base.x, full->baseColor.g * x.base.y, full->baseColor.b * x.base.z};
    if (h.m.volume) {
        h.m.thickness = 0.0;                       // traced: Beer-Lambert along the segment instead
        if (x.back) h.m.ior = 1.0 / h.m.ior;       // leaving the volume
    }
    h.eta = h.m.volume ? h.m.ior : 1.0;
    const double r = std::max(h.m.roughness, pbr::kMinRoughness), rc = std::max(h.m.clearcoatRoughness, pbr::kMinRoughness);
    h.aBase = r * r; h.aCoat = rc * rc;
    const double mt = std::clamp(h.m.metallic, 0.0, 1.0), tr = std::clamp(h.m.transmission, 0.0, 1.0) * (1.0 - mt);
    double w[5] = {1.0, h.m.clearcoat > 0.0 ? 0.5 * h.m.clearcoat : 0.0,
                   0.25 + 0.75 * (1.0 - mt) * (1.0 - tr) + (h.m.sheenColor.max3() > 0.0 ? 0.5 : 0.0), tr, tr > 0.0 ? 0.1 : 0.0};
    const double sum = w[0] + w[1] + w[2] + w[3] + w[4];
    for (int k = 0; k < 5; ++k) h.p[k] = w[k] / sum;
    return h;
}
D3 toLocal(const Shade& h, D3 w) { return {dot(w, h.t), dot(w, h.b), dot(w, h.n)}; }
D3 toWorld(const Shade& h, D3 w) { return add(add(scale(h.t, w.x), scale(h.b, w.y)), scale(h.n, w.z)); }

Eval eval(const PtScene& s, const Shade& h, D3 wo, D3 wi) {
    Eval e;
    if (!h.full) {
        const Vec3 o = f3(wo), i = f3(wi);
        const pt::Eval v = pt::evalBsdf(h.v2, o, i);
        const Vec3 l = pt::lambertCos(h.v2, o, i);
        e.fcos = {v.fcos.x, v.fcos.y, v.fcos.z}; e.smooth = {l.x, l.y, l.z}; e.pdf = v.pdf;
        return e;
    }
    if (wo.z <= 0.0 || wi.z == 0.0) return e;
    wo = unit(wo); wi = unit(wi);   // the frame came from a float normal: keep |w| = 1 so no cosine passes 1
    const pbr::Terms t = pbr::evalTerms(h.m, *s.tables, wo, wi);
    const double c = std::fabs(wi.z);
    const pbr::Rgb tot = t.total();
    e.fcos = {tot.r * c, tot.g * c, tot.b * c};
    if (wi.z > 0.0) e.pdf = h.p[0] * pdfReflect(wo, wi, h.aBase) + h.p[1] * pdfReflect(wo, wi, h.aCoat) + h.p[2] * wi.z / kPi;
    else {
        const double pt = h.m.volume ? pdfRefract(wo, wi, h.aBase, h.eta) : pdfReflect(wo, {wi.x, wi.y, -wi.z}, h.aBase);
        e.pdf = h.p[3] * pt + h.p[4] * (-wi.z) / kPi;
    }
    return e;
}

bool sample(const Shade& h, D3 wo, double pick, double u1, double u2, D3& wi) {
    if (!h.full) {
        Vec3 w;
        if (!pt::sampleBsdf(h.v2, f3(wo), float(pick), float(u1), float(u2), w)) return false;
        wi = d3(w);
        return true;
    }
    const auto cosine = [&](double sign) {
        const double r = std::sqrt(u1), ph = 2.0 * kPi * u2;
        return D3{r * std::cos(ph), r * std::sin(ph), sign * std::sqrt(std::max(0.0, 1.0 - u1))};
    };
    const auto reflect = [&](double a) { const D3 m = vndf(wo, a, u1, u2); return add(scale(m, 2.0 * dot(wo, m)), scale(wo, -1.0)); };
    double acc = h.p[0];
    if (pick < acc) { wi = reflect(h.aBase); return wi.z > 0.0; }
    if (pick < (acc += h.p[1])) { wi = reflect(h.aCoat); return wi.z > 0.0; }
    if (pick < (acc += h.p[2])) { wi = cosine(1.0); return wi.z > 0.0; }
    if (pick < (acc += h.p[3])) {
        if (!h.m.volume) { const D3 r = reflect(h.aBase); wi = {r.x, r.y, -r.z}; return wi.z < 0.0; }
        const D3 m = vndf(wo, h.aBase, u1, u2);
        const double c = dot(wo, m), er = 1.0 / h.eta, k = 1.0 - er * er * (1.0 - c * c);
        if (k < 0.0) return false;                 // total internal reflection: the reflect strategies cover it
        wi = add(scale(wo, -er), scale(m, er * c - std::sqrt(k)));
        return wi.z < 0.0;
    }
    wi = cosine(-1.0);
    return wi.z < 0.0;
}

}  // namespace v3
}  // namespace raw::rt
