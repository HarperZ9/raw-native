#pragma once
// raw_native_cli hw-pt: the path tracer on hardware ray queries (HW H2.0, docs/hw-rt.md,
// bounds in evidence/hw-h2-pt-bounds.json).
//   hw-pt contract [--quick] [--out FILE]      Q1, Q2, C1 to C3 and the offset sweep; JSON
//   hw-pt render [--scene S] [--width W] [--height H] [--spp N] [--seed S] [--compute] [--out DIR]
//   hw-pt time [--scene S] [--width W] [--height H] [--spp N]   hardware against compute, GPU timestamps
// Exit 0 within every bound (contract) or written, 1 a bound missed, 2 bad arguments, 4 no GPU backend.
// Without DXR tier 1.1 (or with RAW_NATIVE_HW_DISABLE=ray_query) render and time take the compute
// tracer and say so; the contract reports the fallback and fails Q3 only if no fallback was taken.
#include <string>
namespace raw::rhi { class Device; }
namespace raw {
struct HwPtReport { std::string json; bool pass{false}; };
HwPtReport hwPtContract(rhi::Device& dev, bool quick);
int hwPtCommand(int argc, char** argv);
}
