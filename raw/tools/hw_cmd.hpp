#pragma once
// raw_native_cli hw-probe: the hardware capability probe and, with --checks, H1.0's
// functional checks and timestamp linearity (raw/rhi/hw.hpp; bounds in
// evidence/hw-h1-0-bounds.json).
//   hw-probe [--checks] [--out FILE]
// JSON on stdout (and in FILE). Exit 0: probe only, or every check within its bound;
// 1: a check outside its bound; 2: bad arguments; 4: no GPU backend or adapter.
#include <string>
namespace raw::rhi { class Device; }
namespace raw {
struct HwReport { std::string json; bool pass{false}; };
// The probe, the functional checks, the timing and its control, as one evidence record.
HwReport hwChecks(rhi::Device& dev);
int hwProbeCommand(int argc, char** argv);
}
