// raw_native_cli gpu-timestamps: check t1 of evidence/gpu-timestamps-bounds.json on the linked
// backend: two timestamps around an empty stretch, and the material gallery's shading dispatch timed
// by timestamps against its submit-to-fence time. Exit 0 when the checks hold, 1 when one fails,
// 77 when there is no device; a backend without timestamps reports so and exits 1.
#include "raw/renderer/gallery.hpp"
#include "raw/renderer/lighting_parity.hpp"
#include "raw/rhi/rhi.hpp"
#include <cstdio>
#include <string>
#include <vector>
namespace raw {

int timestampsCommand(int, char**) {
    std::string err;
    rhi::Device* dev = rhi::device(err);
    if (!dev) { std::printf("gpu-timestamps: skipped: %s\n", err.c_str()); return 77; }
    bool ok = true;
    // An empty stretch.
    rhi::CommandList* c = dev->begin(err);
    if (!c) { std::printf("gpu-timestamps: %s\n", err.c_str()); return 1; }
    const bool rec = c->timestamp(0) && c->timestamp(1);
    if (!dev->submitAndWait(err)) { std::printf("gpu-timestamps: %s\n", err.c_str()); return 1; }
    std::vector<double> s;
    std::string terr;
    const bool read = rec && dev->timestampSeconds(2, s, terr);
    const double empty = read ? s[1] - s[0] : -1.0;
    const bool emptyOk = read && empty >= 0.0 && empty < 1.0;
    ok = ok && emptyOk;
    // The gallery's shading dispatch: timestamps against submit-to-fence, per chunk medians summed.
    const gallery::Gallery g = gallery::build(96, 96, 1);
    std::vector<float> out;
    gpu_check::DispatchTiming tm;
    const bool shaded = gpu_check::shadeOnGpu(*dev, g.grid, g.samples, g.lights, g.env, g.sh, g.exposure, out, err, &tm, 9);
    const bool dispOk = shaded && tm.gpuMs >= 0.0 && tm.gpuMs <= tm.ms && tm.gpuMs < 1000.0;
    ok = ok && dispOk;
    std::printf("{\n \"check\": \"gpu timestamps (t1)\",\n \"backend\": \"%s\",\n \"adapter\": \"%s\",\n"
                " \"empty_interval_s\": %.9g, \"empty_ok\": %s,\n \"dispatch_gpu_ms\": %.4f, \"dispatch_wall_ms\": %.4f, \"dispatch_ok\": %s,\n"
                " \"error\": \"%s\",\n \"pass\": %s\n}\n",
                dev->backendName(), dev->adapter().description.c_str(), empty, emptyOk ? "true" : "false", tm.gpuMs, tm.ms, dispOk ? "true" : "false",
                (!terr.empty() ? terr : (!tm.gpuError.empty() ? tm.gpuError : err)).c_str(), ok ? "true" : "false");
    return ok ? 0 : 1;
}
}  // namespace raw
