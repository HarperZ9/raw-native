#pragma once
// Private to src/tools/present/: the passes of the presentation host and one frame recorded as
// a frame graph (N2, evidence/n2-presentation-bounds.json).
//   presented  scene -> offscreen texture -> blit -> backbuffer -> read-back -> Present
//   offscreen  scene -> offscreen texture -> read-back
// The frame graph owns the Present: the blit and the read-back declare the swapchain resource
// that FrameGraph::presentTo returns, and the hash is taken in its before-present step.
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::present {

struct Pipelines {
    rhi::RasterPipelineHandle scene, blit, pq, pqControl;
};
// Every pass on `dev`, from the shader table of the device's format (DXIL or SPIR-V). The blit
// targets `blitFormat`, the swapchain's format (BGRA8Unorm where a Vulkan surface offers only it);
// the PQ passes target RGB10A2Unorm.
bool createPipelines(rhi::Device& dev, rhi::TextureFormat blitFormat, Pipelines& p, std::string& err);

// The per-size resources of a frame.
struct Targets {
    uint32_t width{0}, height{0};
    rhi::TextureHandle offscreen;                        // RGBA8Unorm, RenderTarget | Sampled | CopySrc
    rhi::BufferHandle readback, sceneUniform, blitUniform;
};
bool makeTargets(rhi::Device& dev, uint32_t w, uint32_t h, Targets& t, std::string& err);
void dropTargets(rhi::Device& dev, Targets& t);

// SHA-256 of tight rows of 4-byte texels (the copy's row padding dropped), top row first, as
// RGBA: a BGRA8 image is swizzled first, so the hash does not depend on the swapchain's order.
std::string hashRows(const void* mapped, uint32_t w, uint32_t h, uint32_t pitch, bool bgra = false);

struct FrameSpec {
    uint32_t frame{0};
    uint32_t seed{0x5EED};
    int32_t blitOffsetX{0};      // the control sets 1
    uint32_t syncInterval{1};
    bool readback{true};
    bool keepPixels{false};      // also return the read-back image (tight RGBA8 rows)
};
struct FrameOut {
    std::string hash;            // empty when there was no read-back
    rhi::PresentStatus status{rhi::PresentStatus::NotPresented};
    std::string graph;           // FrameGraph::describe() of the frame
    std::vector<unsigned char> pixels;   // with keepPixels
};
// One frame. With a swapchain the frame is presented; without one it is rendered offscreen.
// The targets must match the swapchain's size.
bool runFrame(rhi::Device& dev, const Pipelines& p, Targets& t, rhi::Swapchain* sc, const FrameSpec& spec,
              FrameOut& out, std::string& err);

}  // namespace raw::present
