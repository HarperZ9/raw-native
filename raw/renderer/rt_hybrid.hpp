#pragma once
// Hybrid rendering: the R1 rasterizer's visibility buffer, turned into a G-buffer on the GPU
// (swr_gbuffer), then ray-traced sun shadows and mirror reflections through the PLOC BVH
// (pt_hybrid). Checks H1 and H2 of evidence/rt-r2-bounds.json (method in addendum 2).
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/swr.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {

struct HybridFrame {
    std::string error;
    std::vector<float> lit;       // 1 lit, 0 in shadow, -1 uncovered
    std::vector<int> reflTri;     // reflected triangle, -1 none
    std::vector<float> reflT;
    std::vector<float> gbuffer;   // 12 floats a pixel (swr_gbuffer)
};
inline constexpr std::uint32_t kHybridNoOffset = 32, kHybridTiltNormal = 64;
// The R1 GPU rasterizer (its own setup) at w x h, then the G-buffer and the hybrid rays.
HybridFrame hybridGpu(rhi::Device& dev, const rt::PtScene& ps, const swr::Scene& base, int w, int h, std::uint32_t flags = 0);

struct RtHybridParity {
    std::string error, backend, adapter, results;
    bool allPass{false};
    bool pass() const { return error.empty() && allPass; }
    std::string json() const;
};
RtHybridParity rtHybridParity(rhi::Device& dev);

}  // namespace raw::gpu_check
