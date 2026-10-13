#pragma once
// raw_native_cli hw-mesh: HW H1.3, a visibility buffer by amplification and mesh shaders with
// meshlet frustum and cone culling, against vertex pulling, with the flipped-cone control and
// timings (evidence/hw-h1-3-bounds.json).
//   hw-mesh [--warmup N] [--repeats N] [--out FILE]
// JSON on stdout (and in FILE). Exit 0 within the bounds, 1 a bound missed, 2 bad arguments,
// 4 no GPU backend or adapter.
#include <string>
namespace raw::rhi { class Device; }
namespace raw {
struct HwMeshReport { std::string json; bool pass{false}; };
HwMeshReport hwMesh(rhi::Device& dev, int warmup, int repeats);
int hwMeshCommand(int argc, char** argv);
}
