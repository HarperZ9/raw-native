// flooded-arcade: the block-built showcase of evidence/p6-radiance-cascades-bounds.json addendum A
// (the author's reference of 2026-10-22: block architecture made real by light alone). Generated
// here from axis-aligned boxes on a 0.5 m grid. An arcade rings an open courtyard (x -12..12,
// z -7..7) inside 1 m outer walls 5.5 m high: columns of 1 x 1 m every 3 m on the lines z = +-4 and
// x = +-9, stepped (corbelled) arches under a beam at 4..5 m, and a roof over the aisles. Water stands
// 0.3 m deep over the whole floor as a dark plane; clumps of green blocks grow on beams, column bases
// and wall tops. A sun 30 degrees up and the sky light it. (A first, two-storey version with 9.7 m
// walls and a 20-degree sun left the court in shadow; it was changed before any measured run.)
#include "raw/tools/model_scene.hpp"
#include "block_mesh.hpp"
#include <cmath>
#include <cstdint>
namespace raw {
using namespace blocks;
namespace {
struct Parts { Mesh stone, beam, water, green; };
// A deterministic 0..1 value per integer cell, for the overgrowth.
float cellHash(int x, int y, int z){
    std::uint32_t h = std::uint32_t(x * 73856093) ^ std::uint32_t(y * 19349663) ^ std::uint32_t(z * 83492791);
    h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
    return float(h & 0xFFFFu) / 65536.0f;
}
// A column of one storey at (x, z) from y0 to y1, with a corbel step on each side along the row.
void column(Parts& p, float x, float z, float y0, float y1, bool alongX){
    box(p.stone, {x - 0.5f, y0, z - 0.5f}, {x + 0.5f, y1, z + 0.5f});
    const Vec3 d = alongX ? Vec3{1, 0, 0} : Vec3{0, 0, 1}, w = alongX ? Vec3{0, 0, 0.5f} : Vec3{0.5f, 0, 0};
    for (float s : {-1.0f, 1.0f})
        for (int k = 0; k < 2; ++k){                         // two steps: 0.5 m out at y1-1, 1 m out at y1-0.5
            const float out = 0.5f + 0.5f * float(k + 1), top = y1 - 0.5f * float(1 - k);
            const Vec3 a = Vec3{x, 0, z} + d * (s * 0.5f), b = Vec3{x, 0, z} + d * (s * out);
            const Vec3 lo{std::fmin(a.x, b.x) - w.x, top - 0.5f, std::fmin(a.z, b.z) - w.z}, hi{std::fmax(a.x, b.x) + w.x, top, std::fmax(a.z, b.z) + w.z};
            box(p.stone, lo, hi);
        }
}
// One row of columns along x (z fixed) or along z (x fixed), both storeys, with beams over each.
void row(Parts& p, bool alongX, float fixed, float from, float to){
    for (float t = from; t <= to + 1e-3f; t += 3.0f){
        const float x = alongX ? t : fixed, z = alongX ? fixed : t;
        column(p, x, z, 0.0f, 4.0f, alongX);
    }
    const Vec3 lo = alongX ? Vec3{from - 0.5f, 0, fixed - 0.5f} : Vec3{fixed - 0.5f, 0, from - 0.5f};
    const Vec3 hi = alongX ? Vec3{to + 0.5f, 0, fixed + 0.5f} : Vec3{fixed + 0.5f, 0, to + 0.5f};
    box(p.beam, {lo.x, 4.0f, lo.z}, {hi.x, 5.0f, hi.z});
}
void shell(Parts& p){
    box(p.stone, {-13.0f, -0.5f, -8.0f}, {13.0f, 0.0f, 8.0f});                 // the ground under the water
    box(p.water, {-12.0f, 0.25f, -7.0f}, {12.0f, 0.3f, 7.0f});                  // the water's surface
    box(p.stone, {-13.0f, 0.0f, -8.0f}, {13.0f, 5.5f, -7.0f});                  // outer walls
    box(p.stone, {-13.0f, 0.0f, 7.0f}, {13.0f, 5.5f, 8.0f});
    box(p.stone, {-13.0f, 0.0f, -7.0f}, {-12.0f, 5.5f, 7.0f});
    box(p.stone, {12.0f, 0.0f, -7.0f}, {13.0f, 5.5f, 7.0f});
    for (float s : {-1.0f, 1.0f}){                                             // the roofs over the aisles
        box(p.stone, {-12.0f, 5.0f, s > 0 ? 4.5f : -7.0f}, {12.0f, 5.5f, s > 0 ? 7.0f : -4.5f});
        box(p.stone, {s > 0 ? 9.5f : -12.0f, 5.0f, -4.5f}, {s > 0 ? 12.0f : -9.5f, 5.5f, 4.5f});
    }
}
// Overgrowth: on every 0.5 m cell of the beam tops, the column bases at the water and the wall tops,
// a block where the cell hash says so, and sometimes a second hanging below or stacked above.
void overgrowth(Parts& p){
    const auto clump = [&](float x, float y, float z, float chance, float hang){
        const int ix = int(std::floor(x * 2.0f)), iy = int(std::floor(y * 2.0f)), iz = int(std::floor(z * 2.0f));
        const float h = cellHash(ix, iy, iz);
        if (h >= chance) return;
        box(p.green, {x, y, z}, {x + 0.5f, y + 0.5f, z + 0.5f});
        if (h < chance * 0.4f) box(p.green, {x, y + hang * 0.5f, z}, {x + 0.5f, y + hang * 0.5f + 0.5f, z + 0.5f});
    };
    for (float x = -12.0f; x < 12.0f; x += 0.5f)
        for (float z : {-4.5f, 4.0f}){ clump(x, 5.0f, z, 0.18f, 1.0f); clump(x, 3.5f, z, 0.12f, -1.0f); }
    for (float z = -4.5f; z < 4.5f; z += 0.5f)
        for (float x : {-9.5f, 9.0f}) clump(x, 5.0f, z, 0.18f, 1.0f);
    for (float t = -10.5f; t <= 10.6f; t += 3.0f)
        for (float side : {-1.0f, 0.5f}){ clump(t + side, 0.3f, -4.0f, 0.6f, 1.0f); clump(t + side, 0.3f, 4.0f, 0.6f, 1.0f); }
    for (float x = -13.0f; x < 13.0f; x += 0.5f){ clump(x, 5.5f, -8.0f, 0.3f, 1.0f); clump(x, 5.5f, 7.5f, 0.3f, 1.0f); }
}
}  // namespace

Scene floodedArcadeScene(){
    Scene s = buildTestScene(64, 64);
    s.meshes.clear();
    Parts p{part({0.62f, 0.58f, 0.50f}), part({0.52f, 0.47f, 0.42f}), part({0.04f, 0.06f, 0.07f}), part({0.16f, 0.30f, 0.10f})};
    shell(p);
    row(p, true, -4.0f, -10.5f, 10.5f);
    row(p, true, 4.0f, -10.5f, 10.5f);
    row(p, false, -9.0f, -1.0f, 2.0f);
    row(p, false, 9.0f, -1.0f, 2.0f);
    overgrowth(p);
    for (Mesh* m : {&p.stone, &p.beam, &p.water, &p.green}) s.meshes.push_back(std::move(*m));
    const float el = 0.52359878f, az = 0.6f;                                     // 30 degrees up
    s.lights = {Light{normalize(Vec3{std::cos(el) * std::cos(az), -std::sin(el), std::cos(el) * std::sin(az)}), 3.0f}};
    return s;
}

}  // namespace raw
