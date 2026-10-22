#pragma once
// The engine's dither module (ROADMAP P8b; evidence/p8-noise-dither-bounds.json). Every method
// takes a grey image in [0, 1] (row-major, w x h) and returns it quantised to `levels` levels in
// [0, 1]. Ordered methods are per-pixel; error diffusion runs left to right, top to bottom.
//   Bayer (1973) ordered matrices 2, 4, 8 by the recursive construction
//   Floyd and Steinberg (1976): 7, 3, 5, 1 over 16
//   Atkinson (Apple, 1980s): 1/8 to six neighbours, a quarter of the error dropped
//   Stucki (1981): twelve neighbours over 42
//   blue-noise thresholding on the engine's void-and-cluster mask (raw/renderer/noise.hpp)
//   line fill: 45-degree hatching whose stroke covers the tone's share of each period
//   contour fill: dark lines on the edges between tone bands (on white paper at two levels)
#include <cstdint>
#include <vector>
namespace raw::dither {

enum class Method : std::uint8_t { Bayer2, Bayer4, Bayer8, FloydSteinberg, Atkinson, Stucki, BlueNoise, LineFill, ContourFill };
const char* name(Method m);
// The Bayer threshold rank at (x, y) for an n x n matrix (n a power of two), in 0 .. n*n - 1.
int bayerRank(int n, int x, int y);
std::vector<float> apply(Method m, const std::vector<float>& grey, int w, int h, int levels = 2, int period = 6, int bands = 6);

}  // namespace raw::dither
