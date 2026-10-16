#pragma once
// Paint-over projection, the C++ core's float64 reference (ADR 0019). A painting made over a
// render from one camera is projected back onto the geometry and becomes the surface colour. The
// GPU pass is style_paintover in src/renderer/gpu/shaders/style.wgsl; the JavaScript reference
// it is checked against is web/shaders/paintover/.
//
// A point P with normal N is covered by a projector when it lands inside the painting, faces the
// projector (f = N . toProjector at least fMin) and the projector's depth map does not hide it
// (depth - mapDepth at most tol, tol = pixelWorld * (tolBase + tolGrazing (1 - f))). Projectors
// blend by view angle: w_k = f_k^p * max(window(angle_k / reach_k), floor), window(a) =
// max(1 - a^2, 0)^2. With keyframe reach, a projector's window reaches to the nearest other
// projector's direction (at most thetaMax, at least 8 degrees), so each projector's own view shows
// its painting unchanged.
#include "raw/renderer/style_ditherlight.hpp"   // V3
#include <cstdint>
#include <optional>
#include <vector>
namespace raw::style {

// A camera: orthographic (scale is the half-height in world units) or perspective (fovY radians).
struct PaintCamera {
    bool ortho{true};
    V3 pos{}, fwd{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0};
    double scale{8}, fovY{0.9};
};
PaintCamera makePaintCamera(bool ortho, V3 pos, V3 fwd, double scale = 8, double fovY = 0.9);
// yawDeg about the vertical (45 is the classic corner), elevDeg above the horizon (35.264 is true
// isometric), looking at target from dist.
PaintCamera isoCamera(double yawDeg = 45, double elevDeg = 35.264, V3 target = {0, 1, 0}, double dist = 60, double scale = 8,
                      bool ortho = true, double fovY = 0.5);
// World point to (pixel x, pixel y, depth along the view axis); the first pixel centre is (0, 0).
V3 project(const PaintCamera& c, int w, int h, const V3& P);
V3 toCamera(const PaintCamera& c, const V3& P);
double pixelWorld(const PaintCamera& c, int h, double depth);

struct PaintOptions {
    double thetaMaxDeg{40}, facingPow{2}, fMin{0.12}, floor{1e-4}, tolBase{2}, tolGrazing{6};
    bool reach{true};          // keyframe reach (the default for new code); false: fixed thetaMax
    double sampleScale{255};   // the 8-bit divisor of a texel; a control changes it, nothing else does
};
struct Projector {
    PaintCamera cam;
    int w{0}, h{0};
    std::vector<std::uint8_t> rgba;   // the painting, RGBA8
    std::vector<float> depth;         // the block-out's depth seen from cam, row-major
};
// Angular reach of each projector in radians.
std::vector<double> reaches(const std::vector<const Projector*>& set, const PaintOptions& o);
V3 sampleRgb(const Projector& p, double x, double y, double scale = 255);

struct Coverage {
    double px{0}, py{0}, f{0};
    double margin{0};   // the smallest distance of a decision quantity to its threshold (tie reports)
};
// std::nullopt when not covered; `margin` (when given) receives the decision margin either way.
std::optional<Coverage> covered(const Projector& p, const V3& P, const V3& N, const PaintOptions& o, double* margin = nullptr);

struct PaintShade {
    bool covered{false};
    V3 rgb{};
    int hit{0};
    double margin{1e30};   // the smallest decision margin over every projector
};
PaintShade shadePoint(const std::vector<const Projector*>& set, const std::vector<double>& reach, const V3& P, const V3& N,
                      const PaintCamera& view, const PaintOptions& o);

}  // namespace raw::style
