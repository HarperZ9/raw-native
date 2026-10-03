#pragma once
#include "raw/cli_params.hpp"
#include <string>
namespace raw {
// raw_native_cli --gpu: render on the GPU backend (D3D12 or WebGPU), render the CPU reference
// for the same params, and write
//   <out>/            the GPU frame's files, certificate.json and channels.json
//   <out>/cpu/        the CPU reference's files, certificate.json and channels.json
//   <out>/gpu_certificate.json   raw-gpu-cert/1: the GPU frame against the CPU
// Both directories pass `raw_native_cli verify`. Returns 0 when both rendered,
// 2 when the output cannot be written, 4 when no GPU is available (the GPU
// certificate is still written, with verdict unverifiable and the reason).
int runGpu(const CliParams& p);
// --gpu --bench <runs>: one untimed warm-up render (pipeline compilation), then
// <runs> timed GPU renders including upload, every pass and the read-back of
// every channel (runs_ms), then <runs> renders that read back only the frame
// (frame_only_runs_ms). JSON in the shape of the CPU bench plus backend and adapter.
std::string benchGpuJson(const CliParams& p);
}
