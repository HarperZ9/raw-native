#pragma once
// The path tracer on the GPU (src/renderer/gpu/shaders/pt.wgsl), interface v1 of
// raw/renderer/rt_pathtrace.hpp, and its checks (evidence/rt-r2-bounds.json, P1 to P4).
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
namespace raw::gpu_check {

// Render on the GPU in slices of `slice` samples a dispatch (all in one submission); the
// result is the same for any slice size, up to float summation order.
rt::PathTraceOutput pathTraceGpu(rhi::Device& dev, const rt::PtScene& s, const rt::PathTraceDesc& d, std::uint32_t slice = 16);

struct RtPtParity {
    std::string error, backend, adapter, results;
    bool allPass{false};
    bool pass() const { return error.empty() && allPass; }
    std::string json() const;
};
// quick: P2 at 64 x 48 and 128 spp instead of 128 x 96 and 256 (CI on SwiftShader).
RtPtParity rtPtParity(rhi::Device& dev, bool quick = false);

}  // namespace raw::gpu_check
