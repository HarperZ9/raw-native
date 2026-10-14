#pragma once
// raw_native_cli hw-rayquery: HW H1.1, inline ray query against the CPU BVH on the procedural
// scenes, with the offset and rotation controls (evidence/hw-h1-1-bounds.json).
//   hw-rayquery [--repeats N] [--warmup N] [--criterion b] [--out FILE]
// --criterion b: H1.1b (evidence/hw-h1-1b-bounds.json), the hall without coincident faces, conditioned
// tolerances and the coplanar control.
// JSON on stdout (and in FILE). Exit 0 within every bound, 1 a bound missed, 2 bad arguments,
// 4 no GPU backend or adapter.
#include <string>
namespace raw::rhi { class Device; }
namespace raw {
struct HwRayQueryReport { std::string json; bool pass{false}; };
HwRayQueryReport hwRayQuery(rhi::Device& dev, int warmup, int repeats, bool criterionB = false);
int hwRayQueryCommand(int argc, char** argv);
}
