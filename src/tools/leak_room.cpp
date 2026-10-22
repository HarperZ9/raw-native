// leak-room: the thin-wall leak scene of evidence/p6-radiance-cascades-bounds.json, generated
// here. A closed room 12 x 4 x 6 m (x -6..6, y 0..4, z -3..3) of 0.2 m walls, split at x = 0 by a
// 2 cm wall with a 1 m doorway at its far end (z 1.8..2.8, up to 2.2 m); a 3 x 3 m skylight over
// the west half (x -3.5..-0.5, z -0.5..2.5) lets a sun of 4 onto the west floor, up the thin wall's
// west face, and a little through the doorway. The camera stands in the east half and looks at the
// thin wall and through the doorway. Every face is cut into tiles of at most 0.5 m, because the CPU
// rasterizer drops a triangle with a vertex behind the near plane.
#include "raw/tools/model_scene.hpp"
#include <algorithm>
#include <cmath>
namespace raw {
namespace {
void tile(Mesh& m, Vec3 o, Vec3 du, Vec3 dv, Vec3 n){
    const int nu = std::max(1, int(std::ceil(length(du) / 0.5f))), nv = std::max(1, int(std::ceil(length(dv) / 0.5f)));
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i){
            const Vec3 a = o + du * (float(i) / float(nu)) + dv * (float(j) / float(nv));
            const Vec3 su = du * (1.0f / float(nu)), sv = dv * (1.0f / float(nv));
            const int b = int(m.positions.size());
            for (Vec3 v : {a, a + su, a + su + sv, a + sv}){ m.positions.push_back(v); m.normals.push_back(n); }
            for (int k : {0, 1, 2, 0, 2, 3}) m.indices.push_back(b + k);
        }
}
// An axis-aligned box with outward faces, counter-clockwise seen from outside.
void box(Mesh& m, Vec3 lo, Vec3 hi){
    const Vec3 d = hi - lo, X{d.x, 0, 0}, Y{0, d.y, 0}, Z{0, 0, d.z};
    tile(m, {lo.x, lo.y, hi.z}, X, Y, {0, 0, 1});            // +z
    tile(m, {hi.x, lo.y, lo.z}, X * -1.0f, Y, {0, 0, -1});   // -z
    tile(m, {hi.x, lo.y, hi.z}, Z * -1.0f, Y, {1, 0, 0});    // +x
    tile(m, {lo.x, lo.y, lo.z}, Z, Y, {-1, 0, 0});           // -x
    tile(m, {lo.x, hi.y, hi.z}, X, Z * -1.0f, {0, 1, 0});    // +y
    tile(m, {lo.x, lo.y, lo.z}, X, Z, {0, -1, 0});           // -y
}
Mesh part(Vec3 albedo){ Mesh m; m.material.albedo = albedo; return m; }
}  // namespace

Scene leakRoomScene(){
    Scene s = buildTestScene(64, 64);
    s.meshes.clear();
    Mesh floor = part({0.55f, 0.55f, 0.55f}), walls = part({0.75f, 0.75f, 0.75f}), roof = part({0.7f, 0.7f, 0.7f});
    Mesh thin = part({0.75f, 0.75f, 0.75f});
    box(floor, {-6.2f, -0.2f, -3.2f}, {6.2f, 0.0f, 3.2f});
    box(roof, {-6.2f, 4.0f, -3.2f}, {-3.5f, 4.2f, 3.2f});    // the roof around the skylight
    box(roof, {-0.5f, 4.0f, -3.2f}, {6.2f, 4.2f, 3.2f});
    box(roof, {-3.5f, 4.0f, -3.2f}, {-0.5f, 4.2f, -0.5f});
    box(roof, {-3.5f, 4.0f, 2.5f}, {-0.5f, 4.2f, 3.2f});
    box(walls, {-6.2f, 0.0f, -3.2f}, {-6.0f, 4.0f, 3.2f});
    box(walls, {6.0f, 0.0f, -3.2f}, {6.2f, 4.0f, 3.2f});
    box(walls, {-6.0f, 0.0f, -3.2f}, {6.0f, 4.0f, -3.0f});
    box(walls, {-6.0f, 0.0f, 3.0f}, {6.0f, 4.0f, 3.2f});
    box(thin, {-0.01f, 0.0f, -3.0f}, {0.01f, 4.0f, 1.8f});   // the thin wall, its doorway and lintel
    box(thin, {-0.01f, 0.0f, 2.8f}, {0.01f, 4.0f, 3.0f});
    box(thin, {-0.01f, 2.2f, 1.8f}, {0.01f, 4.0f, 2.8f});
    for (Mesh* m : {&floor, &roof, &walls, &thin}) s.meshes.push_back(std::move(*m));
    s.lights = {Light{normalize(Vec3{0.3f, -1.0f, 0.15f}), 4.0f}};
    return s;
}

}  // namespace raw
