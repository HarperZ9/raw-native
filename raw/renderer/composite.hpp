#pragma once
#include "raw/renderer/gbuffer.hpp"
#include "raw/scene/scene.hpp"
#include "raw/core/arena.hpp"
namespace raw {
// Displayable shade: linear radiance clamped to [0,1] per channel (the human's
// 8-bit-bound view). Preserved unchanged for backward compatibility.
Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s, Arena* arena = nullptr);

// HDR channel: the SAME lighting as shade() but with NO tonemap and NO
// clamp. Values may exceed 1.0. This is the highest-information visual channel:
// the model receives exact linear radiance, while the human sees the tonemapped
// 8-bit frame. Background (unmasked) pixels are {0,0,0}.
Buffer<Vec3> shadeHDR(const GBuffer& g, const Buffer<float>& ao, const Scene& s, Arena* arena = nullptr);

// Reinhard tonemap of a single linear radiance value: L_d = L / (1 + L).
// Monotonic on [0, inf), maps 0 -> 0, and maps any finite L >= 0 into [0,1).
float tonemapReinhard(float L);

// Reinhard tonemap of a linear-radiance buffer into a displayable [0,1] buffer,
// applied per channel. The input HDR buffer is read-only and left intact.
Buffer<Vec3> tonemapReinhard(const Buffer<Vec3>& hdr, Arena* arena = nullptr);

// Maximum linear radiance over any channel of any covered pixel (the HDR
// headroom indicator). Returns 0 for an empty/background-only frame.
float maxRadiance(const Buffer<Vec3>& hdr);
}
