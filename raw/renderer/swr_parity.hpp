#pragma once
// The GPU compute rasterizer (src/renderer/gpu/shaders/swr.wgsl) driven from the host, and
// its checks against the CPU reference (evidence/rt-r1-bounds.json, C4 and C5). Backend-neutral.
#include "raw/renderer/swr.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {

struct SwrFrame {
    std::string error;
    swr::Setup setup;                // the setup the GPU rasterized (its own, or the shared one)
    swr::Visibility vis;             // slot, depth, depthQ (count is not produced on the GPU)
    swr::Resolved res;               // uv, texelCoord, texel, colour
    std::uint32_t listEntries{0};    // total bin-list length
};
// Rasterize on the GPU. With `shared`, the GPU skips swr_setup and uses those records (C4);
// without, it transforms, clips and snaps the geometry itself (C5).
SwrFrame swrGpu(rhi::Device& dev, const swr::Geometry& g, const swr::TextureSet& t, Vec3 light, const Mat4& viewProj,
                int w, int h, const swr::Options& o, const swr::Setup* shared, int tile = 16);

struct SwrCase {
    std::string check, scene, mode;
    long pixels{0}, covered{0}, triMismatch{0}, slotMismatch{0}, depthMismatch{0}, uvOutside{0}, texelMismatch{0},
        texelExempt{0}, colourOutside{0};
    // C5 only
    long validCpu{0}, validGpu{0}, validSetDiff{0}, slotsDiffering{0}, coordOverStep{0}, pixelsUnexplained{0},
        pixelsExplained{0}, depthOverBound{0};
    double worstUv{0}, worstDepth{0};
    bool pass{false};
    std::string json() const;
};
struct SwrParity {
    std::string error, backend, adapter;
    std::vector<SwrCase> cases;
    long controlFillRuleDiff{0}, controlMovedVertexDiff{0}, controlGuardBandSlotDiff{0};
    long controlMovedVertexUvDiff{0};   // reported, not gated: UV components whose bits change
    bool pass() const;
    std::string json() const;
};
SwrParity swrParity(rhi::Device& dev);

}  // namespace raw::gpu_check
