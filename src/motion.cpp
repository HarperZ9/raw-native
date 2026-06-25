#include "raw/motion.hpp"
#include <cmath>
namespace raw {
// Project world point -> clip -> NDC -> [0,1] UV (matching the rasterizer's
// screen mapping: u = ndc.x*0.5+0.5, v = 1 - (ndc.y*0.5+0.5)). Returns false
// when the point is on or behind the projection plane.
bool projectToUV(const Mat4& vp, Vec3 wp, Vec2& outUV){
    Vec4 cs = mul(vp, Vec4{wp.x, wp.y, wp.z, 1.0f});
    if (cs.w <= 1e-6f) return false;
    float invw = 1.0f / cs.w;
    float ndcx = cs.x * invw;
    float ndcy = cs.y * invw;
    outUV.x = ndcx * 0.5f + 0.5f;
    outUV.y = 1.0f - (ndcy * 0.5f + 0.5f);
    return true;
}
int computeMotion(GBuffer& g, const Mat4& currentViewProj, const Mat4& prevViewProj){
    int valid = 0;
    for (int y = 0; y < g.h; ++y) for (int x = 0; x < g.w; ++x){
        g.motion.at(x,y) = Vec2{0.0f, 0.0f};
        if (!g.mask.at(x,y)) continue;
        Vec3 wp = g.position.at(x,y);
        Vec2 cur, prev;
        if (!projectToUV(currentViewProj, wp, cur)) continue;
        if (!projectToUV(prevViewProj,    wp, prev)) continue;
        Vec2 mv = cur - prev;            // current minus previous clip UV
        if (!std::isfinite(mv.x) || !std::isfinite(mv.y)) continue;
        g.motion.at(x,y) = mv;
        ++valid;
    }
    return valid;
}
int computeMotion(GBuffer& g, const Scene& scene, const Mat4& prevViewProj){
    Mat4 vp = mul(scene.camera.proj(), scene.camera.view());
    return computeMotion(g, vp, prevViewProj);
}
}
