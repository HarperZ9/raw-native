#pragma once
// Private to the generated scenes (src/tools/leak_room.cpp, src/tools/flooded_arcade.cpp): axis-aligned
// boxes whose faces are cut into tiles of at most 0.5 m, because the CPU rasterizer drops a triangle with
// a vertex behind the near plane.
#include "raw/scene/scene.hpp"
#include <algorithm>
#include <cmath>
namespace raw::blocks {
inline void tile(Mesh& m, Vec3 o, Vec3 du, Vec3 dv, Vec3 n){
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
inline void box(Mesh& m, Vec3 lo, Vec3 hi){
    const Vec3 d = hi - lo, X{d.x, 0, 0}, Y{0, d.y, 0}, Z{0, 0, d.z};
    tile(m, {lo.x, lo.y, hi.z}, X, Y, {0, 0, 1});            // +z
    tile(m, {hi.x, lo.y, lo.z}, X * -1.0f, Y, {0, 0, -1});   // -z
    tile(m, {hi.x, lo.y, hi.z}, Z * -1.0f, Y, {1, 0, 0});    // +x
    tile(m, {lo.x, lo.y, lo.z}, Z, Y, {-1, 0, 0});           // -x
    tile(m, {lo.x, hi.y, hi.z}, X, Z * -1.0f, {0, 1, 0});    // +y
    tile(m, {lo.x, lo.y, lo.z}, X, Z, {0, -1, 0});           // -y
}
inline Mesh part(Vec3 albedo){ Mesh m; m.material.albedo = albedo; return m; }
}  // namespace raw::blocks
