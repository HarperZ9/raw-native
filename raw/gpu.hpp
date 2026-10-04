#pragma once
#include "raw/render.hpp"
#include "raw/gpu_reconcile.hpp"
#include <string>
namespace raw {
// The GPU backends. Two exist, and a build contains at most one:
//   webgpu  the raw-native-gpu WebAssembly target (CMake option RAW_NATIVE_GPU),
//           WGSL compute passes through the browser's WebGPU;
//   d3d12   the native Windows CLI with RAW_NATIVE_GPU_D3D12=ON, the same passes
//           translated to HLSL by scripts/wgsl_to_hlsl.py and compiled by DXC.
// The default build has no GPU code; gpuCompiled() is false there.
//
// renderGpu runs triangle setup, rasterization, motion, screen-space AO,
// ray-traced AO and shading as compute passes, reads every channel back,
// and fills a FrameResult the CPU writers and certificates accept unchanged.
// The AO reconcile (SS versus RT) is computed on the host from the read-back
// buffers with the same reconcile() the CPU path uses.
bool gpuCompiled();
// "webgpu", "d3d12", or "none" in a build without a GPU backend. Recorded as
// params.backend in every --gpu certificate.
const char* gpuBackendName();
// Create the device once. Returns false with a reason when no adapter or device
// is available (no navigator.gpu, no hardware D3D12 adapter, lost device).
bool gpuInit(GpuAdapterInfo& info, std::string& err);
// Render on the GPU. Returns false with a reason on any WebGPU error; the frame
// is then left empty and no certificate may claim a GPU result. frameOnly reads
// back only the shaded frame (for timing a display path); every other channel,
// the reconcile and the motion counts are then left empty.
bool renderGpu(const Scene& scene, int w, int h, const Mat4& prevViewProj,
               const RenderOptions& opts, FrameResult& out, std::string& err, bool frameOnly = false);
}
