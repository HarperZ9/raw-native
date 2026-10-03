#pragma once
#include "raw/gbuffer.hpp"
#include "raw/scene.hpp"
#include "raw/reconcile.hpp"
#include "raw/mat.hpp"
#include "raw/arena.hpp"
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
// Render `scene` at w*h and fill every channel. `prevViewProj` is the previous
// frame's view-projection for motion reprojection; pass the current view-projection
// (the default produced when prevViewProj == identity is handled by the caller)
// for a static frame -> zero motion. The two-way loop feeds last frame's
// viewProj here to get real screen-space velocity.
FrameResult renderWithParams(const Scene& scene, int w, int h,
                             const Mat4& prevViewProj, Arena* arena = nullptr);
}
