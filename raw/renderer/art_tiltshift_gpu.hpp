#pragma once
// P23 on the GPU (evidence/tiltshift-bounds.json g1 and g3): the art_ts_* chain from the scene's
// distances and view rays and a paint frame to 8-bit codes, with the CoC read back for parity and,
// with repeats > 0, the whole chain timed with GPU timestamps.
#include "raw/renderer/art_tiltshift.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {
struct TiltGpu {
    std::string error;
    std::vector<float> coc;                 // the CoC a pixel
    std::vector<std::uint32_t> packed;      // RGBA8 a pixel
    double chainMs{0};                      // median GPU milliseconds of the ten passes
};
bool tiltOnGpu(rhi::Device& dev, const art::Frame& f, const art::LensGeom& g, const art::FxPlan& P, const std::vector<float>& paint, TiltGpu& out, int repeats);
}  // namespace raw::gpu_check
