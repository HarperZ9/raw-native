#pragma once
// Private to the style checks: the two style passes of style.wgsl run on an RHI device, with the
// buffer layouts the WGSL declares. A game runtime's pass drivers will grow from these.
#include "raw/renderer/style_ditherlight.hpp"
#include "raw/renderer/style_paintover.hpp"
#include "raw/rhi/rhi.hpp"
#include "gpu_util.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check::style_gpu {

// DitherParams in style.wgsl.
struct DitherParamsGpu {
    std::uint32_t width, height, levels, source, anchor, lights, pad0, pad1;
    float a[4], b[4], ink[4], paper[4];
};
static_assert(sizeof(DitherParamsGpu) == 96, "DitherParams layout");
// PaintParams in style.wgsl.
struct PaintParamsGpu {
    std::uint32_t width, height, count, pad0;
    float opt[4], opt2[4];
};
static_assert(sizeof(PaintParamsGpu) == 48, "PaintParams layout");

// One dispatch of the style pass at index `pass` (style_layout.hpp) over n invocations, 64 a group,
// after the uploads; then reads the storage buffers `outs` (n words each) into `dst`.
bool dispatchAndRead(util::Gpu& gpu, int pass, const std::vector<rhi::BufferHandle>& binds, const std::vector<rhi::Access>& inAccess,
                     const std::vector<rhi::BufferHandle>& outs, std::uint32_t n, std::vector<std::vector<std::uint32_t>*> dst);
int passIndex(const char* name);   // -1 when style.wgsl has no such pass

inline constexpr int kMaxDitherLights = 8;
inline constexpr int kMaxProjectors = 4;

// Levels and packed RGBA8 words, one a pixel.
bool runDither(rhi::Device& dev, const style::DitherPlan& p, const std::vector<style::DitherLight>& lights, const style::DitherGBuffer& g,
               std::vector<std::uint32_t>& level, std::vector<std::uint32_t>& packed, std::string& err);

// The view G-buffer of a paint-over pass: position, normal (3 floats a pixel) and a hit flag.
struct PaintView { int w{0}, h{0}; std::vector<float> P, N; std::vector<std::uint8_t> hit; };
// Packed RGBA8 words, alpha 255 where some projector covers, 0 elsewhere.
bool runPaint(rhi::Device& dev, const std::vector<const style::Projector*>& set, const std::vector<double>& reach, const style::PaintCamera& view,
              const style::PaintOptions& o, const PaintView& g, std::vector<std::uint32_t>& packed, std::string& err);

}  // namespace raw::gpu_check::style_gpu
