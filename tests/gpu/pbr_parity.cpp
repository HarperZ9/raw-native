// The material model's GPU parity on the linked RHI backend (M3; bounds in
// evidence/m3-materials-bounds.json). Without an adapter it exits 77, which CTest reports
// as skipped with the reason, never as passed. CI runs it on WARP with
// RAW_NATIVE_D3D12_WARP=1, where 77 fails the step.
//   pbr_parity [out.json]
#include "raw/renderer/pbr_parity.hpp"
#include <cstdio>
#include <fstream>
#include <string>
int main(int argc, char** argv) {
    std::string err;
    raw::rhi::Device* dev = raw::rhi::device(err);
    if (!dev) { std::printf("skipped: %s\n", err.c_str()); return 77; }
    const raw::gpu_check::PbrParity r = raw::gpu_check::pbrParity(*dev);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
