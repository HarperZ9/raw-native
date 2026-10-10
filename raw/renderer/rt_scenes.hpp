#pragma once
// Owned scenes for RT stage R2, built in code (evidence/rt-r2-bounds.json).
//   dense_block   a city block of 36 x 36 lots: buildings with ledges, roof tanks and crates,
//                 about 100,000 triangles, procedural textures from the R1 set
//   soup          seeded random triangles in a cube, for the BVH checks
//   furnace_box   a closed box seen from inside, for the path tracer's furnace test
#include "raw/math/primitives.hpp"
#include "raw/renderer/swr.hpp"
#include <cstdint>
#include <vector>
namespace raw::rt {

swr::Scene denseBlock();
swr::Scene furnaceBox();
std::vector<Tri> soup(std::size_t count, std::uint64_t seed);
std::vector<Tri> trianglesOf(const swr::Geometry& g);

}  // namespace raw::rt
