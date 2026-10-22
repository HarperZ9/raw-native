#pragma once
// The measures of evidence/fractal-dither-bounds.json: the rr-bounds temporal instability on
// 8-bit display codes, a paired bootstrap over frames, tone accuracy, level-change density and
// the golden hash. To be folded into art_metrics once that lands.
#include <cstdint>
#include <vector>
namespace raw::fractal {

std::uint8_t encode8(double linear);   // clamp, sRGB, round half up
// Mean over valid pixels and the three colour channels of |(out - warp(outPrev)) - (ref - warp(refPrev))|
// in [0, 1]; RGBA8 images, mv = current minus previous pixel position (2 floats), nearest-pixel warp.
// Returns -1 when no pixel is valid.
double instability(const std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& outPrev, const std::vector<std::uint8_t>& ref,
                   const std::vector<std::uint8_t>& refPrev, const std::vector<float>& mv, const std::vector<std::uint8_t>& valid, int w, int h);

struct Paired {
    double meanA{0}, meanB{0}, diff{0}, diffLo{0}, diffHi{0}, reduction{0}, redLo{0}, redHi{0};
    int frames{0};
    std::uint64_t seed{0};
};
// a against b over frames: 95% intervals of mean(a - b) and of 1 - mean(a) / mean(b).
Paired pairedBootstrap(const std::vector<double>& a, const std::vector<double>& b, std::uint64_t seed, int resamples = 10000);

// 2026-10-15 protocol A: 100 x mean |cbrt(box4(out)) - cbrt(box4(tone))| over 4 x 4 blocks whose pixels are all valid.
double accuracyA(const std::vector<double>& outLinear, const std::vector<double>& tone, const std::vector<std::uint8_t>& valid, int w, int h);
// Fraction of horizontally adjacent pixel pairs whose levels differ.
double changeDensity(const std::vector<std::uint8_t>& level, int w, int h);

std::uint64_t fnv1a(const std::vector<std::uint8_t>& v);

}  // namespace raw::fractal
