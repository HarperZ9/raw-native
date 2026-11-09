#pragma once
// The path tracer on hardware ray queries (HW H2, docs/hw-rt.md): the compute tracer's passes with
// its BVH traversal swapped for DXR inline ray queries (hw_pt.hlsl, generated from pt_trace.hlsl by
// scripts/hw_pt_hlsl.py). Same inputs, same outputs and the same random numbers as pathTraceGpu, so
// any caller can switch traversal without other change. Interface v2 scenes only, as pathTraceGpu.
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/rhi/hw_rt.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
namespace raw::gpu_check {

struct HwPtOptions {
    std::uint32_t slice{16};     // samples a submission at 1280 x 720, scaled down above it (as pathTraceGpu)
    float offset[3]{0, 0, 0};    // the planted-offset control: moves the hardware instance only
};
struct HwPtResult {
    rt::PathTraceOutput out;     // out.error set when the hardware path did not run
    rhi::hw::PassTiming timing;
};
HwPtResult pathTraceHw(rhi::Device& dev, const rt::PtScene& s, const rt::PathTraceDesc& d, const HwPtOptions& o = {});

}  // namespace raw::gpu_check
