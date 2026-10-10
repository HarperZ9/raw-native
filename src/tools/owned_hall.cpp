// raw-hall: an interior hall at Sponza scale. See raw/tools/owned_assets.hpp.
// Units are metres. The nave runs along x from -18 to 18, the arcades stand at z = +-5, the outer
// walls at z = +-8; the ground storey's arches spring at 5.4 m, the gallery floor is at 7.2 m,
// the roof over the aisles at 14 m, and the nave is open to the sky (an atrium).
#include "owned_build.hpp"
#include <cmath>
namespace raw::owned {
namespace {
using namespace build;
constexpr int kBays = 12;                  // 13 columns a row, 3 m apart
constexpr double kX0 = -18.0, kBay = 3.0;

pbr::Material stone(double r, double g, double b, double rough) {
    pbr::Material m; m.baseColor = {r, g, b}; m.metallic = 0.0; m.roughness = rough;
    return m;
}
// A fluted column: base, a shaft with entasis and 20 flutes, a flared capital and an abacus.
void column(Part& p, P3 at, double scale) {
    std::vector<std::pair<double, double>> prof = {
        {0.55, 0.0}, {0.55, 0.22}, {0.48, 0.26}, {0.46, 0.36}, {0.40, 0.42}, {0.36, 0.48}};
    for (int k = 0; k <= 16; ++k) {   // the shaft from 0.5 m to 4.5 m, slightly swelling
        const double t = k / 16.0, r = 0.34 - 0.06 * t + 0.02 * dsin(kPi * t);
        prof.push_back({r, 0.5 + 4.0 * t});
    }
    prof.insert(prof.end(), {{0.31, 4.6}, {0.36, 4.75}, {0.46, 4.95}, {0.56, 5.05}, {0.58, 5.2}});
    for (auto& q : prof) { q.first *= scale; q.second *= scale; }
    const double lo = 0.5 * scale, hi = 4.5 * scale;
    lathe(p, prof, 96, at, true, true, [lo, hi](double a, double y) {
        if (y <= lo || y >= hi) return 1.0;
        // Flutes on the shaft only, fading in and out over 0.2 m so the surface stays continuous.
        const double w = std::fmin(1.0, std::fmin((y - lo) / 0.2, (hi - y) / 0.2));
        const double f = dcos(20.0 * a);
        return 1.0 - 0.025 * w * f * f;
    });
}
// A round arch between x0 and x1 at height y (the springing line), z in [z0, z1]: an annulus
// sector from radius ri to ro, its front, back, soffit and extrados.
void arch(Part& p, double x0, double x1, double y, double z0, double z1, double ri, double ro) {
    const double cx = 0.5 * (x0 + x1);
    const auto ring = [&](double r, double a, double z) { return P3{cx + r * dcos(a), y + r * dsin(a), z}; };
    grid(p, [&](double u, double v) { return ring(ri + (ro - ri) * v, kPi * u, z1); }, 64, 4, false, true);    // front (+z)
    grid(p, [&](double u, double v) { return ring(ri + (ro - ri) * v, kPi * u, z0); }, 64, 4, false, false);   // back (-z)
    grid(p, [&](double u, double v) { return ring(ri, kPi * u, z0 + (z1 - z0) * v); }, 64, 4, false, true);    // soffit, facing the opening
    grid(p, [&](double u, double v) { return ring(ro, kPi * u, z0 + (z1 - z0) * v); }, 64, 4, false, false);   // extrados
}
// A drapery panel hung from (x0, top, z) across width w and drop h, folds growing downward.
void drape(Part& p, double x0, double top, double z, double w, double h, double side) {
    grid(p, [=](double u, double v) {
        const double x = x0 + w * u, y = top - h * v;
        const double fold = 0.09 * (0.35 + 0.65 * v) * dsin(2.0 * kPi * 5.0 * u + 0.6 * v);
        return P3{x, y, z + side * fold};
    }, 64, 96, false, side > 0);
}
}  // namespace

Asset hall() {
    Asset a; a.name = "raw-hall";
    Part floor; floor.name = "floor"; floor.kind = "floor"; floor.material = stone(0.55, 0.52, 0.47, 0.6);
    Part tiles; tiles.name = "floor inlay"; tiles.kind = "floor"; tiles.material = stone(0.22, 0.20, 0.19, 0.4);
    for (int i = 0; i < 36; ++i) for (int k = 0; k < 16; ++k)
        box(((i + k) % 2 ? tiles : floor), {kX0 + i, -0.1, -8.0 + k}, {kX0 + i + 0.98, 0.0, -8.0 + k + 0.98});
    Part cols; cols.name = "columns"; cols.kind = "column"; cols.material = stone(0.72, 0.68, 0.6, 0.55);
    Part arches; arches.name = "arches"; arches.kind = "arch"; arches.material = stone(0.66, 0.6, 0.52, 0.6);
    Part walls; walls.name = "walls"; walls.kind = "wall"; walls.material = stone(0.6, 0.55, 0.48, 0.7);
    int nColumns = 0, nArches = 0;
    for (int storey = 0; storey < 2; ++storey) {
        const double base = storey == 0 ? 0.0 : 7.2, scale = storey == 0 ? 1.0 : 0.75;
        const double spring = base + 5.2 * scale, ri = 1.5 - 0.58 * scale, ro = 1.5;
        for (double z : {-5.0, 5.0}) {
            for (int i = 0; i <= kBays; ++i) { column(cols, {kX0 + kBay * i, base, z}, scale); ++nColumns; }
            for (int i = 0; i < kBays; ++i) { arch(arches, kX0 + kBay * i, kX0 + kBay * (i + 1), spring, z - 0.3, z + 0.3, ri, ro); ++nArches; }
            // The wall above the arches, up to the next floor or the roof.
            box(walls, {kX0 - 0.3, spring + ro, z - 0.3}, {-kX0 + 0.3, storey == 0 ? 7.2 : 14.0, z + 0.3});
        }
    }
    box(walls, {kX0 - 0.6, 0.0, -8.4}, {-kX0 + 0.6, 14.0, -8.0});    // outer walls
    box(walls, {kX0 - 0.6, 0.0, 8.0}, {-kX0 + 0.6, 14.0, 8.4});
    box(walls, {kX0 - 0.6, 0.0, -8.0}, {kX0 - 0.2, 14.0, 8.0});     // end walls
    box(walls, {-kX0 + 0.2, 0.0, -8.0}, {-kX0 + 0.6, 14.0, 8.0});
    for (double s : {-1.0, 1.0}) {                                      // gallery floors and aisle roofs
        box(walls, {kX0, 7.0, s > 0 ? 5.3 : -8.0}, {-kX0, 7.2, s > 0 ? 8.0 : -5.3});
        box(walls, {kX0, 14.0, s > 0 ? 5.3 : -8.0}, {-kX0, 14.3, s > 0 ? 8.0 : -5.3});
    }
    Part cloth; cloth.name = "drapery"; cloth.kind = "drapery";
    cloth.material = stone(0.45, 0.06, 0.07, 0.8);
    cloth.material.sheenColor = {0.9, 0.35, 0.35}; cloth.material.sheenRoughness = 0.45;
    cloth.extensions = kSheen;
    int nDrapes = 0;
    for (double z : {-5.0, 5.0}) for (int i : {2, 4, 7, 9}) {   // hung in the gallery's bays, facing the nave
        drape(cloth, kX0 + kBay * i + 0.3, 7.2 + 5.2 * 0.75, z - (z > 0 ? 0.35 : -0.35), 2.4, 3.4, z > 0 ? -1.0 : 1.0);
        ++nDrapes;
    }
    Part frames; frames.name = "lantern frames"; frames.kind = "lantern frame";
    frames.material.baseColor = {0.2, 0.17, 0.12}; frames.material.metallic = 1.0; frames.material.roughness = 0.4;
    Part glow; glow.name = "lanterns"; glow.kind = "lantern";
    glow.material.baseColor = {0.0, 0.0, 0.0}; glow.material.metallic = 0.0; glow.material.roughness = 0.3;
    glow.material.emissive = {1.0, 0.62, 0.25}; glow.material.emissiveStrength = 120.0;
    glow.extensions = kEmissiveStrength;
    int nLanterns = 0;
    for (int i = 0; i < 5; ++i) for (double z : {-3.2, 3.2}) {   // ten lanterns along the nave
        const double x = -12.0 + 6.0 * i, y = 4.0;
        box(frames, {x - 0.25, y + 0.55, z - 0.25}, {x + 0.25, y + 0.62, z + 0.25});
        box(frames, {x - 0.25, y - 0.42, z - 0.25}, {x + 0.25, y - 0.35, z + 0.25});
        lathe(glow, {{0.05, -0.35}, {0.16, -0.25}, {0.19, 0.0}, {0.16, 0.3}, {0.05, 0.55}}, 32, {x, y, z}, true, true);
        ++nLanterns;
    }
    a.parts = {floor, tiles, cols, arches, walls, cloth, frames, glow};
    a.parts[2].instances = nColumns; a.parts[3].instances = nArches; a.parts[5].instances = nDrapes; a.parts[7].instances = nLanterns;
    return a;
}
}  // namespace raw::owned
