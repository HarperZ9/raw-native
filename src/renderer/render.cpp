// The CPU reference renderer, the oracle every other path is checked against.
// It runs as a host frame graph (raw/graph/frame_graph.hpp): the same pass
// names and data flow as the GPU renderer's graph, executed on the CPU in
// declaration order.
#include "raw/renderer/render.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/renderer/accel.hpp"
#include "raw/renderer/ray_ao.hpp"
#include "raw/renderer/ssao.hpp"
#include "raw/renderer/composite.hpp"
#include "raw/renderer/motion.hpp"
#include "raw/graph/frame_graph.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
namespace raw {
namespace {
using rhi::Access;
struct Res { graph::Resource G, RT, SS, REC, FR, HDR; };
// The two AO estimators and their reconcile. Same allocation order as 0.2.0
// (accel, RT AO, SS AO), so the arena certificate for a default render is unchanged.
// The passes run after this function returns, so they capture the frame, the
// scene and the options by reference (the caller owns them) and the arena
// pointer by value.
void addAoPasses(graph::FrameGraph& g, const Res& x, FrameResult& r, const Scene& scene, int w, int h,
                 Arena* arena, const RenderOptions& opts, Buffer<float>& sky, Buffer<float>& sun){
    const Access R = Access::StorageRead, W = Access::StorageWrite;
    if (opts.rtao)
        g.addPass("rtao", {{x.G, R}, {x.RT, W}}, [&r, &scene, &opts, arena, &sky, &sun](graph::PassContext&){
            LinearAccel accel; accel.build(scene, arena);
            if (lightingModel(opts) == 2){
                const Vec3 toLight = (scene.lights.empty() ? Vec3{0,-1,0} : scene.lights[0].dir) * -1.0f;
                r.aoRT = Buffer<float>(arena);
                computeRTVisibility(r.g, accel, kRtSamples, kAoRadius, toLight, r.aoRT, sky, sun, opts.threads, lightingNoSun());
            } else {
                r.aoRT = computeRTAO(r.g, accel, kRtSamples, kAoRadius, arena, opts.threads);
            }
        });
    g.addPass("ssao", {{x.G, R}, {x.SS, W}}, [&r, &opts, arena](graph::PassContext&){
        r.aoSS = computeSSAO(r.g, kSsSamples, kAoRadius, arena, opts.threads);
    });
    if (opts.rtao)
        g.addPass("reconcile", {{x.SS, R}, {x.RT, R}, {x.G, R}, {x.REC, W}}, [&r, &opts, arena](graph::PassContext&){
            r.rec = reconcile(r.aoSS, r.aoRT, r.g.mask, opts.tolerance, arena);
        });
    else
        // No reference: an empty reconcile (pixels == 0) makes the verdict
        // "unverifiable" instead of comparing against invented data.
        g.addPass("reconcile", {{x.REC, W}}, [&r, arena, w, h](graph::PassContext&){
            r.rec.errorMap = Buffer<float>(arena);
            r.rec.errorMap.resize(w, h);
        });
}
}  // namespace

int lightingModel(const RenderOptions& o){
    if (!o.rtao) return 1;
    const char* v = std::getenv("RAW_NATIVE_LIGHTING");
    if (v && std::strcmp(v, "v1") == 0) return 1;
    if (v && (std::strcmp(v, "v2") == 0 || std::strcmp(v, "v2-nosun") == 0)) return 2;
    return o.lighting == 1 ? 1 : 2;
}
bool lightingNoSun(){
    const char* v = std::getenv("RAW_NATIVE_LIGHTING");
    return v && std::strcmp(v, "v2-nosun") == 0;
}
FrameResult renderWithParams(const Scene& scene, int w, int h,
                             const Mat4& prevViewProj, Arena* arena,
                             const RenderOptions& opts){
    FrameResult r(arena);
    graph::FrameGraph g;   // host graph: no device
    const Res x{g.createHost("gbuffer"), g.createHost("ao_rt"), g.createHost("ao_ss"),
                g.createHost("reconcile"), g.createHost("frame"), g.createHost("hdr")};
    const Access R = Access::StorageRead, W = Access::StorageWrite;
    g.addPass("raster", {{x.G, W}}, [&](graph::PassContext&){ r.g = rasterize(scene, w, h, arena); });
    Buffer<float> sky(arena), sun(arena);   // lighting model 2's visibility, filled by the rtao pass
    addAoPasses(g, x, r, scene, w, h, arena, opts, sky, sun);
    const graph::Resource lightRes = opts.rtao ? x.RT : x.SS;
    const Buffer<float>& lightAO = opts.rtao ? r.aoRT : r.aoSS;
    const bool model2 = lightingModel(opts) == 2;
    g.addPass("shade", {{x.G, R}, {lightRes, R}, {x.FR, W}}, [&](graph::PassContext&){
        r.frame = model2 ? shadeV2(r.g, sky, sun, scene, arena) : shade(r.g, lightAO, scene, arena);     // human view: clamped [0,1]
    });
    g.addPass("shade_hdr", {{x.G, R}, {lightRes, R}, {x.HDR, W}}, [&](graph::PassContext&){
        r.hdr = model2 ? shadeHDRV2(r.g, sky, sun, scene, arena) : shadeHDR(r.g, lightAO, scene, arena);    // model view: unclamped HDR
    });
    // Motion vectors by reprojection against the previous view-projection; they
    // are written into the G-buffer's motion plane.
    g.addPass("motion", {{x.G, W}}, [&](graph::PassContext&){
        r.viewProj = mul(scene.camera.proj(), scene.camera.view());
        r.motionValid = computeMotion(r.g, r.viewProj, prevViewProj);
        int covered = 0;
        for (auto m : r.g.mask.px) covered += m;
        r.motionTotal = covered;
    });
    for (graph::Resource out : {x.G, x.RT, x.SS, x.REC, x.FR, x.HDR}) g.markOutput(out);
    std::string err;
    if (!g.execute(err)){
        // The graph above is fixed at compile time; a failure is a bug in it.
        std::fprintf(stderr, "raw-native: CPU reference frame graph: %s\n", err.c_str());
        std::abort();
    }
    return r;
}
}
