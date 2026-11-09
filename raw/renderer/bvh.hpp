#pragma once
// A bounding volume hierarchy over triangles for the CPU references (ROADMAP M3: shadow
// visibility, the lightmap baker and the path-traced references). Binned surface-area
// heuristic, deterministic for a given triangle order; the triangle test is the same
// watertight intersectTri the linear accelerator uses, so a query returns exactly
// what a scan of every triangle would (tests/test_bvh.cpp checks that on random rays).
#include "raw/math/primitives.hpp"
#include <cstdint>
#include <vector>
namespace raw {

struct Hit { float t{0}, u{0}, v{0}; int tri{-1}; };

class Bvh {
public:
    void build(std::vector<Tri> tris);
    // The nearest hit with t < tMax (ties go to the lower triangle index), or false.
    bool closest(const Ray& r, float tMax, Hit& hit) const;
    // Any hit with t < tMax.
    bool occluded(const Ray& r, float tMax) const;
    const std::vector<Tri>& triangles() const { return tris_; }
    // The tree for the GPU rtao pass (src/renderer/gpu/shaders/passes.wgsl): 8 floats a node,
    // box min, then the leaf's first triangle or the interior node's left child (its right child
    // is the one after it), box max, then the leaf's triangle count (0 for an interior node). Boxes
    // are padded outward by `pad` times their extent plus 1e-6, so a slab test in float never
    // rejects a ray the triangle test would accept. Indices are exact as floats below 2^24.
    // positions: the triangles in the tree's order, 9 floats each (a, b, c).
    void flatten(std::vector<float>& nodes, std::vector<float>& positions, float pad = 1e-4f) const;
    // The deepest stack occluded()'s traversal (and the GPU pass's) can reach on this tree.
    int maxStack() const;
    std::size_t nodeCount() const { return nodes_.size(); }
private:
    struct Node { AABB box; int first{0}, count{0}, right{0}; };   // count > 0: leaf of tris [first, first + count)
    void split(int node, std::vector<int>& order, std::vector<Vec3>& centres);
    std::vector<Tri> tris_;
    std::vector<Node> nodes_;
    std::vector<int> index_;                                    // input index of each stored triangle
};

}  // namespace raw
