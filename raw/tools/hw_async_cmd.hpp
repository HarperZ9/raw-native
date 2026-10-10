#pragma once
// raw_native_cli hw-async: HW H1.4, serial against async-compute schedules, the wrong-wait
// control and the overlap timing (evidence/hw-h1-4-bounds.json).
//   hw-async [--n N] [--warmup N] [--repeats N] [--out FILE]
// JSON on stdout (and in FILE). Exit 0 within the bounds, 1 a bound missed, 2 bad arguments,
// 4 no GPU backend or adapter.
#include <cstdint>
#include <string>
namespace raw::rhi { class Device; }
namespace raw {
struct HwAsyncReport { std::string json; bool pass{false}; };
HwAsyncReport hwAsync(rhi::Device& dev, uint32_t n, int warmup, int repeats);
int hwAsyncCommand(int argc, char** argv);
}
