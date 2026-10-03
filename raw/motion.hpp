#pragma once
#include "raw/gbuffer.hpp"
#include "raw/mat.hpp"
#include "raw/scene.hpp"
namespace raw {
// motion-vector channel.
//
// Per-pixel screen-space velocity by REPROJECTION: take the world position
// already stored in the G-buffer position plane and project it with both the
// current and the previous view-projection matrix; the motion vector is the
// difference of the two clip-space UVs (current minus previous), in [0,1] UV
// units. This is the standard reprojection motion-vector method used for
// temporal anti-aliasing and temporal upsampling (e.g. Karis, "High Quality
// Temporal Supersampling", SIGGRAPH 2014; Nehab et al., "Accelerating Real-Time
// Shading with Reverse Reprojection Caching", Graphics Hardware 2007).
//
// A static camera (prevViewProj == currentViewProj) yields zero motion at every
// covered pixel. Background (unmasked) pixels report zero motion. Pixels whose
// world point falls behind either projection plane are left at zero and are NOT
// counted as valid by the coherence witness.

// Project a world point to clip-space UV in [0,1]; returns false if the point is
// behind the projection plane (clip w <= 0) so the caller can refuse to invent a
// velocity for it.
bool projectToUV(const Mat4& viewProj, Vec3 worldPos, Vec2& outUV);

// Fill g.motion in place from the world positions in g.position. currentViewProj
// must be the view-projection used to rasterize g. Returns the number of valid
// (finite, in-front-of-both-planes) motion vectors written.
int computeMotion(GBuffer& g, const Mat4& currentViewProj, const Mat4& prevViewProj);

// Convenience: derive currentViewProj from the scene camera, then compute motion.
int computeMotion(GBuffer& g, const Scene& scene, const Mat4& prevViewProj);
}
