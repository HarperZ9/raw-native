#pragma once
// The noise and dither modules' golden images (evidence/p8-noise-dither-bounds.json n1 and d1):
// one image per noise function and recipe, and one per dither method on a ramp and a test card.
// tests/test_noise.cpp and tests/test_dither.cpp pin their hashes; raw_native_cli noise-gallery
// writes them as PNGs for review.
//   raw_native_cli noise-gallery [--out DIR]
#include <string>
#include <vector>
namespace raw {
struct GreyImage { std::string name; int w{0}, h{0}; std::vector<float> px; float lo{0.0f}, hi{1.0f}; };   // lo, hi: the PNG's display range
std::vector<GreyImage> noiseGoldens(int size = 128);
// The dither inputs: a horizontal ramp (256 x 64) and a synthetic test card (128 x 128).
GreyImage ditherRamp();
GreyImage ditherCard();
std::vector<GreyImage> ditherGoldens();
int noiseGalleryCommand(int argc, char** argv);
// raw_native_cli noise-parity (src/tools/noise_parity_cmd.cpp; evidence/p8-noise-gpu-bounds.json).
int noiseParityCommand(int argc, char** argv);
}
