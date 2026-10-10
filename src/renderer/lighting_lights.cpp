// Punctual lights in physical units: see raw/renderer/lighting.hpp.
#include "raw/renderer/lighting.hpp"
#include <algorithm>
#include <cmath>
namespace raw::lighting {
namespace {
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
}  // namespace

Rgb illuminance(const Light& l, D3 p, D3& toLight) {
    if (l.type == LightType::Directional) {
        toLight = {-l.direction.x, -l.direction.y, -l.direction.z};
        return {l.color.r * l.intensity, l.color.g * l.intensity, l.color.b * l.intensity};
    }
    const D3 d{l.position.x - p.x, l.position.y - p.y, l.position.z - p.z};
    const double d2 = dot(d, d), dist = std::sqrt(d2);
    if (dist <= 0.0) { toLight = {0, 0, 1}; return {}; }
    toLight = {d.x / dist, d.y / dist, d.z / dist};
    double att = 1.0 / d2;
    if (l.range > 0.0) {
        const double q = dist / l.range, w = std::clamp(1.0 - q * q * q * q, 0.0, 1.0);
        att *= w * w;
    }
    if (l.type == LightType::Spot) {
        const double scale = 1.0 / std::max(0.001, std::cos(l.innerCone) - std::cos(l.outerCone));
        const double offset = -std::cos(l.outerCone) * scale;
        const double cd = -dot(l.direction, toLight);               // the light's axis against the ray to p
        const double a = std::clamp(cd * scale + offset, 0.0, 1.0);
        att *= a * a;
    }
    const double e = l.intensity * att;
    return {l.color.r * e, l.color.g * e, l.color.b * e};
}

double ev100(double aperture, double shutter, double iso) { return std::log2(aperture * aperture / shutter * 100.0 / iso); }
double exposure(double ev) { return 1.0 / (1.2 * std::exp2(ev)); }

D3 toLocal(const Sample& s, D3 w) {
    const D3 b = cross(s.n, s.t);
    return {dot(w, s.t), dot(w, b), dot(w, s.n)};
}

Rgb shadePunctual(const Sample& s, const pbr::Tables& t, const std::vector<Light>& lights, const std::vector<int>& list) {
    Rgb sum = pbr::emission(s.m);
    const D3 wo = toLocal(s, s.v);
    for (int k : list) {
        D3 L;
        const Rgb e = illuminance(lights[std::size_t(k)], s.p, L);
        if (e.r == 0.0 && e.g == 0.0 && e.b == 0.0) continue;
        const D3 wi = toLocal(s, L);
        const Rgb f = pbr::eval(s.m, t, wo, wi);
        const double c = std::fabs(wi.z);
        sum = {sum.r + f.r * e.r * c, sum.g + f.g * e.g * c, sum.b + f.b * e.b * c};
    }
    return sum;
}

}  // namespace raw::lighting
