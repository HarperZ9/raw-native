#pragma once
// HW H1.1: classify GPU hits against the CPU reference (private to src/tools/hw_rayquery_*.cpp).
#include "raw/renderer/bvh.hpp"
#include "raw/rhi/hw.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw {
struct RayCompare {
    uint64_t rays{0}, bothMiss{0}, sameTri{0}, violT{0}, violUV{0};
    uint64_t edge{0}, tie{0}, grazing{0}, boundary{0}, unexplained{0};
    double worstT{0}, worstUV{0};   // worst error over its tolerance among agreeing hits
    std::vector<std::string> samples;   // the first few unexplained cases, for diagnosis
    uint64_t explained() const { return edge + tie + grazing + boundary; }
    void add(const RayCompare& o);
};
RayCompare compareHits(const std::vector<Tri>& tris, const std::vector<rhi::hw::RayIn>& rays,
                       const std::vector<Hit>& cpu, const std::vector<bool>& cpuHit, const std::vector<rhi::hw::HitOut>& gpu);
}
