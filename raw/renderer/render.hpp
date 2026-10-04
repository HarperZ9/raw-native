#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/scene/scene.hpp"
#include "raw/cert/reconcile.hpp"
#include "raw/math/mat.hpp"
#include "raw/core/arena.hpp"
namespace raw {
// One rendered frame plus every extra channel a program can read beyond the
// human-visible 8-bit frame. All buffers are arena-backed when `arena` is set.
struct FrameResult {
    GBuffer g;                  // includes the motion plane
    Buffer<float> aoRT, aoSS;   // ray-traced truth, screen-space approximation
    ReconcileResult rec;        // AO reconcile verdict
    Buffer<Vec3> frame;         // displayable, clamped [0,1] (human view)
    Buffer<Vec3> hdr;           // linear radiance, UNCLAMPED (model view)
    int motionValid{0};         // count of valid motion vectors
    int motionTotal{0};         // count of covered pixels (motion denominator)
    Mat4 viewProj{};            // the view-projection used this frame (for reprojection)
    FrameResult() = default;
    explicit FrameResult(Arena* a)
        : g(a), aoRT(a), aoSS(a), frame(a), hdr(a) {}
};
// Fixed sampling of the two AO estimators. Recorded in every raw-cert/2
// certificate so a checker knows what the numbers were computed from.
inline constexpr int   kRtSamples = 64;   // ray-traced reference, hemisphere samples per pixel
inline constexpr int   kSsSamples = 24;   // screen-space shortcut, samples per pixel
inline constexpr float kAoRadius  = 2.0f; // world-space occlusion radius for both
// Per-render options that are not camera parameters.
//   tolerance: RMSE bound for the AO verdict (recorded in the certificate).
//   rtao:      false skips the ray-traced reference. The frame is then shaded
//              with the screen-space AO and the reconcile has no data, so the
//              certificate verdict is "unverifiable". Used for raster-only timing.
//   threads:   render threads for the AO passes. Output is identical for any count.
struct RenderOptions {
    float tolerance{0.12f};
    bool  rtao{true};
    int   threads{1};
};
// Render `scene` at w*h and fill every channel. `prevViewProj` is the previous
// frame's view-projection for motion reprojection; pass the current view-projection
// (the default produced when prevViewProj == identity is handled by the caller)
// for a static frame -> zero motion. The two-way loop feeds last frame's
// viewProj here to get real screen-space velocity.
FrameResult renderWithParams(const Scene& scene, int w, int h,
                             const Mat4& prevViewProj, Arena* arena = nullptr,
                             const RenderOptions& opts = {});
}
