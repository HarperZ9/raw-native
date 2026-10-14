#pragma once
// raw_native_cli hw-wave16: HW H1.2, a full prefix sum with wave intrinsics against its
// group-shared form, and a bilateral filter in native fp16 against fp32, with controls and
// timings (evidence/hw-h1-2-bounds.json).
//   hw-wave16 [--warmup N] [--repeats N] [--out FILE]
// JSON on stdout (and in FILE). Exit 0 within the bounds, 1 a bound missed, 2 bad arguments,
// 4 no GPU backend or adapter.
#include <string>
namespace raw::rhi { class Device; }
namespace raw {
struct HwWave16Report { std::string json; bool pass{false}; };
HwWave16Report hwWave16(rhi::Device& dev, int warmup, int repeats);
int hwWave16Command(int argc, char** argv);
}
