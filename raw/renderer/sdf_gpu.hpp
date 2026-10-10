#pragma once
// The ray marcher on the GPU (src/renderer/gpu/shaders/sdf.wgsl) driven from the host, and its
// checks against the float64 reference (evidence/rt-r3-bounds.json, M2 to M7).
#include "raw/renderer/sdf.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {

struct SdfGpu {
    std::string error;
    std::vector<float> march;       // 8 a pixel: t (-1 a miss), material, closest, steps, normal (3), pad
    std::vector<float> god;         // 4 a pixel: in-scatter, clear samples, end distance, pad
    std::vector<float> terms;       // 2 a point: soft shadow, AO
    std::vector<float> fog;         // 4 a case: homogeneous, height closed form, march 256, march 4
    std::vector<float> composite;   // 4 a pixel: winner (0 raster, 1 SDF, -1 none), SDF z/w, pad 2
};
struct SdfJob {
    int w{256}, h{256};
    bool god{false};
    std::uint32_t flags{0};                  // 1 sun visibility forced to 1, 2 SDF always in front
    float softK{12.0f};
    std::vector<float> points;               // 6 a point for the terms pass (empty: skip)
    std::vector<float> fogCases;             // 8 a case for the fog pass (empty: skip)
    const std::vector<std::uint32_t>* rasterVis{nullptr};   // 2 a pixel (slot + 1, integer depth): compositing
    const std::vector<float>* rasterDepth{nullptr};         // R1 z/w a pixel
    float nearZ{0.1f}, farZ{100.0f};
};
// The march over every pixel, then the passes the job asks for, in one submission.
SdfGpu sdfGpu(rhi::Device& dev, const sdf::Scene& s, const SdfJob& job);

struct SdfParity {
    std::string error, backend, adapter, results;
    bool allPass{false};
    int size{256};   // M2 to M5; M6 runs at 256 x 256 always
    bool pass() const { return error.empty() && allPass; }
    std::string json() const;
};
// quick: 128 x 128 instead of 256 x 256 (SwiftShader in a browser, where the CPU references are single-threaded).
SdfParity sdfParity(rhi::Device& dev, bool quick = false);

}  // namespace raw::gpu_check
