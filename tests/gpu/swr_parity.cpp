// The software rasterizer's GPU parity on the linked RHI backend (RT stage R1; bounds in
// evidence/rt-r1-bounds.json). Without an adapter it exits 77 (skipped, never passed).
// CI runs it on WARP with RAW_NATIVE_D3D12_WARP=1, where 77 fails the step.
//   swr_parity [out.json]
#include "raw/renderer/swr_parity.hpp"
#include <cstdio>
#include <fstream>
#include <string>
int main(int argc, char** argv) {
    std::string err;
    raw::rhi::Device* dev = raw::rhi::device(err);
    if (!dev) { std::printf("skipped: %s\n", err.c_str()); return 77; }
    const raw::gpu_check::SwrParity r = raw::gpu_check::swrParity(*dev);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
