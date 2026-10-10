// Deterministic mesh building for the owned assets: see src/tools/owned_build.hpp.
#include "owned_build.hpp"
#include <cmath>
namespace raw::owned {

// Range reduction to [-pi, pi] by a double-precision 2 pi, then to [-pi/2, pi/2] by symmetry,
// then the Taylor series to x^23 (error below 1e-16 there). Only +, -, *, / and floor.
double dsin(double x) {
    const double twoPi = 6.283185307179586476925, pi = 3.141592653589793238463, half = 1.570796326794896619231;
    x -= twoPi * std::floor(x / twoPi + 0.5);
    if (x > half) x = pi - x;
    else if (x < -half) x = -pi - x;
    const double x2 = x * x;
    double term = x, sum = x;
    for (int k = 1; k <= 11; ++k) {
        term *= -x2 / double((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sum;
}
double dcos(double x) { return dsin(x + 1.570796326794896619231); }

const char* extName(int bit) {
    static const char* names[kExtCount] = {"KHR_materials_ior", "KHR_materials_specular", "KHR_materials_clearcoat", "KHR_materials_sheen",
                                           "KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_anisotropy",
                                           "KHR_materials_iridescence", "KHR_materials_emissive_strength"};
    return bit >= 0 && bit < kExtCount ? names[bit] : "";
}

namespace build {
P3 unit(P3 a) {
    const double l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0 / l) : P3{0, 1, 0};
}

void grid(Part& p, const Surface& f, int nu, int nv, bool wrapU, bool flip) {
    const uint32_t base = uint32_t(p.positions.size());
    const int cols = wrapU ? nu : nu + 1;
    const double h = 1e-5;
    for (int j = 0; j <= nv; ++j) for (int i = 0; i < cols; ++i) {
        const double u = double(i) / nu, v = double(j) / nv;
        const P3 q = f(u, v);
        // One-sided differences at the edges, central inside.
        const double u0 = (wrapU || i > 0) ? u - h : u, u1 = (wrapU || i < nu) ? u + h : u;
        const double v0 = j > 0 ? v - h : v, v1 = j < nv ? v + h : v;
        P3 n = unit(cross((f(u1, v) - f(u0, v)) * (1.0 / (u1 - u0)), (f(u, v1) - f(u, v0)) * (1.0 / (v1 - v0))));
        if (flip) n = n * -1.0;
        p.positions.push_back({float(q.x), float(q.y), float(q.z)});
        p.normals.push_back({float(n.x), float(n.y), float(n.z)});
    }
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i) {
        const uint32_t a = base + uint32_t(j * cols + i), b = base + uint32_t(j * cols + (i + 1) % cols);
        const uint32_t c = base + uint32_t((j + 1) * cols + (i + 1) % cols), d = base + uint32_t((j + 1) * cols + i);
        if (!flip) p.indices.insert(p.indices.end(), {a, b, c, a, c, d});
        else p.indices.insert(p.indices.end(), {a, c, b, a, d, c});
    }
}

void box(Part& p, P3 lo, P3 hi) {
    const P3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const P3& d : n) {
        // Two tangents with t x b = d.
        const P3 t = std::fabs(d.y) > 0.5 ? P3{1, 0, 0} : P3{0, 1, 0}, b = cross(d, t);
        const P3 c{(lo.x + hi.x) * 0.5 + d.x * (hi.x - lo.x) * 0.5, (lo.y + hi.y) * 0.5 + d.y * (hi.y - lo.y) * 0.5,
                   (lo.z + hi.z) * 0.5 + d.z * (hi.z - lo.z) * 0.5};
        const P3 ext{(hi.x - lo.x) * 0.5, (hi.y - lo.y) * 0.5, (hi.z - lo.z) * 0.5};
        const double et = std::fabs(t.x) * ext.x + std::fabs(t.y) * ext.y + std::fabs(t.z) * ext.z;
        const double eb = std::fabs(b.x) * ext.x + std::fabs(b.y) * ext.y + std::fabs(b.z) * ext.z;
        const uint32_t base = uint32_t(p.positions.size());
        for (int k = 0; k < 4; ++k) {
            const double s = (k == 1 || k == 2) ? 1.0 : -1.0, r = k >= 2 ? 1.0 : -1.0;
            const P3 q = c + t * (s * et) + b * (r * eb);
            p.positions.push_back({float(q.x), float(q.y), float(q.z)});
            p.normals.push_back({float(d.x), float(d.y), float(d.z)});
        }
        // Corners: (-t,-b), (+t,-b), (+t,+b), (-t,+b); t x b = d, so this order faces d.
        p.indices.insert(p.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

void lathe(Part& p, const std::vector<std::pair<double, double>>& prof, int segments, P3 at, bool capBottom, bool capTop,
           const std::function<double(double, double)>& scale) {
    const int n = int(prof.size()) - 1;
    const auto f = [&](double u, double v) {
        const double s = v * n;
        int k = int(std::floor(s));
        if (k >= n) k = n - 1;
        const double t = s - k;
        const double r = prof[std::size_t(k)].first * (1 - t) + prof[std::size_t(k + 1)].first * t;
        const double y = prof[std::size_t(k)].second * (1 - t) + prof[std::size_t(k + 1)].second * t;
        const double a = 2.0 * kPi * u, rs = r * (scale ? scale(a, y) : 1.0);
        // Angle increases from +x toward -z, so d/du x d/dv points outward for a rising profile.
        return P3{at.x + rs * dcos(a), at.y + y, at.z - rs * dsin(a)};
    };
    grid(p, f, segments, n, true, false);
    for (int end = 0; end < 2; ++end) {
        if ((end == 0 && !capBottom) || (end == 1 && !capTop)) continue;
        const double v = end == 0 ? 0.0 : 1.0, ny = end == 0 ? -1.0 : 1.0;
        const P3 c = f(0.0, v);
        const uint32_t centre = uint32_t(p.positions.size());
        p.positions.push_back({float(at.x), float(c.y), float(at.z)});
        p.normals.push_back({0.0f, float(ny), 0.0f});
        for (int i = 0; i < segments; ++i) {
            const P3 q = f(double(i) / segments, v);
            p.positions.push_back({float(q.x), float(q.y), float(q.z)});
            p.normals.push_back({0.0f, float(ny), 0.0f});
        }
        for (int i = 0; i < segments; ++i) {
            const uint32_t a = centre + 1 + uint32_t(i), b = centre + 1 + uint32_t((i + 1) % segments);
            // The rim runs from +x toward -z: counter-clockwise seen from above.
            if (end == 1) p.indices.insert(p.indices.end(), {centre, a, b});
            else p.indices.insert(p.indices.end(), {centre, b, a});
        }
    }
}

void translate(Part& p, std::size_t first, P3 d) {
    for (std::size_t i = first; i < p.positions.size(); ++i)
        p.positions[i] = {float(double(p.positions[i].x) + d.x), float(double(p.positions[i].y) + d.y), float(double(p.positions[i].z) + d.z)};
}
}  // namespace build
}  // namespace raw::owned
