// HW H1.0 on the linked backend (bounds in evidence/hw-h1-0-bounds.json): the probe, the
// functional checks behind it, and timestamp linearity with its control. Without an
// adapter it exits 77 (skipped, never passed). CI runs it on WARP.
//   hw_probe [out.json]
#include "raw/rhi/rhi.hpp"
#include "raw/tools/hw_cmd.hpp"
#include <cstdio>
#include <fstream>
int main(int argc, char** argv){
    std::string err;
    raw::rhi::Device* dev = raw::rhi::device(err);
    if (!dev){ std::printf("skipped: %s\n", err.c_str()); return 77; }
    const raw::HwReport r = raw::hwChecks(*dev);
    std::fputs(r.json.c_str(), stdout);
    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << r.json;
    return r.pass ? 0 : 1;
}
