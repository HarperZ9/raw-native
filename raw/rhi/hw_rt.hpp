#pragma once
// Hardware ray tracing on D3D12 (DXR tier 1.1 inline ray queries), as a library. The contract that
// keeps it honest against the CPU reference is docs/hw-rt.md; bounds in evidence/hw-h2-*.json.
//
// Three entry points, all plain data so any caller can use them (raw-native's renderer, and tools
// that only have triangles and rays, such as a lens bench or a lidar simulation):
//   RayCaster        a persistent scene: build once, refit when vertices move, cast batches of rays.
//   pathTracePasses  the path tracer's own passes (hw_pt.hlsl, generated from pt_trace.hlsl) over a
//                    hardware structure; raw/renderer/rt_pt_hw.hpp wraps it for rt::PtScene.
//   spectralPasses   the spectral caustic tracer (hw_caustic.hlsl); raw/renderer/rt_caustic.hpp wraps it.
// Backends without DXR (null, WebGPU, a device without tier 1.1, RAW_NATIVE_HW_DISABLE=ray_query)
// return ran = false with the reason, and the caller takes its CPU path.
#include "raw/rhi/hw.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace raw::rhi::hw {

// A persistent hardware scene over a triangle soup (9 floats a triangle). Not thread safe.
class RayCaster {
public:
    // allowUpdate: the structure may be refit (slightly slower to trace). offset moves the instance,
    // which exists for the planted-offset control of the contract.
    static std::unique_ptr<RayCaster> create(Device& dev, const std::vector<float>& triangles, bool allowUpdate,
                                             std::string& err, const float offset[3] = nullptr);
    virtual ~RayCaster() = default;
    // New positions for the same triangles (the count must not change); needs allowUpdate.
    virtual bool refit(const std::vector<float>& triangles, std::string& err) = 0;
    // Closest hit of every ray (tri -1 on a miss); u, v weight vertices b and c. RayIn and HitOut
    // are H1.1's types (raw/rhi/hw.hpp): tmin and tmax bound the hit distance.
    virtual bool closest(const std::vector<RayIn>& rays, std::vector<HitOut>& hits, std::string& err) = 0;
    virtual double buildMs() const = 0;   // GPU time of the last build or refit
    virtual double traceMs() const = 0;   // GPU time of the last closest()
};

// The path tracer's passes on a hardware structure. Buffers are those of pt_trace.hlsl (T, A, TX, M;
// see src/renderer/gpu/rt_pt_gpu.cpp's packing). sliceParams holds the 56 uniform words of each
// slice's dispatch, finishParams those of pt_finish. out: 36 floats a pixel (pt_finish's O).
struct PtPassInput {
    uint32_t width{0}, height{0};
    std::vector<float> tris, attrs, mats;
    std::vector<uint32_t> tex;
    std::vector<std::vector<uint32_t>> sliceParams;
    std::vector<uint32_t> finishParams;
    float offset[3]{0, 0, 0};   // the planted-offset control
};
struct PassTiming {
    double buildMs{0}, finishMs{0}, wallMs{0};
    std::vector<double> sliceMs;   // GPU time of each traced slice
};
struct PtPassResult { bool ran{false}; std::string error; std::vector<float> out; PassTiming timing; };
PtPassResult pathTracePasses(Device& dev, const PtPassInput& in);

// The spectral caustic tracer's passes (src/rhi/d3d12/hw_shaders/hw_caustic.hlsl). The layout of
// every buffer is given by raw/renderer/rt_caustic.hpp, which packs them; this level only binds.
struct SpectralPassInput {
    uint32_t width{0}, height{0};
    std::vector<float> tris;               // 9 floats a triangle
    std::vector<float> geo;                // per-triangle shading data
    std::vector<float> tables;             // materials, glasses, lights, colour matching, illuminant
    std::vector<std::vector<uint32_t>> cameraParams, lightParams;   // 32 words a dispatch (hw_caustic.hlsl's CP)
    float offset[3]{0, 0, 0};
};
struct SpectralPassResult {
    bool ran{false};
    std::string error;
    std::vector<float> camera;             // 4 floats a pixel: XYZ sum of the camera pass, sample count
    std::vector<uint64_t> splat;           // 3 words a pixel: the light pass's XYZ in 2^-24 fixed point
    PassTiming timing;
    std::vector<double> lightMs;           // GPU time of each light dispatch
};
SpectralPassResult spectralPasses(Device& dev, const SpectralPassInput& in);

}  // namespace raw::rhi::hw
