#pragma once
// Tolerances for the GPU-versus-CPU reconcile (raw-gpu-cert/1).
//
// Committed on 2026-10-03 before any GPU backend code existed in this
// repository and before any GPU output was seen. Changing a value after a GPU
// run is a change to the claim and must say so in its commit message.
//
// Reasoning, written in advance:
// - The GPU runs the same algorithms on the same float32 inputs. Differences
//   come from shader-compiler rewrites (fused multiply-add, relaxed sqrt, sin
//   and cos precision), which move values by a few ulps and can flip a pixel
//   centre that lies exactly on a triangle edge or a ray that grazes an edge.
// - A flipped hemisphere ray moves ray-traced AO by 1/64 = 0.0156 at that
//   pixel; a flipped screen-space sample moves SSAO by about 1/24. If one pixel
//   in twenty has one flipped ray, RT AO RMSE is about 0.0035. The bound of
//   0.01 allows that with margin and still catches a wrong algorithm, which
//   moves AO RMSE by 0.05 or more.
// - Coverage may differ only on silhouette edges: at most 0.2% of the pixels
//   covered by either side.
// - Max errors are reported and never bounded: one flipped pixel can be off by
//   a full step, and a bound on it would only measure luck.
namespace raw::gpu_tolerance {
inline constexpr double kMaskMismatchFraction = 0.002;  // of pixels covered by either side
inline constexpr double kDepthRelRmse   = 1e-4;   // RMSE of |gpu-cpu| / cpu depth
inline constexpr double kPositionRmse   = 1e-3;   // world units, per component
inline constexpr double kNormalRmse     = 1e-3;   // per component
inline constexpr double kMotionRmse     = 1e-4;   // UV units, per component
inline constexpr double kAoSsRmse       = 0.01;   // SSAO, 0..1
inline constexpr double kAoRtRmse       = 0.01;   // ray-traced AO, 0..1
inline constexpr double kFrameRmse      = 0.01;   // clamped RGB, 0..1, per component
inline constexpr double kReconcileRmseDelta = 0.005;  // |gpu SS-vs-RT RMSE - cpu SS-vs-RT RMSE|
// The GPU frame's own AO verdict (verified or refuted) must equal the CPU's.
inline constexpr bool   kVerdictMustMatch = true;
}
