#pragma once
// Palette quantisation and dither, the C++ core's float64 reference (ADR 0019). The GPU pass is
// style_dither in src/renderer/gpu/shaders/style.wgsl; the JavaScript reference it is checked
// against is web/shaders/dither/dither.mjs with the author's retro-dither.js (vendored as
// web/shaders/reference/retro-dither.mjs) and the mix modes of dither/mix.mjs.
//
// Pair modes: the nearest palette colour i in OKLab, the far-side colour j whose segment from i
// passes closest to the pixel (with a pair-distance penalty), and an ordered threshold t (Bayer
// 2/4/8, interleaved gradient noise or 64 x 64 blue noise) that picks j when the pixel's fraction
// along the segment exceeds 0.5 + (t - 0.5) strength. Mix modes: a plan of 16 palette colours whose
// sum in linear light tracks the pixel, sorted by lightness; the threshold picks one entry.
#include "raw/renderer/style_oklab.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

enum class DitherMode : std::uint32_t { None = 0, Bayer2, Bayer4, Bayer8, Noise, Blue, MixBayer4, MixBayer8, MixBlue };
DitherMode ditherMode(const std::string& name);   // throws std::invalid_argument on an unknown mode
struct DitherPalette {
    std::vector<PaletteEntry> entries;
    std::vector<V3> lin;                  // each entry's sRGB bytes in linear light (the mix modes)
};
DitherPalette ditherPalette(const std::string& name);
struct DitherOptions { DitherMode mode{DitherMode::Bayer4}; double strength{1}, brightness{0}; };
double pairPenalty();                     // 0.12 (after Yliluoma)
double ign(int x, int y);
// Palette indices of an RGBA8 plate; margin (when given) receives, per pixel, the smallest gap of
// a decision quantity to its rival (tie reports).
std::vector<std::uint8_t> ditherPlate(const std::vector<std::uint8_t>& rgba, int w, int h, const DitherPalette& pal, const DitherOptions& o,
                                      std::vector<double>* margin = nullptr);

}  // namespace raw::style
