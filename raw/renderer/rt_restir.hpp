#pragma once
// ReSTIR DI on the CPU path tracer's punctual lights (ROADMAP proposal P1, stage 1; bounds in
// evidence/restir-di-bounds.json). Direct light from PtScene::lights at each pixel's primary hit
// (the pixel centre), shaded by the path tracer v3 material evaluation. Every sampled mode casts
// one shadow ray a pixel, so modes compare at equal shadow rays.
//   Bitterli, Wyman, Pharr, Shirley, Lefohn, Jarosz, "Spatiotemporal reservoir resampling for
//   real-time ray tracing with dynamic direct lighting", SIGGRAPH 2020 (algorithms 3, 4 and 6).
#include "raw/renderer/rt_pathtrace.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rt {

enum class DiMode : std::uint8_t {
    Exact,         // every light with its own shadow ray: the reference
    Uniform,       // one light chosen uniformly, weight N
    Ris,           // M uniform candidates resampled by the unshadowed contribution's luminance
    Restir,        // Ris, then one spatial pass with the unbiased 1/Z normalisation
    RestirBiased,  // the same pass with 1/M (biased; recorded)
};
struct DiDesc {
    int width{256}, height{256};
    PtCamera camera;
    std::uint64_t seed{1};
    DiMode mode{DiMode::Ris};
    int candidates{32};      // M
    int neighbours{5};       // k
    float radius{16.0f};     // pixels
    int threads{0};
    bool controlDropOneOverM{false};   // r2b false control: Ris without its 1/M
};
struct DiOutput {
    int width{0}, height{0};
    std::vector<float> radiance;   // 3 a pixel
    std::vector<int> covered;      // 1 where the pixel centre hits geometry
    std::uint64_t shadowRays{0};
    std::string error;
};
DiOutput directLightCpu(const PtScene& s, const DiDesc& d);

// n point lights by a seeded LCG inside [lo, hi], intensities log-uniform over [i0, 30 i0]
// candela, each with a range (the test and the restir-di command's light sets).
std::vector<lighting::Light> scatterLights(int n, Vec3 lo, Vec3 hi, std::uint64_t seed, double i0, double range);

}  // namespace raw::rt
