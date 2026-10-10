#pragma once
// Owned scenes for the software rasterizer (RT stage R1): built here, in code, from boxes,
// quads and cylinders with procedural RGBA8 textures. Nothing is read from a file and no
// external model is involved.
//
//   retro_room   a BRender-era room: tiled floor running under the camera (past the near
//                plane), brick walls, plank ceiling, pillars, stairs, a table, a low-poly figure
//   iso_street   an isometric street seen down a narrow field of view: stone blocks at
//                varying heights, plaster and brick facades, lamp posts, crates
//   clip_stress  a floor far larger than the guard band crossing the near plane, a wall from
//                behind the camera to the horizon, a block cut by the near plane in view
//   floor_quad   one textured quad at a grazing angle, UV 0..1 (the perspective check)
#include "raw/renderer/swr.hpp"
#include <string>
#include <vector>
namespace raw::swr {

Scene retroRoom();
Scene isoStreet();
Scene clipStress();
Scene floorQuad();
std::vector<Scene> ownedScenes();               // retro_room, iso_street, clip_stress

// Procedural textures, 64 x 64 unless stated, deterministic: indices into a scene's TextureSet.
enum Tex : std::uint32_t { kBrick, kTiles, kPlanks, kChecker, kStone, kPlaster, kCloth, kSkin, kBrass, kTexCount };
TextureSet proceduralTextures();

// Screen-space meshes with shared edges for the fill-rule check (C1): corners three a
// triangle, in pixels; outline is the mesh's outer boundary, in order.
struct ScreenMesh { std::string name; std::vector<Vec2> corners; std::vector<Vec2> outline; };
// 24 x 24 quads of 9 px, vertices on pixel centres; jittered: inner vertices moved up to 2 px on
// each axis (3 px folded one triangle in the first run, evidence/rt-r1-runs.json).
ScreenMesh tieGrid(bool jitter);
ScreenMesh tieFan();                            // 64 triangles around a vertex on a pixel centre
// The same triangles as a Geometry (texture kChecker, UV from position) so a screen mesh can
// be resolved like a scene.
Geometry screenGeometry(const ScreenMesh& m, float w, float h);

}  // namespace raw::swr
