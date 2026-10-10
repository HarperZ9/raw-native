#pragma once
// The sampled-texture identity check (ROADMAP M2 criterion 3): a raster pipeline on
// the RHI samples an uploaded texture through four samplers into a render target,
// and the read-back pixels are compared with the CPU sampler reference
// (raw/renderer/sampler.hpp). Bounds: evidence/m2-rhi-texture-bounds.json.
// Backend-neutral: it runs on whichever RHI device the build links.
#include "raw/rhi/rhi.hpp"
#include <string>
#include <vector>
namespace raw::gpu_check {

struct TextureCase {
    std::string name;           // e.g. "nearest/clamp"
    int pixels{0};              // pixels compared
    int exempt{0};              // nearest only: sample points within 1/1000 texel of an edge
    int maxCodes{0};            // largest channel difference from the reference, in 8-bit codes
    bool pass{false};
};
struct TextureIdentity {
    std::string error;          // set when the check could not run
    std::string backend, adapter;
    std::vector<TextureCase> cases;
    bool controlFails{false};   // the reference with the source rows swapped must not match
    bool pass() const { return error.empty() && !cases.empty() && controlFails && [&]{ for (auto& c : cases) if (!c.pass) return false; return true; }(); }
    std::string json() const;
};
TextureIdentity textureIdentity(rhi::Device& dev);

}  // namespace raw::gpu_check
