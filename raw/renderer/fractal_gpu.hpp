#pragma once
// The GPU link of the fractal dither (evidence/fractal-dither-bounds.json f5 and f6): the
// style_ditherlight pass with anchor 3 against the float64 reference on the courtyard orbit,
// and its GPU time at 1080p by timestamps, beside the screen anchor.
#include "raw/rhi/rhi.hpp"
#include <string>
#include <vector>
namespace raw::gpu_check {

struct FractalCase {
    std::string id;
    long decisions{0}, mismatch{0};
    double mismatchFraction{0}, over2Fraction{0}, meanCode{0};
    bool controlFails{false}, pass{false};
};
struct FractalGpu {
    std::string backend, adapter, error;
    std::vector<FractalCase> cases;
    bool timed{false};
    double msFractal{-1}, msScreen{-1}, msExtra{0};
    std::string timeError;
    bool parityPass() const;
    bool pass() const { return parityPass() && (!timed || (msFractal >= 0 && msScreen >= 0 && msExtra <= 0.3)); }
    std::string json() const;
};
// Parity on frames 0 and 24 at 256 x 144 for ink and tones4; with `cost`, the 1080p timing too.
FractalGpu fractalGpu(rhi::Device& dev, bool cost);
// The bounds: the ditherlight GPU bound of evidence/cpp-port-style-bounds.json.
inline constexpr double kFractalMismatch = 0.0005, kFractalOver2 = 0.0005, kFractalMeanCode = 0.12;

}  // namespace raw::gpu_check
