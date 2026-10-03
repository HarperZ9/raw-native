#include "raw/render.hpp"
#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/composite.hpp"
#include "raw/motion.hpp"
#include <utility>
namespace raw {
FrameResult renderWithParams(const Scene& scene, int w, int h,
                             const Mat4& prevViewProj, Arena* arena,
                             const RenderOptions& opts){
    FrameResult r(arena);
    r.g = rasterize(scene, w, h, arena);
    // Same allocation order as 0.2.0 (accel, RT AO, SS AO), so the arena
    // certificate for a default render is unchanged.
    if (opts.rtao){
        LinearAccel accel; accel.build(scene, arena);
        r.aoRT = computeRTAO(r.g, accel, kRtSamples, kAoRadius, arena, opts.threads);
    }
    r.aoSS = computeSSAO(r.g, kSsSamples, kAoRadius, arena, opts.threads);
    if (opts.rtao){
        r.rec  = reconcile(r.aoSS, r.aoRT, r.g.mask, opts.tolerance, arena);
    } else {
        // No reference: an empty reconcile (pixels == 0) makes the verdict
        // "unverifiable" instead of comparing against invented data.
        r.rec.errorMap = Buffer<float>(arena);
        r.rec.errorMap.resize(w, h);
    }
    const Buffer<float>& lightAO = opts.rtao ? r.aoRT : r.aoSS;
    r.frame = shade(r.g, lightAO, scene, arena);     // human view: clamped [0,1]
    r.hdr   = shadeHDR(r.g, lightAO, scene, arena);  // model view: unclamped HDR

    // Motion vectors by reprojection against the previous view-projection.
    r.viewProj = mul(scene.camera.proj(), scene.camera.view());
    r.motionValid = computeMotion(r.g, r.viewProj, prevViewProj);
    int covered = 0;
    for (auto m : r.g.mask.px) covered += m;
    r.motionTotal = covered;
    return r;
}
}
