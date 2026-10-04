#pragma once
// Host-side data layout shared by the GPU backends (WebGPU and D3D12): the
// triangle and parameter buffers the passes read, and the conversion of the
// read-back channels into a FrameResult. Both backends run the same passes on
// the same layout, so this code exists once.
#include "raw/renderer/render.hpp"
#include <cstdint>
#include <vector>
namespace raw::gpu_host {
// Triangles in scene order, 24 floats each: positions, normals, albedo.
std::vector<float> packTriangles(const Scene& scene);
// The Params struct of src/gpu/common.wgsl, 192 bytes: two matrices, light,
// misc, eight u32.
void packParams(float pf[48], const Scene& scene, const Mat4& vp, const Mat4& prevVP, int w, int h,
                uint32_t ntri, bool rtao);
// The nine read-back channels, in this order: depth, position, normal,
// albedo+mask, motion, SSAO, RT AO, frame, HDR. Element sizes in bytes.
inline constexpr int kChannels = 9;
inline constexpr uint32_t kChannelBytes[kChannels] = {4, 16, 16, 16, 16, 4, 4, 16, 16};
inline constexpr int kFrameChannel = 7;
// Bytes to read back for channel i; zero means skipped (frame-only mode, or RT
// AO without the ray-traced pass).
uint64_t readBytes(int i, int w, int h, bool rtao, bool frameOnly);
// Fill `r` from the read-back channels (f[i] null where readBytes(i) is zero),
// then compute the AO reconcile on the host with the CPU's reconcile().
void unpackFrame(FrameResult& r, const float* const f[kChannels], int w, int h, const Mat4& vp,
                 const RenderOptions& opts, bool frameOnly);
}
