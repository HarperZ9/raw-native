#pragma once
// Split radiance cascades on the GPU (src/renderer/gpu/gi_gpu.cpp, src/renderer/gpu/shaders/gi.wgsl):
// the same algorithm as gi::cascadesCpu (raw/renderer/gi_cascades.hpp), from the CPU's G-buffer.
#include "raw/renderer/gi_cascades.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gi {

struct GpuRun {
    std::string error, backend, adapter;
    std::vector<Vec3> indirect;                    // I a pixel
    std::uint32_t slots[kCascades]{};
    bool slotsMatch{false};                        // the GPU's mark-and-scan gave the CPU's slot tables
    double frameMs{0.0};                           // one steady-state frame of every GI pass (median submit to fence)
    int repeats{0};
};
// Runs p.frames frames; with timeRepeats > 0 then records one more steady-state frame alone and
// submits it timeRepeats times. control: the test-only merge that ignores occlusion
// (RAW_NATIVE_GI_CONTROL=nomerge in the gi command).
GpuRun cascadesGpu(rhi::Device& dev, const Scene3& s, const GBuffer& g, const Params& p, bool control = false,
                   int timeRepeats = 0);

}  // namespace raw::gi
