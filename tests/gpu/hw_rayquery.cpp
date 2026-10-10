// HW H1.1 on the linked backend (bounds in evidence/hw-h1-1-bounds.json): inline ray query
// against the CPU BVH with the offset and rotation controls. Without an adapter it exits 77.
//   hw_rayquery [out.json]
#include "raw/rhi/rhi.hpp"
#include "raw/tools/hw_rayquery_cmd.hpp"
#include <cstdio>
#include <fstream>
int main(int argc, char** argv){
    std::string err;
    raw::rhi::Device* dev = raw::rhi::device(err);
    if (!dev){ std::printf("skipped: %s\n", err.c_str()); return 77; }
    const raw::HwRayQueryReport r = raw::hwRayQuery(*dev, 0, 0);
    std::fputs(r.json.c_str(), stdout);
    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}
