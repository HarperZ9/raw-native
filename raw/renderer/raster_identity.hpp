#pragma once
// RHI version 3 checks (evidence/m3-rhi3-bounds.json): the colour formats round-trip a
// pattern exactly, the depth test makes draw order irrelevant, and the hardware G-buffer
// (raster pass gbuffer) matches the CPU rasterizer pixel for pixel away from triangle edges.
// Backend-neutral: they run on whichever RHI device the build links.
#include "raw/rhi/rhi.hpp"
#include "raw/scene/scene.hpp"
#include <string>
#include <vector>
namespace raw::gpu_check {

struct FormatCase { std::string format; int texels{0}, mismatches{0}; };
struct GbufferCase {
    std::string scene;
    int width{0}, height{0};
    int both{0};                       // pixels both sides cover with the same triangle
    int coverageOrIdDiffs{0};          // pixels covered by one side only, or by different triangles
    int diffsExplained{0};             // of those, within 0.01 px of an edge or at a depth tie
    double worstPosition{0}, worstNormal{0}, worstDistance{0};   // ratios to their bounds (pass at <= 1)
    int albedoDiffs{0};
    std::vector<std::string> unexplained;   // up to 8 differing pixels nothing explains, as JSON objects
    bool pass() const { return both > 0 && diffsExplained == coverageOrIdDiffs && worstPosition <= 1 && worstNormal <= 1 && worstDistance <= 1 && albedoDiffs == 0; }
};
struct RasterIdentity {
    std::string error, backend, adapter;
    std::vector<FormatCase> formats;
    bool depthOrderEqual{false}, depthControlDiffers{false};
    std::vector<GbufferCase> scenes;
    bool controlFails{false};          // the G-buffer comparison with one triangle moved
    int subpixelBits{0};               // the rasterizer's sub-pixel precision the reference used
    bool pass() const;
    std::string json() const;
};
// Scenes are (name, scene) pairs; the first is also used for the moved-triangle control.
// subpixelBits: the CPU reference snaps vertices to 1 / 2^bits of a pixel, as the rasterizer
// does (D3D12 mandates 8; WebGPU leaves it to the implementation, and SwiftShader uses 4).
// 0 (the default) measures it with a probe triangle first.
// The rasterizer's sub-pixel precision in bits (4 or 8), from a probe triangle; 0 with `err` set on error.
int probeSubpixelBits(rhi::Device& dev, std::string& err);
RasterIdentity rasterIdentity(rhi::Device& dev, const std::vector<std::pair<std::string, const Scene*>>& scenes, int size = 256, int subpixelBits = 0);

}  // namespace raw::gpu_check
