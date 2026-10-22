#pragma once
// The noise module on the GPU (ROADMAP P8 stage 2; evidence/p8-noise-gpu-bounds.json): the
// noise_eval pass of src/renderer/gpu/shaders/noise.wgsl over a w x h grid.
//   func: 0 white, 1 value2, 2 value3, 3 perlin2, 4 perlin3, 5 simplex2, 6 simplex3, 7 worley2 F1,
//   8 worley2 F2 - F1, 9 worley3 F1, 10 fBm (Perlin), 11 ridged (simplex), 12 billow (Perlin),
//   13 hybrid (simplex), 14 fBm (simplex) at a domain-warped point (seed + 2), 15 blue noise.
// Position (x, y) x scale, slice z, as raw::noiseGoldens draws them.
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {

struct NoiseGrid { std::uint32_t w{128}, h{128}, func{0}, seed{7}; float scale{1.0f / 16.0f}, z{0.37f}, strength{1.2f}; };
bool noiseOnGpu(rhi::Device& dev, const NoiseGrid& g, std::vector<float>& out, std::string& err);

}  // namespace raw::gpu_check
