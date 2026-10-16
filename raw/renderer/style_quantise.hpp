#pragma once
// Two quantising style passes, the C++ core's float64 references (ADR 0019). The GPU passes are
// style_hysteresis and style_palettelight in src/renderer/gpu/shaders/style.wgsl; the JavaScript
// references they are checked against are web/shaders/hysteresis/hysteresis.mjs and
// web/shaders/palettelight/, whose headers carry the models.
//
// Hysteresis quantisation: band or palette decisions that remember the previous frame. Each pixel
// reprojects through its motion vector to the previous frame's decision and keeps it while the
// input stays within the decision's interval widened by a margin (a Schmitt trigger), unless the
// reprojection lands on another surface (view distance off by more than depthTol) or the plain
// decision has differed for K frames in a row.
// Palette-locked lighting: light resolved into the palette's own ramps (cool and warm variants per
// material class, a cycling void ramp, short emissive ramps), with Bayer 8 x 8 thresholds at two
// phases choosing the step and the variant, so every output pixel is a palette entry.
#include "raw/renderer/style_ditherlight.hpp"   // V3, DitherLight
#include "raw/renderer/style_oklab.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

struct HystPlan {
    bool bands{true};
    int N{6};
    double margin{0.3}, persistSec{0.2}, fps{24}, gain{2.4}, depthTol{0.03};
    double keepHalf{0.5};               // the 0.5 of the keep interval; a source-edit control changes it
    std::vector<PaletteEntry> pal;      // palette mode
    int K{5};                           // max(1, round(persistSec fps))
};
HystPlan hystPreset(const std::string& name);   // "bands6" or "pico8"
struct HystState { std::vector<std::int32_t> idx, cnt; std::vector<float> dist; };
struct HystFrame {
    std::vector<std::int32_t> idx;
    std::vector<std::uint8_t> rgba;
    std::vector<double> margin;         // per pixel, the smallest distance of a decision quantity to its threshold
};
// One frame. rgba: scene-linear RGBA f32; mv (2 a pixel) and dA may be empty (static camera);
// dist: this frame's view distance. state: the previous frame's, empty on the first frame; it is
// replaced by this frame's.
HystFrame hystStep(const HystPlan& p, int w, int h, const std::vector<float>& rgba, const std::vector<float>& mv, const std::vector<float>& dA,
                   const std::vector<float>& dist, HystState& state);

// The ramp palette of web/shaders/palettelight/palette.mjs.
struct RampPalette {
    std::vector<PaletteEntry> entries;
    int steps{4}, voidSteps{6};
    std::vector<int> ramp[4][2];        // ground, stone, wood, foliage x (cool, warm) -> entry per step
    std::vector<int> voidRamp, window, lamp;
    int night{0};
};
RampPalette buildRampPalette(int steps, int voidSteps);
struct PalettelightPlan {
    double gain{1.5}, ambient{0.08}, moon{0.45}, cycleRate{0.18};
    int steps{4}, voidSteps{6};
    V3 moonDir{};
    double ambientBase{0.7};            // the 0.7 in ambient (0.7 + 0.3 (0.5 + 0.5 n.y)); a source-edit control changes it
    DitherLight lamp, glows[2];
    RampPalette palette;
};
// The defaults of PL_DEFAULTS with the lamp at lampPos; steps and voidSteps rebuild the palette.
PalettelightPlan palettelightPlan(const V3& lampPos, int steps = 4, int voidSteps = 6);
// A Chapel Yard G-buffer (web/shaders/fixtures/chapel-render.mjs).
struct ChapelGBuffer {
    int w{0}, h{0};
    std::vector<std::uint8_t> hit, cls;
    std::vector<float> a, phase, P, N, vis;   // P, N, vis: 3 floats a pixel
};
struct PalettelightFrame { std::vector<std::int32_t> idx; std::vector<std::uint8_t> rgba; std::vector<double> margin; };
PalettelightFrame runPalettelight(const PalettelightPlan& p, const ChapelGBuffer& g, double time);

}  // namespace raw::style
