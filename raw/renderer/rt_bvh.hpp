#pragma once
// raw-native's ray-tracing BVH (RT stage R2, evidence/rt-r2-bounds.json): PLOC, Meister and
// Bittner, "Parallel Locally-Ordered Clustering for Bounding Volume Hierarchy Construction"
// (IEEE TVCG 2018), from the paper. This CPU form is the reference for the compute build in
// src/renderer/gpu/shaders/rt.wgsl, written in the same float32 operation order so the two
// produce the same tree node for node.
//
//   1. Morton codes (10 bits an axis) of triangle centroids, scaled into the centroid box by a
//      power of two (no division, so every backend rounds alike).
//   2. Sort by (code, triangle index).
//   3. Rounds of: nearest neighbour of each cluster within `radius` positions by the surface
//      area of the merged box (ties to the lower position); merge mutual neighbours into a new
//      node (ids by prefix sum, in position order); compact the survivors (prefix sum).
//
// Node ids: leaves 0..n-1 in sorted order, internal nodes n..2n-2 in creation order; the
// root is the last. Leaves hold one triangle.
#include "raw/math/primitives.hpp"
#include "raw/renderer/bvh.hpp"   // Hit
#include <cstdint>
#include <vector>
namespace raw::rt {

struct Node { float lo[3]{}, hi[3]{}; int left{-1}, right{-1}, tri{-1}; };   // leaf: tri >= 0
struct Tree {
    std::vector<Node> nodes;
    int root{-1};
    std::uint32_t rounds{0};
};

inline constexpr int kPlocRadius = 16;
// Ize, "Robust BVH Ray Traversal" (JCGT 2013): the slab exit widened by 1 + 2 gamma(3),
// gamma(n) = n eps / (1 - n eps), eps = 2^-24, so float rounding of the slab distances never
// culls a box whose triangle Moeller-Trumbore hits (evidence/rt-r2-runs.json, run 1).
inline constexpr float kSlabExit = 1.0f + 2.0f * (3.0f * 5.96046448e-8f) / (1.0f - 3.0f * 5.96046448e-8f);

// Centroid (a + b + c) * (1/3) in float32, as the GPU computes it.
Vec3 centroid(const Tri& t);
// The power-of-two scale that maps the extent into (0.5, 1]; 1 for an empty extent.
float pow2Scale(float extent);
std::uint32_t morton3(std::uint32_t x, std::uint32_t y, std::uint32_t z);
// Surface area of a box as 2 (dy (dx + dz) + dz dx), in this operation order (a form compilers
// cannot refactor; see src/renderer/rt_bvh.cpp).
float area(const float* lo, const float* hi);

Tree buildPloc(const std::vector<Tri>& tris, int radius = kPlocRadius);

// SAH cost with C_trav = 1, C_isect = 1 (evidence/rt-r2-bounds.json, B3).
double sahCost(const Tree& t);
// The same cost for the M3 binned-SAH build (raw/renderer/bvh.hpp: 12 bins, leaves of up to
// 4 triangles). Bvh keeps its nodes private, so this ports its split rule; the port is
// checked against Bvh's node count in tests/test_rt_bvh.cpp.
double binnedSahCost(const std::vector<Tri>& tris, std::size_t* nodeCount = nullptr);

// Traversal: the nearest hit with t < tMax, ties to the lower triangle index; any hit.
bool closest(const Tree& t, const std::vector<Tri>& tris, const Ray& r, float tMax, Hit& hit);
bool occluded(const Tree& t, const std::vector<Tri>& tris, const Ray& r, float tMax);

}  // namespace raw::rt
