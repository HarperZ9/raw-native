#pragma once
// The compute BVH build (PLOC) and traversal of src/renderer/gpu/shaders/rt.wgsl, driven from
// the host, and their checks against the CPU reference (evidence/rt-r2-bounds.json, B1 to B3).
#include "raw/renderer/rt_bvh.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {

struct RtBuild {
    std::string error;
    rt::Tree tree;            // read back from the device
    double ms{0};             // host wall time of the whole build, transfers included
};
RtBuild buildPlocGpu(rhi::Device& dev, const std::vector<Tri>& tris, int radius = rt::kPlocRadius);

struct RtTrace {
    std::string error;
    std::vector<float> t;     // -1 for a miss
    std::vector<int> tri;     // -1 a miss, -2 a stack overflow
    long overflows{0};
};
// Trace on the device through `tree`; rays carry their own tMax. any: stop at the first hit.
// farScale: the slab exit factor (1 + 2 gamma(3) normally; the control shortens it).
RtTrace traceGpu(rhi::Device& dev, const rt::Tree& tree, const std::vector<Tri>& tris, const std::vector<Ray>& rays,
                 const std::vector<float>& tMax, bool any, float farScale);
float robustFarScale();

struct RtBvhParity {
    std::string error, backend, adapter;
    std::string results;      // JSON array entries, one a scene
    long controlRadiusDiffNodes{0}, controlShortFarMissed{0};
    bool allPass{false}, quick{false};
    bool pass() const { return error.empty() && allPass && controlRadiusDiffNodes > 0 && controlShortFarMissed > 0; }
    std::string json() const;
};
// quick: 20,000 rays a scene instead of 100,000 (CI on SwiftShader; the evidence uses the full run).
RtBvhParity rtBvhParity(rhi::Device& dev, bool quick = false);

}  // namespace raw::gpu_check
