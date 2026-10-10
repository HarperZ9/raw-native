#pragma once
// Private to the shadow parity (src/renderer/gpu/shadow_parity*.cpp): the GPU runs and the
// uniform block shadow.wgsl reads.
#include "raw/renderer/shadow_parity.hpp"
#include "raw/math/mat.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check::shadow_gpu {

inline constexpr uint32_t kContactSteps = 16;
inline constexpr double kContactLength = 0.5, kContactThickness = 0.1;

// shadow.wgsl's ShadowParams, byte for byte.
struct ShadowParamsGpu {
    uint32_t count, size, width, height, steps, pad0, pad1, pad2;
    float basis[3][4];       // right, up, light
    float box[4][4], span[4][4], cfg[4], vp[16], march[4];
};
static_assert(sizeof(ShadowParamsGpu) == 304, "ShadowParams layout");

struct GpuMap { shadows::ShadowMap map; };
// The four cascades of `tris` (frame_pack layout, 24 floats a triangle) drawn by shadow_depth.
bool maps(rhi::Device& dev, const std::vector<float>& tris, const shadows::CascadeSet& cs, std::array<GpuMap, shadows::kCascades>& out, std::string& err);
// shadow_lookup over `points` (8 floats each: position, view depth, normal, valid): hard, PCF,
// PCSS per point.
bool lookups(rhi::Device& dev, const shadows::CascadeSet& cs, const std::array<GpuMap, shadows::kCascades>& m, const std::vector<float>& points,
             std::vector<float>& out, std::string& err);
// shadow_contact over a w x h view-depth buffer (negative where empty) and its points.
bool contact(rhi::Device& dev, const shadows::CascadeSet& cs, const std::vector<float>& dist, const std::vector<float>& points, int w, int h,
             const Mat4& vp, const shadows::D3& toLight, std::vector<float>& out, std::string& err);

}  // namespace raw::gpu_check::shadow_gpu
