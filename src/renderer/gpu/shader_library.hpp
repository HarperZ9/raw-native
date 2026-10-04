#pragma once
// The compiled code of the renderer's GPU passes. A build carries one shader
// format, the one its RHI backend takes: WGSL text for WebGPU (embedded from
// shaders/*.wgsl by cmake/gpu.cmake) or DXIL for D3D12 (compiled from
// shaders/hlsl/ by DXC at build time). A build without a GPU backend carries
// none. Each format is one source file, chosen by cmake/gpu.cmake.
#include "raw/rhi/rhi.hpp"
namespace raw::gpu_shaders {
// The code of pass `name` in `format`; bytes is null when this build does not
// carry it.
rhi::ShaderCode find(const char* name, rhi::ShaderFormat format);
}
