#include "raw/renderer/raster.hpp"
#include "raw/math/mat.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw {
namespace {
// One clip-space vertex with the attributes the G-buffer interpolates.
struct ClipVert { Vec4 cs; Vec3 wp, wn; };
ClipVert lerpVert(const ClipVert& a, const ClipVert& b, float t) {
    const auto l3 = [t](Vec3 x, Vec3 y) { return Vec3{x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t}; };
    return {{a.cs.x + (b.cs.x - a.cs.x) * t, a.cs.y + (b.cs.y - a.cs.y) * t, a.cs.z + (b.cs.z - a.cs.z) * t, a.cs.w + (b.cs.w - a.cs.w) * t},
            l3(a.wp, b.wp), l3(a.wn, b.wn)};
}
// Sutherland-Hodgman against the plane w = wNear: up to 4 vertices, every one with w >= wNear.
// Attributes are linear in clip space, so the cut is exact before the perspective divide.
int clipNear(const ClipVert in[3], float wNear, ClipVert out[4]) {
    int n = 0;
    for (int k = 0; k < 3; ++k) {
        const ClipVert& a = in[k];
        const ClipVert& b = in[(k + 1) % 3];
        const bool ain = a.cs.w >= wNear, bin = b.cs.w >= wNear;
        if (ain) out[n++] = a;
        if (ain != bin) out[n++] = lerpVert(a, b, (wNear - a.cs.w) / (b.cs.w - a.cs.w));
    }
    return n;
}
struct Target { GBuffer& g; int w, h; Buffer<uint16_t>* ids; const RasterOptions& opt; };
// Rasterize one triangle whose three vertices all have w > 0.
void drawTri(Target& T, const ClipVert v[3], const Material& mat, size_t mi, uint32_t triIndex) {
    const int w = T.w, h = T.h;
    const RasterOptions& opt = T.opt;
    GBuffer& g = T.g;
    Vec3 wp[3] = {v[0].wp, v[1].wp, v[2].wp}, wn[3] = {v[0].wn, v[1].wn, v[2].wn};
    Vec3 ndc[3]; float invw[3];
    for (int k = 0; k < 3; ++k) {
        invw[k] = 1.0f / v[k].cs.w;
        ndc[k] = {v[k].cs.x * invw[k], v[k].cs.y * invw[k], v[k].cs.z * invw[k]};
    }
    // to screen
    Vec3 sp[3];
    for (int k = 0; k < 3; ++k) {
        sp[k].x = (ndc[k].x * 0.5f + 0.5f) * w;
        sp[k].y = (1.0f - (ndc[k].y * 0.5f + 0.5f)) * h;
        sp[k].z = v[k].cs.w;   // view-space distance proxy (-z_view = w)
        sp[k].x += opt.jitterX; sp[k].y += opt.jitterY;
        if (opt.subpixelBits > 0) {
            const float q = float(1 << opt.subpixelBits);
            sp[k].x = std::nearbyint(sp[k].x * q) / q; sp[k].y = std::nearbyint(sp[k].y * q) / q;
        }
    }
    int minx = std::max(0, (int)std::floor(std::min({sp[0].x, sp[1].x, sp[2].x})));
    int maxx = std::min(w - 1, (int)std::ceil(std::max({sp[0].x, sp[1].x, sp[2].x})));
    int miny = std::max(0, (int)std::floor(std::min({sp[0].y, sp[1].y, sp[2].y})));
    int maxy = std::min(h - 1, (int)std::ceil(std::max({sp[0].y, sp[1].y, sp[2].y})));
    float area = (sp[1].x - sp[0].x) * (sp[2].y - sp[0].y) - (sp[1].y - sp[0].y) * (sp[2].x - sp[0].x);
    if (std::fabs(area) < 1e-9f) return;
    for (int y = miny; y <= maxy; ++y) for (int x = minx; x <= maxx; ++x) {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((sp[1].x - px) * (sp[2].y - py) - (sp[1].y - py) * (sp[2].x - px)) / area;
        float w1 = ((sp[2].x - px) * (sp[0].y - py) - (sp[2].y - py) * (sp[0].x - px)) / area;
        float w2 = 1.0f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        // perspective-correct interpolation
        float iw = w0 * invw[0] + w1 * invw[1] + w2 * invw[2];
        float depth = opt.perspectiveDepth ? 1.0f / iw : (w0 * sp[0].z + w1 * sp[1].z + w2 * sp[2].z); // screen-linear interpolation of view distance (correct for non-overlapping test scene; true depth-buffer would interpolate 1/w-deferred)
        if (depth >= g.depth.at(x, y)) continue;
        auto pc = [&](Vec3 a, Vec3 b, Vec3 c) {
            Vec3 r{(w0 * a.x * invw[0] + w1 * b.x * invw[1] + w2 * c.x * invw[2]) / iw,
                   (w0 * a.y * invw[0] + w1 * b.y * invw[1] + w2 * c.y * invw[2]) / iw,
                   (w0 * a.z * invw[0] + w1 * b.z * invw[1] + w2 * c.z * invw[2]) / iw}; return r; };
        g.depth.at(x, y) = depth;
        g.position.at(x, y) = pc(wp[0], wp[1], wp[2]);
        g.normal.at(x, y) = normalize(pc(wn[0], wn[1], wn[2]));
        g.albedo.at(x, y) = mat.albedo;
        g.mask.at(x, y) = 1;
        if (T.ids) T.ids->at(x, y) = uint16_t(mi + 1);
        if (opt.triangleIds) opt.triangleIds->at(x, y) = triIndex + 1;
    }
}
}  // namespace

GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena, Buffer<uint16_t>* ids){
    return rasterize(scene, w, h, arena, ids, RasterOptions{});
}
// A triangle with every vertex at w > 1e-6 is drawn as it is. One with a vertex at or behind
// that is clipped at the near plane (w = nearZ, -z_view = nearZ) and drawn as one or two
// triangles; before evidence/m3-post-bounds.json method note 13 such triangles were dropped,
// which let a surface behind them show through (the raw-hall gallery's slab top).
GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena, Buffer<uint16_t>* ids, const RasterOptions& opt){
    GBuffer g(arena); g.resize(w,h);
    if (ids) ids->resize(w,h);
    if (opt.triangleIds) opt.triangleIds->resize(w,h);
    uint32_t triIndex = 0;
    for (auto& d : g.depth.px) d = std::numeric_limits<float>::infinity();
    Mat4 view = scene.camera.view();
    Mat4 proj = scene.camera.proj();
    Mat4 vp = mul(proj, view);
    Target T{g, w, h, ids, opt};
    for (size_t mi=0; mi<scene.meshes.size(); ++mi){
        const Mesh& m = scene.meshes[mi];
        for (size_t i=0; i+2 < m.indices.size(); i+=3, ++triIndex){
            const int idx[3] = {m.indices[i], m.indices[i+1], m.indices[i+2]};
            ClipVert v[3];
            bool behind = false;
            for (int k=0;k<3;++k){
                const Vec3 p = m.positions[idx[k]];
                v[k] = {mul(vp, Vec4{p.x,p.y,p.z,1}), p, m.normals[idx[k]]};
                behind = behind || v[k].cs.w <= 1e-6f;
            }
            if (!behind) { drawTri(T, v, m.material, mi, triIndex); continue; }
            ClipVert c[4];
            const int n = clipNear(v, scene.camera.nearZ, c);
            for (int k = 1; k + 1 < n; ++k) {
                const ClipVert t[3] = {c[0], c[k], c[k + 1]};
                drawTri(T, t, m.material, mi, triIndex);
            }
        }
    }
    return g;
}
}
