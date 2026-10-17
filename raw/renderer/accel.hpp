#pragma once
// The occlusion query of the CPU ray-traced AO reference (src/renderer/ray_ao.cpp).
// build() keeps the triangle list in the arena, as before, so the arena certificate's record
// is unchanged, and builds a BVH over the same triangles on the heap. occluded() asks the BVH,
// whose triangle test is the same intersectTri with the same t < maxDist rule
// (evidence/m3-ao-bvh-bounds.json). occludedScan() is the scan of every triangle: the test
// oracle. RAW_NATIVE_AO_ORACLE=scan makes occluded() scan, and =skew shortens the BVH query
// distance by 1% (a false-success control); both are for tests only.
#include "raw/math/primitives.hpp"
#include "raw/scene/scene.hpp"
#include "raw/core/arena_allocator.hpp"
#include "raw/renderer/bvh.hpp"
#include <vector>
namespace raw {
struct LinearAccel {
    std::vector<Tri, ArenaAllocator<Tri>> tris;
    void build(const Scene& s, Arena* arena = nullptr);
    bool occluded(const Ray& r, float maxDist) const;
    bool occludedScan(const Ray& r, float maxDist) const;
    enum class Mode : unsigned char { Bvh, Scan, Skew };
    Mode mode{Mode::Bvh};
    Bvh bvh;
};
}
