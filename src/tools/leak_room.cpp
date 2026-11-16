// leak-room: the thin-wall leak scene of evidence/p6-radiance-cascades-bounds.json, generated
// here. A closed room 12 x 4 x 6 m (x -6..6, y 0..4, z -3..3) of 0.2 m walls, split at x = 0 by a
// 2 cm wall with a 1 m doorway at its far end (z 1.8..2.8, up to 2.2 m); a 3 x 3 m skylight over
// the west half (x -3.5..-0.5, z -0.5..2.5) lets a sun of 4 onto the west floor, up the thin wall's
// west face, and a little through the doorway. The camera stands in the east half and looks at the
// thin wall and through the doorway.
#include "raw/tools/model_scene.hpp"
#include "block_mesh.hpp"
#include <algorithm>
#include <cmath>
namespace raw {
using namespace blocks;

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
