// The Lambertian path tracer of the baker: see raw/renderer/bake.hpp.
#include "raw/renderer/bake.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include <algorithm>
#include <cmath>
namespace raw::bake {
namespace {
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 cross(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
D3 unit(D3 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
D3 add(D3 a, D3 b, double s = 1.0) { return {a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
Vec3 f(D3 a) { return {float(a.x), float(a.y), float(a.z)}; }
constexpr double kEps = 1e-4;
}  // namespace

struct Tracer::Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) {                  // splitmix64, so nearby seeds give unrelated streams
        std::uint64_t z = seed + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        s = (z ^ (z >> 31)) | 1ull;
    }
    double next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return double((s * 0x2545F4914F6CDD1Dull) >> 11) * (1.0 / 9007199254740992.0); }
    D3 cosine(D3 n) {                                   // Malley: a disc sample lifted to the hemisphere
        const double u1 = next(), u2 = next(), r = std::sqrt(u1), ph = 2.0 * pbr::kPi * u2;
        D3 t, b; lighting::cube::basis(n, t, b);
        const double x = r * std::cos(ph), y = r * std::sin(ph), z = std::sqrt(std::max(0.0, 1.0 - u1));
        return unit({t.x * x + b.x * y + n.x * z, t.y * x + b.y * y + n.y * z, t.z * x + b.z * y + n.z * z});
    }
};

void quadFrame(const Quad& q, D3& t, D3& b, D3& n) {
    n = unit(cross(q.e1, q.e2));
    t = unit(q.e1);
    b = cross(n, t);
}

Tracer::Tracer(const Room& room) : room_(room) {
    std::vector<Tri> tris;
    for (std::size_t k = 0; k < room.quads.size(); ++k) {
        const Quad& q = room.quads[k];
        const D3 a = q.o, b = add(q.o, q.e1), c = add(add(q.o, q.e1), q.e2), d = add(q.o, q.e2);
        tris.push_back({f(a), f(b), f(c)}); tris.push_back({f(a), f(c), f(d)});
        quadOf_.push_back(int(k)); quadOf_.push_back(int(k));
    }
    bvh_.build(tris);
}

int Tracer::trace(D3 o, D3 d, double& t) const {
    Hit h;
    if (!bvh_.closest({f(o), f(d)}, 1e30f, h)) return -1;
    t = h.t;
    return quadOf_[std::size_t(h.tri)];
}

Rgb Tracer::direct(D3 p, D3 n, D3 b) const {
    Rgb sum;
    for (const lighting::Light& l : room_.lights) {
        D3 L;
        const Rgb e = lighting::illuminance(l, p, L);
        const double c = std::max(0.0, dot(L, b));
        if (c <= 0.0 || dot(L, n) <= 0.0 || (e.r == 0.0 && e.g == 0.0 && e.b == 0.0)) continue;
        const D3 dl{l.position.x - p.x, l.position.y - p.y, l.position.z - p.z};
        const double dist = std::sqrt(dot(dl, dl));
        if (bvh_.occluded({f(add(p, n, kEps)), f(L)}, float(dist - 2 * kEps))) continue;
        sum = {sum.r + e.r * c / pbr::kPi, sum.g + e.g * c / pbr::kPi, sum.b + e.b * c / pbr::kPi};
    }
    return sum;
}

// Radiance arriving at p from direction dir: emission on every surface hit, next-event
// estimation toward the point lights, Russian roulette after the third bounce.
Rgb Tracer::incoming(D3 p, D3 dir, Rng& rng) const {
    Rgb L, thr{1, 1, 1};
    D3 o = add(p, dir, kEps), d = dir;
    for (int depth = 0; depth < 64; ++depth) {
        double t;
        const int qi = trace(o, d, t);
        if (qi < 0) break;
        const Quad& q = room_.quads[std::size_t(qi)];
        D3 qt, qb, n; quadFrame(q, qt, qb, n);
        if (dot(n, d) >= 0.0) break;                    // the back of a one-sided quad is black
        const D3 x = add(o, d, t);
        L = {L.r + thr.r * q.emission.r, L.g + thr.g * q.emission.g, L.b + thr.b * q.emission.b};
        if (q.albedo.max3() <= 0.0) break;
        const Rgb e = direct(x, n, n);
        L = {L.r + thr.r * q.albedo.r * e.r, L.g + thr.g * q.albedo.g * e.g, L.b + thr.b * q.albedo.b * e.b};
        thr = {thr.r * q.albedo.r, thr.g * q.albedo.g, thr.b * q.albedo.b};
        if (depth >= 3) {
            const double keep = std::min(thr.max3(), 0.95);
            if (rng.next() >= keep) break;
            thr = {thr.r / keep, thr.g / keep, thr.b / keep};
        }
        d = rng.cosine(n);
        o = add(x, n, kEps);
    }
    return L;
}

Rgb Tracer::irradiance(D3 p, D3 n, int paths, std::uint64_t seed, bool withDirect) const {
    Rng rng(seed);
    Rgb sum;
    for (int k = 0; k < paths; ++k) {
        const Rgb l = incoming(p, rng.cosine(n), rng);
        sum = {sum.r + l.r, sum.g + l.g, sum.b + l.b};
    }
    const Rgb e = withDirect ? direct(p, n, n) : Rgb{};
    return {e.r + sum.r / paths, e.g + sum.g / paths, e.b + sum.b / paths};
}

// Normalised to the lobe: the basis lobe max(0, w.b) loses the part of itself below the
// surface, (1 + n.b) / 2 of it remains, and the value is divided by that, so uniform light
// gives every basis value the flat irradiance and a flat normal shades exactly. The plain
// form left the flat normal 21% dark (first run, 2026-10-10: median error 0.17).
Rgb Tracer::basisIrradiance(D3 p, D3 n, D3 b, int paths, std::uint64_t seed) const {
    Rng rng(seed);
    const double keep = 0.5 * (1.0 + dot(n, b));
    Rgb sum;
    for (int k = 0; k < paths; ++k) {
        const D3 w = rng.cosine(b);                     // cosine about the basis vector; light from below the surface does not count
        if (dot(w, n) <= 0.0) continue;
        const Rgb l = incoming(p, w, rng);
        sum = {sum.r + l.r, sum.g + l.g, sum.b + l.b};
    }
    const Rgb e = direct(p, n, b);
    return {(e.r + sum.r / paths) / keep, (e.g + sum.g / paths) / keep, (e.b + sum.b / paths) / keep};
}

}  // namespace raw::bake
