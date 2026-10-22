#pragma once
// The stylised track's measures (P5, P16 to P21): the rr-bounds temporal instability
// (evidence/rr-bounds.json, metrics.temporal_instability) on display codes, a paired bootstrap over
// frames, and the golden hash. One definition, used by every art pass.
#include <cstdint>
#include <vector>
namespace raw::art {

// The 8-bit sRGB code of a display-linear value: clamp, sRGB, round half up.
std::uint8_t encode8(double v);
// RGBA f32 display-linear rows to 8-bit codes (alpha written as 255).
std::vector<std::uint8_t> codes8(const std::vector<float>& rgba);

// Temporal instability of one frame: the mean over pixels with valid history and over the three
// colour channels of |(out_t - warp(out_{t-1})) - (ref_t - warp(ref_{t-1}))|, with values in [0, 1]
// (codes / 255) and warp the nearest previous pixel along mv (2 floats a pixel, current minus
// previous). Images are RGBA codes. Returns -1 when no pixel has valid history.
double instability(const std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& outPrev,
                   const std::vector<std::uint8_t>& ref, const std::vector<std::uint8_t>& refPrev,
                   const std::vector<float>& mv, const std::vector<std::uint8_t>& valid, int w, int h);

// A paired bootstrap of a against b over frames (equal lengths): 10,000 resamples by default.
struct Paired {
    double meanA{0}, meanB{0};
    double diff{0}, diffLo{0}, diffHi{0};          // mean(a - b) and its 95% interval
    double reduction{0}, redLo{0}, redHi{0};       // 1 - mean(a) / mean(b) and its 95% interval
    int frames{0};
    std::uint64_t seed{0};
};
Paired pairedBootstrap(const std::vector<double>& a, const std::vector<double>& b, std::uint64_t seed, int resamples = 10000);

// FNV-1a 64 over bytes, and over the bit patterns of floats.
std::uint64_t fnv1a(const std::vector<std::uint8_t>& v);
std::uint64_t fnv1a(const std::vector<float>& v);

}  // namespace raw::art
