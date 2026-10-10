// Rooms, directional lightmaps and ambient cubes: see raw/renderer/bake.hpp.
#include "raw/renderer/bake.hpp"
#include "raw/core/parallel.hpp"
#include <algorithm>
#include <cmath>
namespace raw::bake {
namespace {
D3 add(D3 a, D3 b, double s = 1.0) { return {a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
double len(D3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
// The five outward faces of a box standing on the floor (no bottom).
void addBox(Room& r, D3 lo, D3 hi, Rgb albedo) {
    const double dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z;
    r.quads.push_back({{lo.x, hi.y, lo.z}, {0, 0, dz}, {dx, 0, 0}, albedo, {}});    // +y
    r.quads.push_back({{lo.x, lo.y, lo.z}, {0, 0, dz}, {0, dy, 0}, albedo, {}});    // -x
    r.quads.push_back({{hi.x, lo.y, lo.z}, {0, dy, 0}, {0, 0, dz}, albedo, {}});    // +x
    r.quads.push_back({{lo.x, lo.y, lo.z}, {0, dy, 0}, {dx, 0, 0}, albedo, {}});    // -z
    r.quads.push_back({{lo.x, lo.y, hi.z}, {dx, 0, 0}, {0, dy, 0}, albedo, {}});    // +z
}
// The six inward walls of the box [lo, hi].
void addWalls(Room& r, D3 lo, D3 hi, Rgb floorCeil, Rgb left, Rgb right, Rgb back, Rgb front, Rgb emission) {
    const double dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z;
    r.quads.push_back({{lo.x, lo.y, lo.z}, {0, 0, dz}, {dx, 0, 0}, floorCeil, emission});   // floor, +y
    r.quads.push_back({{lo.x, hi.y, lo.z}, {dx, 0, 0}, {0, 0, dz}, floorCeil, emission});   // ceiling, -y
    r.quads.push_back({{lo.x, lo.y, lo.z}, {dx, 0, 0}, {0, dy, 0}, back, emission});        // back, +z
    r.quads.push_back({{lo.x, lo.y, hi.z}, {0, dy, 0}, {dx, 0, 0}, front, emission});       // front, -z
    r.quads.push_back({{lo.x, lo.y, lo.z}, {0, dy, 0}, {0, 0, dz}, left, emission});        // left, +x
    r.quads.push_back({{hi.x, lo.y, lo.z}, {0, 0, dz}, {0, dy, 0}, right, emission});       // right, -x
}
}  // namespace

Room testRoom() {
    Room r;
    const Rgb white{0.75, 0.75, 0.75};
    addWalls(r, {-2, 0, -2}, {2, 3, 2}, white, {0.75, 0.12, 0.1}, {0.12, 0.6, 0.15}, white, white, {});
    r.quads.push_back({{-0.5, 2.999, -0.5}, {1, 0, 0}, {0, 0, 1}, {}, {10, 10, 10}});   // the ceiling panel, facing down
    addBox(r, {-1.3, 0, -1.4}, {-0.3, 1.6, -0.4}, white);
    addBox(r, {0.3, 0, -0.2}, {1.3, 0.8, 0.8}, white);
    lighting::Light l;
    l.type = lighting::LightType::Point; l.position = {0.6, 2.2, 0.9}; l.intensity = 80; l.color = {1.0, 0.92, 0.8};
    r.lights.push_back(l);
    return r;
}

Room furnaceBox(double albedo, double emission) {
    Room r;
    const Rgb a{albedo, albedo, albedo};
    addWalls(r, {-1, -1, -1}, {1, 1, 1}, a, a, a, a, a, {emission, emission, emission});
    return r;
}

D3 Lightmap::texelCentre(const Room& room, int i, int j) const {
    const Quad& q = room.quads[std::size_t(quad)];
    return add(add(q.o, q.e1, (i + 0.5) / n1), q.e2, (j + 0.5) / n2);
}

std::vector<Lightmap> bakeLightmaps(const Tracer& tr, double texelsPerMetre, int paths, int threads) {
    const Room& room = tr.room();
    std::vector<Lightmap> maps;
    for (std::size_t k = 0; k < room.quads.size(); ++k) {
        const Quad& q = room.quads[k];
        if (q.albedo.max3() <= 0.0) continue;           // emitters only glow; they need no map
        Lightmap lm;
        lm.quad = int(k);
        lm.n1 = std::max(1, int(std::lround(len(q.e1) * texelsPerMetre)));
        lm.n2 = std::max(1, int(std::lround(len(q.e2) * texelsPerMetre)));
        for (auto& b : lm.basis) b.assign(std::size_t(lm.n1) * lm.n2, {});
        parallelRows(lm.n2, threads, [&](int j) {
            for (int i = 0; i < lm.n1; ++i)
                bakeTexel(tr, lm, i, j, paths, (std::uint64_t(k) << 40) ^ (std::uint64_t(j) << 24) ^ (std::uint64_t(i) << 4));
        });
        maps.push_back(std::move(lm));
    }
    return maps;
}

void bakeTexel(const Tracer& tr, Lightmap& lm, int i, int j, int paths, std::uint64_t seed) {
    const Room& room = tr.room();
    D3 t, bt, n; quadFrame(room.quads[std::size_t(lm.quad)], t, bt, n);
    const D3 p = lm.texelCentre(room, i, j);
    const std::size_t at = std::size_t(j) * lm.n1 + i;
    Rgb sum;
    for (int c = 0; c < 3; ++c) {
        const D3 w{t.x * kBasis[c].x + bt.x * kBasis[c].y + n.x * kBasis[c].z, t.y * kBasis[c].x + bt.y * kBasis[c].y + n.y * kBasis[c].z,
                   t.z * kBasis[c].x + bt.z * kBasis[c].y + n.z * kBasis[c].z};
        const Rgb v = tr.basisIrradiance(p, n, w, paths, seed * 4 + std::uint64_t(c));
        lm.basis[c][at] = v;
        sum = {sum.r + v.r, sum.g + v.g, sum.b + v.b};
    }
    const Rgb flat = tr.irradiance(p, n, paths, seed * 4 + 3);
    const auto scale = [](double f, double s) { return s > 0.0 ? 3.0 * f / s : 0.0; };
    const Rgb k{scale(flat.r, sum.r), scale(flat.g, sum.g), scale(flat.b, sum.b)};
    for (int c = 0; c < 3; ++c) { Rgb& v = lm.basis[c][at]; v = {v.r * k.r, v.g * k.g, v.b * k.b}; }
}

Rgb shadeRnm(const Lightmap& lm, int i, int j, D3 nt) {
    double w[3], sum = 0.0;
    for (int c = 0; c < 3; ++c) {
        const double d = std::max(0.0, nt.x * kBasis[c].x + nt.y * kBasis[c].y + nt.z * kBasis[c].z);
        w[c] = d * d; sum += w[c];
    }
    Rgb o;
    if (sum <= 0.0) return o;
    for (int c = 0; c < 3; ++c) {
        const Rgb& v = lm.basis[c][std::size_t(j) * lm.n1 + i];
        o = {o.r + v.r * w[c] / sum, o.g + v.g * w[c] / sum, o.b + v.b * w[c] / sum};
    }
    return o;
}

Rgb shadeRnmAt(const Lightmap& lm, double u, double v, D3 nt) {
    const double x = std::clamp(u * lm.n1 - 0.5, 0.0, lm.n1 - 1.0), y = std::clamp(v * lm.n2 - 0.5, 0.0, lm.n2 - 1.0);
    const int x0 = std::min(int(x), std::max(0, lm.n1 - 2)), y0 = std::min(int(y), std::max(0, lm.n2 - 2));
    const int x1 = std::min(x0 + 1, lm.n1 - 1), y1 = std::min(y0 + 1, lm.n2 - 1);
    const double fx = x - x0, fy = y - y0;
    const Rgb a = shadeRnm(lm, x0, y0, nt), b = shadeRnm(lm, x1, y0, nt), c = shadeRnm(lm, x0, y1, nt), d = shadeRnm(lm, x1, y1, nt);
    const auto m = [&](double p, double q, double r, double s) { return (p * (1 - fx) + q * fx) * (1 - fy) + (r * (1 - fx) + s * fx) * fy; };
    return {m(a.r, b.r, c.r, d.r), m(a.g, b.g, c.g, d.g), m(a.b, b.b, c.b, d.b)};
}

AmbientCube bakeAmbientCube(const Tracer& tr, D3 p, int paths, std::uint64_t seed) {
    static constexpr D3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    AmbientCube c;
    for (int k = 0; k < 6; ++k) c.face[k] = tr.irradiance(p, axes[k], paths, seed * 6 + std::uint64_t(k), false);
    return c;
}

Rgb shadeAmbientCube(const AmbientCube& c, D3 n) {
    const Rgb& x = c.face[n.x >= 0 ? 0 : 1];
    const Rgb& y = c.face[n.y >= 0 ? 2 : 3];
    const Rgb& z = c.face[n.z >= 0 ? 4 : 5];
    const double a = n.x * n.x, b = n.y * n.y, d = n.z * n.z;
    return {x.r * a + y.r * b + z.r * d, x.g * a + y.g * b + z.g * d, x.b * a + y.b * b + z.b * d};
}

}  // namespace raw::bake
