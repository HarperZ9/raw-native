// The SDF program: builders and the float64 evaluator. See raw/renderer/sdf.hpp.
#include "raw/renderer/sdf.hpp"
#include <algorithm>
#include <cmath>
namespace raw::sdf {
namespace {
D3 add(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 sub(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 mul(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dotd(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double len(D3 a) { return std::sqrt(dotd(a, a)); }
double clampd(double v, double a, double b) { return std::min(std::max(v, a), b); }
double bulb(D3 p, int iters, double power) {
    D3 z = p;
    double dr = 1.0, r = len(z);
    for (int i = 0; i < iters; ++i) {
        r = len(z);
        if (r > 2.0) break;
        const double theta = std::acos(clampd(z.z / r, -1.0, 1.0)) * power, phi = std::atan2(z.y, z.x) * power;
        dr = std::pow(r, power - 1.0) * power * dr + 1.0;
        const double zr = std::pow(r, power);
        z = add(mul({std::sin(theta) * std::cos(phi), std::sin(phi) * std::sin(theta), std::cos(theta)}, zr), p);
    }
    r = len(z);
    return 0.5 * std::log(r) * r / dr;
}
double mandelbox(D3 p, int iters, double scale) {
    constexpr double fold = 1.0, minR2 = 0.25, fixedR2 = 1.0;
    D3 z = p;
    double dr = 1.0;
    for (int i = 0; i < iters; ++i) {
        z = {clampd(z.x, -fold, fold) * 2.0 - z.x, clampd(z.y, -fold, fold) * 2.0 - z.y, clampd(z.z, -fold, fold) * 2.0 - z.z};
        const double r2 = dotd(z, z);
        if (r2 < minR2) { const double k = fixedR2 / minR2; z = mul(z, k); dr *= k; }
        else if (r2 < fixedR2) { const double k = fixedR2 / r2; z = mul(z, k); dr *= k; }
        z = add(mul(z, scale), p);
        dr = dr * std::fabs(scale) + 1.0;
    }
    return len(z) / std::fabs(dr);
}
double primitive(const float* n, D3 p) {
    switch (int(n[0])) {
    case kSphere: return len(p) - n[2];
    case kBox: case kRoundBox: {
        const double r = int(n[0]) == kRoundBox ? n[5] : 0.0;
        const D3 q{std::fabs(p.x) - (n[2] - r), std::fabs(p.y) - (n[3] - r), std::fabs(p.z) - (n[4] - r)};
        return len({std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0)}) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0) - r;
    }
    case kTorus: { const double a = std::sqrt(p.x * p.x + p.z * p.z) - n[2]; return std::sqrt(a * a + p.y * p.y) - n[3]; }
    case kCapsule: {
        const D3 a{n[2], n[3], n[4]}, b{n[5], n[6], n[7]}, pa = sub(p, a), ba = sub(b, a);
        const double h = clampd(dotd(pa, ba) / dotd(ba, ba), 0.0, 1.0);
        return len(sub(pa, mul(ba, h))) - n[8];
    }
    case kCylinder: {
        const double dx = std::sqrt(p.x * p.x + p.z * p.z) - n[2], dy = std::fabs(p.y) - n[3];
        return std::min(std::max(dx, dy), 0.0) + std::sqrt(std::max(dx, 0.0) * std::max(dx, 0.0) + std::max(dy, 0.0) * std::max(dy, 0.0));
    }
    case kPlane: return p.x * n[2] + p.y * n[3] + p.z * n[4] + n[5];
    case kBulb: return bulb(p, int(n[2]), n[3]);
    default: return mandelbox(p, int(n[2]), n[3]);
    }
}
}  // namespace

void Program::sphere(float r, int m) { nodes.insert(nodes.end(), {float(kSphere), float(m), r}); nodes.resize(nodes.size() + 13); }
void Program::box(Vec3 h, int m) { nodes.insert(nodes.end(), {float(kBox), float(m), h.x, h.y, h.z}); nodes.resize(nodes.size() + 11); }
void Program::roundBox(Vec3 h, float r, int m) { nodes.insert(nodes.end(), {float(kRoundBox), float(m), h.x, h.y, h.z, r}); nodes.resize(nodes.size() + 10); }
void Program::torus(float R, float r, int m) { nodes.insert(nodes.end(), {float(kTorus), float(m), R, r}); nodes.resize(nodes.size() + 12); }
void Program::capsule(Vec3 a, Vec3 b, float r, int m) { nodes.insert(nodes.end(), {float(kCapsule), float(m), a.x, a.y, a.z, b.x, b.y, b.z, r}); nodes.resize(nodes.size() + 7); }
void Program::cylinder(float r, float h, int m) { nodes.insert(nodes.end(), {float(kCylinder), float(m), r, h}); nodes.resize(nodes.size() + 12); }
void Program::plane(Vec3 n, float h, int m) { nodes.insert(nodes.end(), {float(kPlane), float(m), n.x, n.y, n.z, h}); nodes.resize(nodes.size() + 10); }
void Program::bulb(int it, float pw, int m) { nodes.insert(nodes.end(), {float(kBulb), float(m), float(it), pw}); nodes.resize(nodes.size() + 12); fractal = true; }
void Program::mandelbox(int it, float sc, int m) { nodes.insert(nodes.end(), {float(kMandelbox), float(m), float(it), sc}); nodes.resize(nodes.size() + 12); fractal = true; }
void Program::op(Op o, float k) { nodes.insert(nodes.end(), {float(o), 0.0f, k}); nodes.resize(nodes.size() + 13); if (o == kSmooth) fractal = true; }
void Program::push(Vec3 t, const float r[9], float s) {
    nodes.insert(nodes.end(), {float(kPushFrame), 0.0f, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], t.x, t.y, t.z, s, 0.0f});
}
void Program::push(Vec3 t, float s) { const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; push(t, id, s); }
void Program::pop() { nodes.insert(nodes.end(), {float(kPopFrame)}); nodes.resize(nodes.size() + 15); }
void Program::repeat(Vec3 per) { nodes.insert(nodes.end(), {float(kRepeat), 0.0f, per.x, per.y, per.z}); nodes.resize(nodes.size() + 11); }

Sample eval(const Program& prog, D3 q) {
    Sample st[kStack];
    D3 fp[kStack];
    double fs[kStack];
    int sp = 0, fsp = 0;
    D3 p = q;
    double scale = 1.0;
    for (std::size_t i = 0; i + kNodeFloats <= prog.nodes.size(); i += kNodeFloats) {
        const float* n = &prog.nodes[i];
        const int o = int(n[0]);
        if (o < kUnion) { st[sp++] = {primitive(n, p) * scale, int(n[1])}; continue; }
        if (o == kPushFrame) {
            fp[fsp] = p; fs[fsp++] = scale;
            const D3 d = sub(p, {n[11], n[12], n[13]});
            p = mul({n[2] * d.x + n[3] * d.y + n[4] * d.z, n[5] * d.x + n[6] * d.y + n[7] * d.z, n[8] * d.x + n[9] * d.y + n[10] * d.z}, 1.0 / n[14]);
            scale *= n[14];
            continue;
        }
        if (o == kPopFrame) { p = fp[--fsp]; scale = fs[fsp]; continue; }
        if (o == kRepeat) {
            const auto rep = [](double v, double per) { return per > 0.0 ? v - per * std::floor(v / per + 0.5) : v; };
            p = {rep(p.x, n[2]), rep(p.y, n[3]), rep(p.z, n[4])};
            continue;
        }
        const Sample b = st[--sp], a = st[--sp];
        Sample r = a;
        if (o == kUnion) r = a.d <= b.d ? a : b;
        else if (o == kSubtract) r = {std::max(a.d, -b.d), a.mat};
        else if (o == kIntersect) r = a.d >= b.d ? a : b;
        else {
            const double k = n[2], h = clampd(0.5 + 0.5 * (b.d - a.d) / k, 0.0, 1.0);
            r = {b.d + (a.d - b.d) * h - k * h * (1.0 - h), h > 0.5 ? a.mat : b.mat};
        }
        st[sp++] = r;
    }
    return sp ? st[sp - 1] : Sample{1e30, -1};
}

}  // namespace raw::sdf
