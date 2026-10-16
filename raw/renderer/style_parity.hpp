#pragma once
// The checks of the style passes ported into the C++ core (ADR 0019; bounds in
// evidence/cpp-port-style-bounds.json). Three links per pass:
//   cpu   the C++ float64 reference against the JavaScript float64 reference, on the fixtures that
//         tools/port/export_style_fixtures.mjs exports from the web modules;
//   gpu   the one WGSL source (style.wgsl) on this build's backend against the C++ reference.
// Look keys have the CPU link only. Every link carries a control that must fail. Backend-neutral:
// without a device the GPU links are reported as skipped, never as passed.
#include "raw/rhi/rhi.hpp"
#include <string>
#include <vector>
namespace raw::gpu_check {

struct StyleCase {
    std::string pass, id;
    long decisions{0};              // pixels compared
    // CPU link
    double cpuMaxAbs{0};            // tone (dither lighting) or colour (paint-over)
    long cpuMismatch{0}, cpuExcused{0};   // decision mismatches: unexcused, excused as float64 ties
    long cpuRgbaMismatch{0};        // 8-bit mismatches where the decisions agree (dither) / hit-count mismatches (paint-over)
    long covered{0};                // paint-over: pixels the C++ reference covers
    long surface{0};                // paint-over: surface pixels in the view
    bool planEqual{true};           // dither: the C++ preset and overrides give the exported plan
    bool cpuControlFails{false};
    bool viewShiftControlFails{false};   // paint-over: the control as first written (reported only; Addendum A)
    bool cpuPass{false};
    // GPU link
    bool gpuRun{false};
    long gpuMismatch{0};
    double gpuMismatchFraction{0}, gpuOver2Fraction{0}, gpuMean{0};
    int gpuMax{0};
    bool gpuControlFails{false};
    bool gpuPass{false};
    // Round 2 image passes (purity, lightpaint, glaze, hatch; evidence/cpp-port-style-bounds-2.json)
    bool presetEqual{true};         // the C++ preset gives the exported plan
    long hitMismatch{0}, fallbackMismatch{0};   // hatch: ray hits, and umbilic fallbacks where both hit
    int gpuP999{0};
};
// The post stack's driver (round 2): plans against web/motion/post.mjs, chains on the host and the
// device against the JavaScript cpu() chains.
struct PostChain {
    std::string id;
    long values{0};
    double cpuMaxUlp{0}, cpuDifferingFraction{0};
    bool cpuControlFails{false}, cpuPass{false};
    bool gpuRun{false};
    double gpuMaxRel{0};
    bool gpuControlFails{false}, gpuPass{false};
};
struct PostStackResult {
    int lists{0};
    int planMismatch{0};
    bool planControlDiffers{false};
    std::vector<PostChain> chains;
    bool cpuPass() const;
    bool gpuPass() const;
};
struct LookKeysResult {
    int series{0};
    long samples{0};
    double maxAbs{0};
    bool keysExact{false}, refusalsMatch{false}, unknownRefused{false};
    bool controlFails{false};
    bool pass() const { return series > 0 && samples > 0 && maxAbs <= 1e-12 && keysExact && refusalsMatch && unknownRefused && controlFails; }
};
struct StyleParity {
    std::string error, backend, adapter;
    bool gpuWanted{true};
    int expectDither{0}, expectPaint{0};   // cases in the fixture bundles
    int expectImage{0};                    // round 2 image cases in their bundles
    int expectQuant{0};                    // hysteresis and palettelight cases (bounds-3)
    std::vector<StyleCase> cases;
    LookKeysResult lookkeys;
    PostStackResult poststack;
    bool cpuPass() const;
    bool gpuPass() const;           // false when the GPU links did not run
    bool pass() const { return cpuPass() && (!gpuWanted || gpuPass()); }
    std::string json() const;
};
// fixtureDir holds ditherlight.rnfx, paintover.rnfx, lookkeys.json and, from round 2,
// poststack.rnfx, purity.rnfx, lightpaint.rnfx, glaze.rnfx, hatch.rnfx, hysteresis.rnfx and
// palettelight.rnfx. dev may be null (CPU only).
StyleParity styleParity(rhi::Device* dev, const std::string& fixtureDir);

}  // namespace raw::gpu_check
