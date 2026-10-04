#pragma once
#include "raw/math/primitives.hpp"
#include "raw/scene/scene.hpp"
#include "raw/core/arena_allocator.hpp"
#include <vector>
namespace raw {
struct LinearAccel {
    std::vector<Tri, ArenaAllocator<Tri>> tris;
    void build(const Scene& s, Arena* arena = nullptr);
    bool occluded(const Ray& r, float maxDist) const;
};
}
