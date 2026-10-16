#pragma once
// Private to the style checks: the image passes of round 2 (purity, lightpaint, glaze, hatch) on an
// RHI device, with the uniform layouts style.wgsl declares. Each returns one packed RGBA8 word a
// pixel (purity and hatch: sRGB; lightpaint and glaze: the lab shoulder then sRGB).
#include "raw/renderer/style_hatch.hpp"
#include "raw/renderer/style_purity.hpp"
#include "raw/renderer/style_relight.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check::style_gpu {

struct PurityParamsGpu { std::uint32_t width, height, pad0, pad1; float a[4], b[4], c[4], d[4]; };
static_assert(sizeof(PurityParamsGpu) == 80, "PurityParams layout");
struct LightpaintParamsGpu { std::uint32_t width, height, complement, pad0; float a[4], shadow[4], light[4], fog[4]; };
static_assert(sizeof(LightpaintParamsGpu) == 80, "LightpaintParams layout");
struct GlazeParamsGpu { std::uint32_t width, height, dispersion, pad0; float a[4], b[4], c[4], fwd[4], right[4], up[4], eye[4], fog[4]; };
static_assert(sizeof(GlazeParamsGpu) == 144, "GlazeParams layout");
struct HatchParamsGpu { std::uint32_t width, height, layers, pad0; float a[4], ink[4], paper[4]; };
static_assert(sizeof(HatchParamsGpu) == 64, "HatchParams layout");

bool runPurity(rhi::Device& dev, const style::PurityPlan& p, int w, int h, const std::vector<float>& src, double t,
               std::vector<std::uint32_t>& packed, std::string& err);
bool runLightpaint(rhi::Device& dev, const style::LightpaintPlan& p, const style::LitFrame& f, double gain,
                   std::vector<std::uint32_t>& packed, std::string& err);
bool runGlaze(rhi::Device& dev, const style::GlazePlan& p, const style::LitFrame& f, double gain,
              std::vector<std::uint32_t>& packed, std::string& err);
bool runHatch(rhi::Device& dev, const style::HatchPlan& p, int w, int h, std::vector<std::uint32_t>& packed, std::string& err);

}  // namespace raw::gpu_check::style_gpu
