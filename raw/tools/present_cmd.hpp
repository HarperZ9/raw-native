#pragma once
// raw_native_present: the native presentation host (N2, evidence/n2-presentation-bounds.json).
// Built with RAW_NATIVE_GPU_D3D12=ON on Windows (cmake/present.cmake).
//   raw_native_present run      --out F [--frames N] [--width W] [--height H] [--offscreen]
//                               [--lifecycle] [--control] [--vsync on|off] [--no-readback]
//                               [--fullscreen-check] [--hidden] [--dump F.ppm]
//   raw_native_present hdr      --out F
//   raw_native_present pacing   --out F [--frames N] [--warmup N] [--fullscreen] [--vsync on|off]
//   raw_native_present selftest
// Exit codes: 0 done (and every self-check held), 1 a self-check failed, 2 usage,
// 3 a runtime error, 77 no adapter or no window (CTest's skip).
namespace raw {
int presentCommand(int argc, char** argv);
}
