#pragma once
// HW H2 ray reconstruction: the owned scenes the dataset is rendered from, and camera paths
// (evidence/rr-split.json, evidence/rr-bounds-amendment-1.json). Private to src/tools/rr_*.cpp.
#include "raw/renderer/rt_pathtrace.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rr {

// Scene names: retro_room, dense_block, iso_street, clip_stress (RT); raw_hall, raw_hero (M3,
// lit here); builtin (the core test scene); hall_b (HW H1.1b); terrain_<seed>, debris_<seed>
// (HW, seeded here). Returns false for an unknown name. cam is the scene's base camera.
bool scene(const std::string& name, rt::PtScene& out, rt::PtCamera& cam, std::string& lighting);
std::vector<std::string> knownScenes();

// Frame k of an n-frame path around the base camera; k may be -1 (the frame before the first,
// for motion). Smooth, seeded by pathSeed; a few pixels of motion a frame at 480 x 270.
rt::PtCamera pathCamera(const rt::PtCamera& base, std::uint64_t pathSeed, int k, int n);

// Per-pixel material roughness and metallic from the pixel-centre triangle (-1: 0 and 0).
void materialAovs(const rt::PtScene& s, const std::vector<int>& triangle, std::vector<float>& rough, std::vector<float>& metal);

std::uint64_t mix(std::uint64_t x);   // splitmix64 finaliser

}  // namespace raw::rr
