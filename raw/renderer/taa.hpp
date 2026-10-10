#pragma once
// Temporal anti-aliasing, the CPU reference of the GPU resolve, and the frames it is measured
// on (ROADMAP M3 criterion 2; bounds in evidence/m3-post-bounds.json): Halton(2, 3) jitter,
// history reprojected by per-pixel motion and read with a Catmull-Rom filter, a depth disocclusion test, a YCoCg neighbourhood
// clip, and an exponential blend.
#include "raw/scene/scene.hpp"
#include <cstdint>
#include <vector>
namespace raw::taa {

// One frame's inputs to the resolve, 128 x 128 for the checks.
struct Frame {
    int w{0}, h{0};
    std::vector<float> rgb;       // shaded colour, 3 floats a pixel (linear)
    std::vector<float> depth;     // view depth, negative where nothing was drawn
    std::vector<float> prev;      // per pixel: previous screen position x, y (pixels) and expected previous view depth
    std::vector<float> coverage;  // fraction of the pixel the moving mesh covers (reference frames only)
};
inline constexpr float kBackground = 0.05f, kAmbient = 0.2f, kBlend = 0.1f, kClipSigma = 1.25f;

// Halton(2, 3) jitter of phase k (of 16), in pixels, in [-0.5, 0.5).
void jitter(int k, float& jx, float& jy);
// Shade one frame: `scene` at this frame, `prevCam` and `moved` (the last mesh's world motion
// since the previous frame) give each pixel's previous position. supersample > 1 renders
// supersample^2 stratified samples a pixel, box filtered (the reference), and fills coverage.
Frame render(const Scene& scene, const Camera& prevCam, Vec3 moved, int w, int h, float jx, float jy, int supersample = 1);

struct Options { bool clip{true}, depthTest{true}; };
// One resolve step: history (3 floats a pixel, empty on the first frame) and the previous
// frame's depth give the new history.
std::vector<float> resolve(const Frame& cur, const std::vector<float>& history, const std::vector<float>& prevDepth, const Options& o = {});

}  // namespace raw::taa
