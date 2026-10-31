// The GPU path tracer's energy checks, interface v4 (docs/rt-interface-v4.md): e2 the white-plane
// furnace and e3 the closed-box furnace, both at the default settings, each with its control.
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/renderer/rt_pt_checks.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
namespace raw::gpu_check {

void rtPtEnergy(rhi::Device& dev, const std::function<void(const std::string&, bool)>& record, std::string& error) {
    struct Case { const char* what; rt::PtMaterial m; bool legacy; };
    const Case cases[] = {
        {"default dielectric", rt::PtMaterial{}, false},
        {"dielectric specular 1 roughness 0.1", {1.0f, 0.1f, 0.0f, 1.0f, {}}, false},
        {"metal roughness 1", {1.0f, 1.0f, 1.0f, 0.5f, {}}, false},
        {"default dielectric", rt::PtMaterial{}, true},
        {"metal roughness 1", {1.0f, 1.0f, 1.0f, 0.5f, {}}, true},
    };
    for (const Case& k : cases) {
        const rt::PtScene s = rt::ptWhitePlane(k.m, {1.0f, 1.0f, 1.0f});
        rt::PathTraceDesc d;
        d.width = 64; d.height = 48; d.spp = 64; d.seed = 105;
        d.camera = d.previous = rt::whitePlaneCamera();
        d.controls.legacyLobeV2 = k.legacy;
        const rt::PathTraceOutput o = pathTraceGpu(dev, s, d);
        if (!o.error.empty()) { error = o.error; return; }
        const rt::PlaneFurnace f = rt::planeFurnace(o);
        record(rt::planeFurnaceJson(f, k.what, "gpu", k.legacy), k.legacy ? !f.within() : f.within());
    }
    // e3: the closed box at the default PathTraceDesc; the control cuts at 8 bounces.
    for (const auto& [a, control] : {std::pair<float, bool>{0.8f, false}, {0.9f, false}, {0.8f, true}}) {
        const rt::PtScene s = rt::ptFurnace(1.0f, a);
        rt::PathTraceDesc d;
        d.width = 64; d.height = 48; d.spp = 64; d.seed = 107;
        if (control) d.maxBounces = 8;
        d.camera = d.previous = rt::cameraOf(rt::furnaceBox());
        const rt::PathTraceOutput o = pathTraceGpu(dev, s, d);
        if (!o.error.empty()) { error = o.error; return; }
        const double expect = 1.0 / (1.0 - a);
        double sum = 0, var = 0, worst = 0;
        for (std::size_t p = 0; p < o.depth.size(); ++p) {
            const double l = 0.2126 * o.radiance[p * 3] + 0.7152 * o.radiance[p * 3 + 1] + 0.0722 * o.radiance[p * 3 + 2];
            sum += l; var += o.variance[p];
            worst = std::max(worst, std::fabs(l - expect) / std::sqrt(std::max(1e-20, double(o.variance[p]))));
        }
        const double n = double(o.depth.size()), mean = sum / n, z = std::fabs(mean - expect) / (std::sqrt(var) / n);
        const bool ok = z <= 4.0;   // the worst pixel is reported: roulette gives single pixels a heavy tail
        char b[400];
        std::snprintf(b, sizeof b, "{\"check\": \"e3\", \"side\": \"gpu\", \"albedo\": %.1f, \"max_bounces\": %u, \"control\": %s, \"mean\": %.6f, \"expected\": %.6f, "
                      "\"z\": %.3f, \"worst_pixel_z\": %.3f, \"within\": %s}",
                      a, d.maxBounces, control ? "true" : "false", mean, expect, z, worst, ok ? "true" : "false");
        record(b, control ? !ok : ok);
    }
}

}  // namespace raw::gpu_check
