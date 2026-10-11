// The compute path tracer on the linked RHI backend (RT stage R2; bounds in
// evidence/rt-r2-bounds.json). Without an adapter it exits 77 (skipped, never passed).
// CI runs it on WARP with RAW_NATIVE_D3D12_WARP=1, where 77 fails the step.
//   rt_pt_parity [out.json]
#include "raw/renderer/rt_pt_gpu.hpp"
#include <cstdio>
#include <fstream>
#include <string>
int main(int argc, char** argv) {
    std::string err;
    raw::rhi::Device* dev = raw::rhi::device(err);
    if (!dev) { std::printf("skipped: %s\n", err.c_str()); return 77; }
    const raw::gpu_check::RtPtParity r = raw::gpu_check::rtPtParity(*dev);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
