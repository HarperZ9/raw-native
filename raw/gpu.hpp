#pragma once
#include "raw/render.hpp"
#include "raw/gpu_reconcile.hpp"
#include <string>
namespace raw {
// The WebGPU backend. Compiled only into the raw-native-gpu WebAssembly target
// (CMake option RAW_NATIVE_GPU); the default native build has no GPU code and
// gpuCompiled() is false there.
//
// renderGpu runs triangle setup, rasterization, motion, screen-space AO,
// ray-traced AO and shading as WGSL compute passes, reads every channel back,
// and fills a FrameResult the CPU writers and certificates accept unchanged.
// The AO reconcile (SS versus RT) is computed on the host from the read-back
// buffers with the same reconcile() the CPU path uses.
bool gpuCompiled();
// Create the device once. Returns false with a reason when no adapter or device
// is available (no navigator.gpu, blocked adapter, lost device).
bool gpuInit(GpuAdapterInfo& info, std::string& err);
// Render on the GPU. Returns false with a reason on any WebGPU error; the frame
// is then left empty and no certificate may claim a GPU result. frameOnly reads
// back only the shaded frame (for timing a display path); every other channel,
// the reconcile and the motion counts are then left empty.
bool renderGpu(const Scene& scene, int w, int h, const Mat4& prevViewProj,
               const RenderOptions& opts, FrameResult& out, std::string& err, bool frameOnly = false);
}
