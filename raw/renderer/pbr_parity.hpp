#pragma once
// GPU parity of the material model (evidence/m3-materials-bounds.json, gpu_parity and
// gpu_control): the WGSL pass pbr_eval (src/renderer/gpu/shaders/pbr.wgsl) evaluates
// f * |cos theta_i| and the emission for a seeded set of cases, in float32, and the
// float64 reference (raw/renderer/pbr.hpp) evaluates the same cases from the same
// float32-rounded inputs. Backend-neutral: it runs on whichever RHI device the build links.
#include "raw/renderer/pbr.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gpu_check {

inline constexpr int kPbrCaseFloats = 44;
inline constexpr std::uint32_t kPbrCases = 4096;
inline constexpr std::uint64_t kPbrSeed = 20261010;

// The seeded cases: materials over every family and direction pairs, both hemispheres.
struct PbrCase { pbr::Material m; pbr::D3 wo, wi; int family; };
std::vector<PbrCase> pbrCases(std::uint32_t count, std::uint64_t seed);
// Pack a case as the WGSL reads it, and round its fields to float32 in place so the
// reference sees exactly what the GPU sees.
void packCase(PbrCase& c, float* out);
// Every table, in the order and at the offsets pbr.wgsl names (OFF_A ... OFF_BAVG2).
std::vector<float> packTables(const pbr::Tables& t);

struct PbrParity {
    std::string error, backend, adapter;
    std::uint32_t cases{0};
    double worstRatio{0};        // max |gpu - ref| / (1e-3 |ref| + 1e-5); passes at <= 1
    double worstAbs{0};
    int worstCase{-1}, worstFamily{-1}, outside{0};
    double controlWorstRatio{0}; // the same with multiple scattering dropped on the GPU
    bool pass() const { return error.empty() && cases > 0 && worstRatio <= 1.0 && controlWorstRatio > 1.0; }
    std::string json() const;
};
PbrParity pbrParity(rhi::Device& dev, std::uint32_t count = kPbrCases);

}  // namespace raw::gpu_check
