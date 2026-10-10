#pragma once
// GPU parity of the lighting (evidence/m3-lighting-bounds.json): the clustered light
// assignment (light_cluster), the GGX prefilter (light_prefilter) and the shading of
// surface samples with clustered punctual lights and split-sum image lighting (pbr_shade),
// each against the float64 reference in raw/renderer/lighting.hpp. Backend-neutral.
#include "raw/renderer/lighting.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {

inline constexpr int kLightFloats = 16, kSampleFloats = 48;
inline constexpr std::uint32_t kLightingSamples = 4096;
inline constexpr std::uint64_t kLightingSeed = 20261010;
inline constexpr double kLightingEv100 = 9.0;

// Pack lights as light.wgsl reads them (spot scale and offset computed in double here).
std::vector<float> packLights(const std::vector<lighting::Light>& ls);
// Pack a sample as pbr.wgsl's pbr_shade reads it, rounding its fields to float32 in place.
void packSample(lighting::Sample& s, float* out);
// The cube and its harmonics as one float array: every level, then 27 floats of SH.
std::vector<float> packEnvironment(const lighting::Cube& c, const std::vector<lighting::Rgb>& sh);

struct LightingParity {
    std::string error, backend, adapter;
    // Clusters, over 8 scenes of 256 lights.
    long clusterDiffs{0}, clusterBoundaryDiffs{0}, gpuMisses{0}, missPoints{0};
    // Prefilter, every texel of levels 1..5.
    double prefilterWorstRatio{0}, prefilterControlRatio{0};
    // Shading, kLightingSamples samples.
    std::uint32_t samples{0};
    double shadeWorstRatio{0}, shadeWorstAbs{0}, shadeControlRatio{0};
    int shadeWorstSample{-1}, shadeOutside{0};
    bool pass() const {
        return error.empty() && samples > 0 && clusterDiffs == 0 && gpuMisses == 0 && prefilterWorstRatio <= 1.0 &&
               shadeWorstRatio <= 1.0 && prefilterControlRatio > 1.0 && shadeControlRatio > 1.0;
    }
    std::string json() const;
};
LightingParity lightingParity(rhi::Device& dev);

// Shade samples on the GPU end to end: prefilter level 0 of `env` on the GPU, assign the
// lights to `grid`'s clusters on the GPU, then pbr_shade in chunks. `out` receives exposed
// radiance, three floats a sample. Returns false with `err` set when the device cannot.
bool shadeOnGpu(rhi::Device& dev, const lighting::ClusterGrid& grid, const std::vector<lighting::Sample>& samples,
                const std::vector<lighting::Light>& lights, const lighting::Cube& env, const std::vector<lighting::Rgb>& sh,
                double exposure, std::vector<float>& out, std::string& err);

}  // namespace raw::gpu_check
