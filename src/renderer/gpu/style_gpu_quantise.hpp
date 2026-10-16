#pragma once
// Private to the style checks: hysteresis quantisation and palette-locked lighting on an RHI device,
// with the uniform layouts style.wgsl declares.
#include "raw/renderer/style_quantise.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace raw::gpu_check::style_gpu {

struct HystParamsGpu { std::uint32_t width, height, mode, palSize, hasState, hasMotion, on, pad0; float a[4], b[4]; };
static_assert(sizeof(HystParamsGpu) == 64, "HystParams layout");
struct PalettelightParamsGpu { std::uint32_t width, height, steps, voidSteps; float a[4], b[4], lampA[4], lampB[4], g0a[4], g0b[4], g1a[4], g1b[4]; };
static_assert(sizeof(PalettelightParamsGpu) == 144, "PalettelightParams layout");

// A hysteresis sequence on the GPU: two state buffers kept across frames, the previous frame's
// read and this frame's written, as web/shaders/hysteresis/gpu.mjs does.
class GpuHysteresis {
public:
    GpuHysteresis(rhi::Device& dev, const style::HystPlan& p, int w, int h);
    ~GpuHysteresis();
    GpuHysteresis(const GpuHysteresis&) = delete;
    GpuHysteresis& operator=(const GpuHysteresis&) = delete;
    // mv and dA empty for a static camera. idx: the decision a pixel; packed: RGBA8 words.
    bool frame(const std::vector<float>& rgba, const std::vector<float>& mv, const std::vector<float>& dA, const std::vector<float>& dist,
               std::vector<std::int32_t>& idx, std::vector<std::uint32_t>& packed, std::string& err);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool runPalettelight(rhi::Device& dev, const style::PalettelightPlan& p, const style::ChapelGBuffer& g, double time,
                     std::vector<std::uint32_t>& idx, std::vector<std::uint32_t>& packed, std::string& err);

}  // namespace raw::gpu_check::style_gpu
