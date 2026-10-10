// Cascades and shadow-map lookups: see raw/renderer/shadows.hpp.
#include "raw/renderer/shadows.hpp"
#include <algorithm>
#include <cmath>
namespace raw::shadows {
namespace {
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 sub(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 add(D3 a, D3 b, double s = 1.0) { return {a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
D3 cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
D3 unit(D3 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
D3 d3(Vec3 v) { return {v.x, v.y, v.z}; }
constexpr double kPi = 3.14159265358979323846, kCasterReach = 50.0, kMaxBlocker = 10.0;
}  // namespace

D3 CascadeSet::toLight(D3 p) const { return {dot(p, right), dot(p, up), dot(p, light)}; }

std::array<float, 16> CascadeSet::matrix(int k) const {
    const Cascade& q = c[std::size_t(k)];
    const double r = q.radius, dz = q.zFar - q.zNear;
    return {float(right.x / r), float(right.y / r), float(right.z / r), float(-q.centre.x / r),
            float(up.x / r), float(up.y / r), float(up.z / r), float(-q.centre.y / r),
            float(light.x / dz), float(light.y / dz), float(light.z / dz), float(-q.zNear / dz),
            0.0f, 0.0f, 0.0f, 1.0f};
}

CascadeSet fitCascades(const Camera& cam, D3 lightDir, double shadowFar, int size, bool stable) {
    CascadeSet cs;
    cs.size = size;
    cs.light = unit(lightDir);
    {   // Duff et al. 2017 basis around the light
        const D3 n = cs.light;
        const double s = n.z >= 0.0 ? 1.0 : -1.0, a = -1.0 / (s + n.z), b = n.x * n.y * a;
        cs.right = {1.0 + s * n.x * n.x * a, s * b, -s * n.x};
        cs.up = {b, s + n.y * n.y * a, -n.y};
    }
    const D3 eye = d3(cam.eye), f = unit(sub(d3(cam.center), eye)), sv = unit(cross(f, d3(cam.up))), uv = cross(sv, f);
    const double nearZ = cam.nearZ, ty = std::tan(0.5 * cam.fovy), tx = ty * cam.aspect;
    double prev = nearZ;
    for (int k = 0; k < kCascades; ++k) {
        const double i = double(k + 1) / kCascades;
        const double split = 0.75 * nearZ * std::pow(shadowFar / nearZ, i) + 0.25 * (nearZ + (shadowFar - nearZ) * i);
        D3 corners[8]; int m = 0;
        for (double d : {prev, split}) for (double sx : {-1.0, 1.0}) for (double sy : {-1.0, 1.0})
            corners[m++] = add(add(add(eye, f, d), sv, sx * tx * d), uv, sy * ty * d);
        Cascade& q = cs.c[std::size_t(k)];
        q.split = split;
        if (stable) {
            D3 mid{0, 0, 0};
            for (const D3& p : corners) mid = add(mid, p, 1.0 / 8.0);
            double r = 0.0;
            for (const D3& p : corners) r = std::max(r, std::sqrt(dot(sub(p, mid), sub(p, mid))));
            q.radius = std::ceil(r * 64.0) / 64.0;               // the same radius at every camera angle
            D3 c = cs.toLight(mid);
            const double texel = 2.0 * q.radius / size;
            c.x = std::floor(c.x / texel) * texel; c.y = std::floor(c.y / texel) * texel;   // whole texels: no swimming
            q.centre = c;
            q.zNear = c.z - q.radius - kCasterReach; q.zFar = c.z + q.radius;
        } else {
            D3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
            for (const D3& p : corners) {
                const D3 l = cs.toLight(p);
                lo = {std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z)};
                hi = {std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z)};
            }
            q.centre = {0.5 * (lo.x + hi.x), 0.5 * (lo.y + hi.y), 0.5 * (lo.z + hi.z)};
            q.radius = 0.5 * std::max(hi.x - lo.x, hi.y - lo.y);
            q.zNear = lo.z - kCasterReach; q.zFar = hi.z;
        }
        prev = split;
    }
    return cs;
}

int cascadeOf(const CascadeSet& cs, double viewDepth) {
    for (int k = 0; k < kCascades; ++k) if (viewDepth <= cs.c[std::size_t(k)].split) return k;
    return -1;
}

const std::vector<float>& poissonTaps() {
    // Vogel discs (golden-angle spirals): 16 blocker taps, then 25 filter taps, as x, y pairs.
    static const std::vector<float> taps = [] {
        std::vector<float> t;
        for (int n : {16, 25})
            for (int i = 0; i < n; ++i) {
                const double r = std::sqrt((i + 0.5) / n), a = i * 2.399963229728653;
                t.push_back(float(r * std::cos(a))); t.push_back(float(r * std::sin(a)));
            }
        return t;
    }();
    return taps;
}

double lookup(const CascadeSet& cs, const std::array<const ShadowMap*, kCascades>& maps, D3 p, D3 n, double viewDepth, Filter f,
              const LookupParams& lp) {
    const int k = cascadeOf(cs, viewDepth);
    if (k < 0) return 1.0;
    const Cascade& q = cs.c[std::size_t(k)];
    const ShadowMap& m = *maps[std::size_t(k)];
    const double tw = 2.0 * q.radius / cs.size, range = q.zFar - q.zNear;
    const D3 l = cs.toLight(add(p, n, lp.normalOffset * tw));
    const double u = (l.x - q.centre.x) / q.radius, v = (l.y - q.centre.y) / q.radius;
    const double d = (l.z - q.zNear) / range - lp.depthBias * tw / range;
    const double fx = (u * 0.5 + 0.5) * cs.size, fy = (1.0 - (v * 0.5 + 0.5)) * cs.size;
    const auto lit = [&](double x, double y) {
        const int ix = int(std::floor(x)), iy = int(std::floor(y));
        if (ix < 0 || iy < 0 || ix >= m.size || iy >= m.size) return 1.0;
        return d <= m.depth[std::size_t(iy) * m.size + ix] ? 1.0 : 0.0;
    };
    if (f == Filter::Hard) return lit(fx, fy);
    if (f == Filter::Pcf) {
        double s = 0.0;
        for (int b = -2; b <= 2; ++b) for (int a = -2; a <= 2; ++a) s += (3 - std::abs(a)) * (3 - std::abs(b)) * lit(fx + a, fy + b);
        return s / 81.0;
    }
    const std::vector<float>& t = poissonTaps();
    const double tanSun = std::tan(lp.sunAngle), rs = std::clamp(kMaxBlocker * tanSun / tw, 1.0, 16.0);
    double blocker = 0.0; int found = 0;
    for (int i = 0; i < 16; ++i) {
        const int ix = int(std::floor(fx + t[2 * i] * rs)), iy = int(std::floor(fy + t[2 * i + 1] * rs));
        if (ix < 0 || iy < 0 || ix >= m.size || iy >= m.size) continue;
        const double z = m.depth[std::size_t(iy) * m.size + ix];
        if (z < d) { blocker += z; ++found; }
    }
    if (!found) return 1.0;
    const double penumbra = (d - blocker / found) * range * tanSun, rk = std::clamp(penumbra / tw, 1.0, 16.0);
    double s = 0.0;
    for (int i = 16; i < 41; ++i) s += lit(fx + t[2 * i] * rk, fy + t[2 * i + 1] * rk);
    return s / 25.0;
}

}  // namespace raw::shadows
