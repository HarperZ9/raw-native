// motion-vector channel: reprojection screen-space velocity.
#include "raw/renderer/raster.hpp"
#include "raw/renderer/motion.hpp"
#include "raw/math/mat.hpp"
#include "check.hpp"
#include <cmath>
using namespace raw;
int main() {
    const int W = 64, H = 64;
    Scene s = buildTestScene(W, H);
    GBuffer g = rasterize(s, W, H);
    Mat4 curVP = mul(s.camera.proj(), s.camera.view());

    // (1) STATIC camera: prev == current -> motion is ~0 at every covered pixel,
    // and every covered pixel is a valid motion vector.
    int valid = computeMotion(g, curVP, /*prevViewProj=*/curVP);
    int covered = 0; for (auto m : g.mask.px) covered += m;
    CHECK(covered > 100);
    CHECK(valid == covered);                 // all covered pixels valid, none invented
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        if (!g.mask.at(x,y)) continue;
        Vec2 mv = g.motion.at(x,y);
        CHECK_NEAR(mv.x, 0.0, 1e-5);
        CHECK_NEAR(mv.y, 0.0, 1e-5);
    }
    // background reports zero motion (never invented)
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        if (g.mask.at(x,y)) continue;
        CHECK(g.motion.at(x,y).x == 0.0f && g.motion.at(x,y).y == 0.0f);
    }

    // (2) KNOWN RIGID CAMERA TRANSLATION: build a previous camera shifted along
    // world +x, then check a sample covered pixel's motion matches the analytic
    // reprojection (current clip UV minus previous clip UV) within tolerance.
    Scene prev = s;
    prev.camera.eye    = s.camera.eye    + Vec3{0.5f, 0.0f, 0.0f};
    prev.camera.center = s.camera.center + Vec3{0.5f, 0.0f, 0.0f};
    Mat4 prevVP = mul(prev.camera.proj(), prev.camera.view());
    computeMotion(g, curVP, prevVP);

    // pick the first covered pixel and reproject its stored world position by hand
    bool checked = false;
    for (int y=0;y<H && !checked;++y) for (int x=0;x<W;++x){
        if (!g.mask.at(x,y)) continue;
        Vec3 wp = g.position.at(x,y);
        Vec2 cur, pv;
        if (!projectToUV(curVP, wp, cur)) continue;
        if (!projectToUV(prevVP, wp, pv)) continue;
        Vec2 expected = cur - pv;
        Vec2 got = g.motion.at(x,y);
        CHECK_NEAR(got.x, expected.x, 1e-5);
        CHECK_NEAR(got.y, expected.y, 1e-5);
        // a real translation produces nonzero motion somewhere
        CHECK(length(expected) > 1e-6f || length(got) > 1e-6f);
        checked = true; break;
    }
    CHECK(checked);

    // (3) the translated view actually moved at least one pixel a measurable amount
    float maxMag = 0.0f;
    for (int y=0;y<H;++y) for (int x=0;x<W;++x){
        if (!g.mask.at(x,y)) continue;
        maxMag = std::max(maxMag, length(g.motion.at(x,y)));
    }
    CHECK(maxMag > 1e-4f);
    return raw_test_summary();
}
