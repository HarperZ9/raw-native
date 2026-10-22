#pragma once
// The measures of evidence/fractal-dither-bounds.json. The rr-bounds temporal instability, the
// paired bootstrap, encode8 and the golden hash are the stylised track's shared definitions in
// art_metrics (one definition for every art pass); this header adds the two measures only the
// fractal dither uses: tone accuracy and level-change density.
#include "raw/renderer/art_metrics.hpp"
#include <cstdint>
#include <vector>
namespace raw::fractal {

using art::encode8;
using art::fnv1a;
using art::instability;
using art::Paired;
using art::pairedBootstrap;

// 2026-10-15 protocol A: 100 x mean |cbrt(box4(out)) - cbrt(box4(tone))| over 4 x 4 blocks whose pixels are all valid.
double accuracyA(const std::vector<double>& outLinear, const std::vector<double>& tone, const std::vector<std::uint8_t>& valid, int w, int h);
// Fraction of horizontally adjacent pixel pairs whose levels differ.
double changeDensity(const std::vector<std::uint8_t>& level, int w, int h);

}  // namespace raw::fractal
