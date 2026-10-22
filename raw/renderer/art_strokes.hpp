#pragma once
// P5, world-anchored strokes against the advected canvas (evidence/p5-world-strokes-bounds.json):
// the orbit run that measures both with the warp metric, the display reference, one world-anchored
// paint frame (float64), and the GPU chain (art_anchor, then the painterly shader's passes).
#include "raw/renderer/art_anchor.hpp"
#include "raw/renderer/art_metrics.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::art {

struct StrokeOrbit {
    int w{320}, h{180}, frames{48}, first{9};
    double step{1.0};
    int threads{1};
    std::string preset{"oil"};
};
struct StrokeOrbitResult {
    std::vector<double> world, advected, shuffled, offset;   // instability of frames first .. frames - 1
    Paired worldVsAdvected, shuffledVsAdvected, offsetVsAdvected;
};
StrokeOrbitResult runStrokeOrbit(const StrokeOrbit& o, std::uint64_t seed);

// The lit frame through the paint prep's display transform (the shoulder at `exposure`, then
// sRGB), as RGBA codes: the warp metric's reference.
std::vector<std::uint8_t> paintReference(const std::vector<float>& lit, double exposure);
// One frame painted with the world anchor (float64 reference); display-linear RGBA.
std::vector<float> paintWorld(const Frame& f, const AnchorParams& a, const style::PaintPlan& plan);

}  // namespace raw::art

namespace raw::gpu_check {
struct StrokesGpu {
    std::string error;
    std::vector<float> anchor;               // RGBA a pixel, as art::anchorCanvas
    std::vector<std::uint32_t> packed;       // the world-anchored paint frame, RGBA8 a pixel
    // Median milliseconds a frame with the work recorded 10 times a submission (the submission and
    // fence latency amortised); anchorSingleMs is the anchor alone, one dispatch a submission.
    double anchorMs{0}, advectMs{0}, anchorSingleMs{0};   // the anchor pass; the advected canvas's three passes
    double paintWorldMs{0}, paintAdvectedMs{0};
};
// The anchor and the paint frame it feeds, on `dev`; with repeats > 0 also the warm timings.
bool strokesOnGpu(rhi::Device& dev, const art::Frame& f, const art::AnchorParams& a, const style::PaintPlan& plan, StrokesGpu& out, int repeats);
}  // namespace raw::gpu_check
