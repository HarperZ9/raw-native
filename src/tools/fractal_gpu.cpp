// The GPU part of raw_native_cli fractal-dither (f5 parity, f6 cost): see raw/tools/fractal_cmd.hpp.
#include "raw/renderer/fractal_gpu.hpp"
#include "raw/rhi/rhi.hpp"
#include "raw/tools/fractal_cmd.hpp"
#include <cstdio>
namespace raw::fractal {
int gpuReport(bool cost, std::string& json) {
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev) {
        json = "{\"check\": \"fractal dither gpu\", \"skipped\": \"" + why + "\", \"pass\": false}\n";
        return 4;
    }
    const gpu_check::FractalGpu r = gpu_check::fractalGpu(*dev, cost);
    json = r.json();
    return r.pass() ? 0 : 1;
}
}  // namespace raw::fractal
