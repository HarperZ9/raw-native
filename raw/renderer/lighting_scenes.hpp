#pragma once
// Seeded scenes for the lighting checks, shared by the CPU tests and the GPU parity (evidence/m3-lighting-bounds.json): lights in
// view space inside a camera frustum, and surface samples near them.
#include "raw/renderer/lighting.hpp"
#include <cmath>
#include <cstdint>
#include <vector>
namespace raw::lighting::scenes {
using raw::pbr::kPi;

struct Rng {
    std::uint64_t s;
    double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s >> 11) * (1.0 / 9007199254740992.0); }
};
inline D3 unit(D3 a) { const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return {a.x / l, a.y / l, a.z / l}; }
inline D3 randomDir(Rng& r) {
    const double z = 2 * r.next() - 1, ph = 2 * kPi * r.next(), s = std::sqrt(1 - z * z);
    return {s * std::cos(ph), s * std::sin(ph), z};
}
// A point inside the grid's frustum at a depth drawn log-uniformly in [lo, hi].
inline D3 inFrustum(Rng& r, const ClusterGrid& g, double lo, double hi) {
    const double d = lo * std::pow(hi / lo, r.next()), th = std::tan(0.5 * g.fovy);
    return {(2 * r.next() - 1) * d * th * g.aspect, (2 * r.next() - 1) * d * th, -d};
}
inline std::vector<Light> scene(Rng& r, const ClusterGrid& g, int count) {
    std::vector<Light> ls;
    for (int k = 0; k < count; ++k) {
        Light l;
        const double u = r.next();
        l.type = k < 2 ? LightType::Directional : u < 0.6 ? LightType::Point : LightType::Spot;
        l.position = inFrustum(r, g, 0.5, 80.0);
        l.direction = unit(randomDir(r));
        l.color = {0.3 + 0.7 * r.next(), 0.3 + 0.7 * r.next(), 0.3 + 0.7 * r.next()};
        l.intensity = l.type == LightType::Directional ? 2000.0 * r.next() : 50.0 + 2000.0 * r.next();
        l.range = l.type == LightType::Directional ? 0.0 : 0.5 + 8.0 * r.next();
        l.outerCone = 0.2 + 1.2 * r.next(); l.innerCone = l.outerCone * r.next();
        ls.push_back(l);
    }
    return ls;
}
// A random material over the families of the material checks.
inline raw::pbr::Material material(Rng& r, int family) {
    raw::pbr::Material m;
    m.baseColor = {r.next(), r.next(), r.next()}; m.metallic = r.next(); m.roughness = r.next();
    m.ior = 1.0 + 1.5 * r.next(); m.specular = r.next(); m.specularColor = {r.next(), r.next(), r.next()};
    switch (family % 6) {
    case 1: m.clearcoat = r.next(); m.clearcoatRoughness = r.next(); break;
    case 2: m.sheenColor = {r.next(), r.next(), r.next()}; m.sheenRoughness = r.next(); break;
    case 3: m.transmission = r.next(); m.metallic *= 0.5; m.volume = r.next() < 0.5; m.thickness = r.next();
            m.attenuationDistance = 0.5 + r.next(); m.attenuationColor = {0.2 + 0.8 * r.next(), 0.2 + 0.8 * r.next(), 0.2 + 0.8 * r.next()}; break;
    case 4: m.iridescence = r.next(); m.iridescenceThickness = 100 + 900 * r.next(); break;
    case 5: m.emissive = {r.next(), r.next(), r.next()}; m.emissiveStrength = 50.0 * r.next(); break;
    default: break;
    }
    return m;
}
// A surface sample at p facing the eye: normal within 85 degrees of the view, tangent at random.
inline Sample sampleAt(Rng& r, D3 p, int family) {
    Sample s;
    s.p = p;
    s.v = unit({-p.x, -p.y, -p.z});
    D3 n;
    do { n = randomDir(r); } while (n.x * s.v.x + n.y * s.v.y + n.z * s.v.z < 0.09);
    s.n = n;
    const D3 a = randomDir(r), d = {a.x - n.x * (a.x * n.x + a.y * n.y + a.z * n.z), a.y - n.y * (a.x * n.x + a.y * n.y + a.z * n.z),
                                    a.z - n.z * (a.x * n.x + a.y * n.y + a.z * n.z)};
    s.t = unit(d);
    s.m = material(r, family);
    return s;
}
}  // namespace raw::lighting::scenes
