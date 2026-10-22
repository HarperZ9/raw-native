// raw_native_cli noise-parity: the noise module on the GPU against the CPU goldens
// (evidence/p8-noise-gpu-bounds.json g1 to g3). Exit 0 when every check holds, 1 when one fails,
// 77 when there is no device.
#include "raw/renderer/noise.hpp"
#include "raw/renderer/noise_gpu.hpp"
#include "raw/tools/noise_gallery.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace raw {
namespace {
// The goldens in noiseGoldens order: GPU function, scale, slice. Seed 7 throughout.
struct Case { const char* name; std::uint32_t func; float scale, z; bool exact; };
constexpr Case kCases[] = {
    {"white", 0, 1.0f, 0.0f, true}, {"value2", 1, 1.0f / 16.0f, 0.0f, false}, {"value3", 2, 1.0f / 16.0f, 0.37f, false},
    {"perlin2", 3, 1.0f / 16.0f, 0.0f, false}, {"perlin3", 4, 1.0f / 16.0f, 0.37f, false}, {"simplex2", 5, 1.0f / 16.0f, 0.0f, false},
    {"simplex3", 6, 1.0f / 16.0f, 0.37f, false}, {"worley2_f1", 7, 1.0f / 16.0f, 0.0f, false}, {"worley2_f2_minus_f1", 8, 1.0f / 16.0f, 0.0f, false},
    {"worley3_f1", 9, 1.0f / 16.0f, 0.37f, false}, {"fbm_perlin", 10, 1.0f / 32.0f, 0.5f, false}, {"ridged_simplex", 11, 1.0f / 32.0f, 0.5f, false},
    {"billow_perlin", 12, 1.0f / 32.0f, 0.5f, false}, {"hybrid_simplex", 13, 1.0f / 32.0f, 0.5f, false}, {"warp_fbm", 14, 1.0f / 32.0f, 0.5f, false},
    {"blue", 15, 1.0f, 0.0f, true},
};
}  // namespace

int noiseParityCommand(int, char**) {
    std::string err;
    rhi::Device* dev = rhi::device(err);
    if (!dev) { std::printf("noise-parity: skipped: %s\n", err.c_str()); return 77; }
    const std::vector<GreyImage> cpu = noiseGoldens();
    bool ok = true;
    std::string rows;
    for (const Case& c : kCases) {
        const GreyImage* ref = nullptr;
        for (const GreyImage& g : cpu) if (g.name == c.name) ref = &g;
        if (!ref) { std::printf("noise-parity: no golden named %s\n", c.name); return 1; }
        gpu_check::NoiseGrid grid; grid.func = c.func; grid.scale = c.scale; grid.z = c.z;
        std::vector<float> gpu, ctl;
        if (!gpu_check::noiseOnGpu(*dev, grid, gpu, err)) { std::printf("noise-parity: %s\n", err.c_str()); return 1; }
        grid.seed = 8;   // g2: the control, seed off by one
        if (!gpu_check::noiseOnGpu(*dev, grid, ctl, err)) { std::printf("noise-parity: %s\n", err.c_str()); return 1; }
        double worst = 0.0, ctlWorst = 0.0;
        bool same = true;
        for (std::size_t i = 0; i < gpu.size(); ++i) {
            worst = std::max(worst, std::fabs(double(gpu[i]) - ref->px[i]));
            ctlWorst = std::max(ctlWorst, std::fabs(double(ctl[i]) - ref->px[i]));
            same = same && std::memcmp(&gpu[i], &ref->px[i], 4) == 0;
        }
        const bool g1 = c.exact ? same : worst <= 1e-5;
        const bool g2 = c.func == 15 || ctlWorst > 0.01;
        ok = ok && g1 && g2;
        char b[320];
        std::snprintf(b, sizeof b, "%s\n  {\"name\": \"%s\", \"worst_abs\": %.3e, \"bit_equal\": %s, \"g1\": %s, \"control_worst_abs\": %.3e, \"g2\": %s}",
                      rows.empty() ? "" : ",", c.name, worst, same ? "true" : "false", g1 ? "true" : "false", ctlWorst, g2 ? "true" : "false");
        rows += b;
    }
    // g3: the hash chain over 65,536 lattice points, through the white-noise function (24 bits a point).
    gpu_check::NoiseGrid w; w.w = w.h = 256; w.func = 0; w.scale = 1.0f;
    std::vector<float> gw;
    if (!gpu_check::noiseOnGpu(*dev, w, gw, err)) { std::printf("noise-parity: %s\n", err.c_str()); return 1; }
    long diff = 0;
    for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
        const float cpuv = noise::white(x, y, 0, 7u), gv = gw[std::size_t(y) * 256 + x];
        diff += std::memcmp(&cpuv, &gv, 4) != 0;
    }
    ok = ok && diff == 0;
    std::printf("{\n \"check\": \"noise module gpu parity (P8 stage 2)\",\n \"backend\": \"%s\",\n \"adapter\": \"%s\",\n \"functions\": [%s\n ],\n"
                " \"g3_white_points_differing\": %ld,\n \"pass\": %s\n}\n",
                dev->backendName(), dev->adapter().description.c_str(), rows.c_str(), diff, ok ? "true" : "false");
    return ok ? 0 : 1;
}
}  // namespace raw
