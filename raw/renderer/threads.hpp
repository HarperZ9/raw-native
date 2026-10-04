#pragma once
// Threads: particles flow along the zero set of one of fifteen form fields and
// leave luminous trails, tone-mapped by log density. The passes are
// src/renderer/gpu/shaders/threads.wgsl; this records them as a frame graph on
// the RHI, so the D3D12 build runs the same shader source as the web host
// (web/threads.mjs). A creative module: it has no CPU reference and writes no
// certificate (docs/architecture/adr/0010-web-host.md).
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::threads {
inline constexpr int kWorlds = 15;
struct Settings {
    int width{640}, height{360};
    uint32_t particles{1u << 18};
    int world{0};            // 0..14; 3 is Morphogen, which runs a reaction-diffusion field
    int frames{90};          // frames simulated at fps; the last one is returned
    uint32_t substeps{3};
    float fps{30.0f};
    float persistence{0.82f}, exposure{1.0f}, spacing{0.011f}, levels{5.0f};
};
struct Result {
    int width{0}, height{0};
    std::vector<uint8_t> rgba;   // row 0 at the top
    double msPerFrame{0};        // wall clock per simulated frame, submit to completion
};
// Render on `dev`. False with a reason when the build carries no Threads shaders
// for this device or the backend reports an error.
bool render(rhi::Device& dev, const Settings& s, Result& out, std::string& err);
}
