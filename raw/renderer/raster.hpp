#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/scene/scene.hpp"
#include <cstdint>
namespace raw {
// ids, when given, receives the 1-based index of the mesh nearest at each pixel (0 where
// nothing covers it), for id-edge outlines (raw/renderer/outline.hpp). Without it the
// output is unchanged.
GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena = nullptr, Buffer<uint16_t>* ids = nullptr);
// Options for checks against hardware rasterization (ROADMAP M3, evidence/m3-rhi3-bounds.json).
// perspectiveDepth: the depth test and the depth plane use the perspective-correct view
// distance 1 / (interpolated 1/w) instead of the screen-linear one (the default, kept so no
// committed golden changes). triangleIds: receives 1 + the scene-order index of the nearest
// triangle at each pixel (0 where nothing covers it).
// subpixelBits: when > 0, screen-space vertex positions snap to 1 / 2^bits of a pixel before
// coverage and interpolation, as D3D12 (16.8 fixed point) and WebGPU rasterizers do.
struct RasterOptions { bool perspectiveDepth{false}; Buffer<uint32_t>* triangleIds{nullptr}; int subpixelBits{0}; };
GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena, Buffer<uint16_t>* ids, const RasterOptions& opt);
}
