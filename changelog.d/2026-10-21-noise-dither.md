- **Noise and dither modules** (`raw/renderer/noise.hpp`, `raw/renderer/dither.hpp`,
  `raw_native_cli noise-gallery`). One noise module (white, value, Perlin, simplex,
  Worley, fBm, ridged, billow, hybrid, domain warp, blue noise) and one dither module
  (Bayer, Floyd-Steinberg, Atkinson, Stucki, blue-noise thresholding, line and contour
  fills), with golden images that hash the same on GCC and MSVC, uniformity, spectrum,
  continuity and mean-preservation checks. GPU passes and moving the engine's own copies
  onto them come next.
