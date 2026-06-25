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
                             const Mat4& prevViewProj, Arena* arena){
    FrameResult r(arena);
    r.g = rasterize(scene, w, h, arena);
    LinearAccel accel; accel.build(scene, arena);
    r.aoRT = computeRTAO(r.g, accel, 64, 2.0f, arena);
    r.aoSS = computeSSAO(r.g, 24, 2.0f, arena);
    r.rec  = reconcile(r.aoSS, r.aoRT, r.g.mask, 0.12f, arena);
    r.frame = shade(r.g, r.aoRT, scene, arena);     // human view: clamped [0,1]
    r.hdr   = shadeHDR(r.g, r.aoRT, scene, arena);  // model view: unclamped HDR

    // Motion vectors by reprojection against the previous view-projection.
    r.viewProj = mul(scene.camera.proj(), scene.camera.view());
    r.motionValid = computeMotion(r.g, r.viewProj, prevViewProj);
    int covered = 0;
    for (auto m : r.g.mask.px) covered += m;
    r.motionTotal = covered;
    return r;
}
}
