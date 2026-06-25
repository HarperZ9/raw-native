#include "raw/composite.hpp"
#include <algorithm>
namespace raw {
// Shared lighting kernel: linear radiance for one covered pixel, UNCLAMPED.
// shade() clamps this to [0,1]; shadeHDR() returns it verbatim. Keeping one
// kernel guarantees the human's tonemapped view and the model's HDR view come
// from identical lighting.
static Vec3 radianceAt(const GBuffer& g, const Buffer<float>& ao, int x, int y,
                       Vec3 ldir, float li){
    Vec3 n = g.normal.at(x,y);
    float ndl = std::max(0.0f, dot(n, ldir*-1.0f)) * li;
    float ambient = 0.2f;
    float a = ao.at(x,y);
    float lit = (ambient + ndl) * a;
    Vec3 alb = g.albedo.at(x,y);
    return { alb.x*lit, alb.y*lit, alb.z*lit };
}
Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s, Arena* arena){
    Buffer<Vec3> img(arena); img.resize(g.w,g.h);
    Vec3 ldir = s.lights.empty() ? Vec3{0,-1,0} : s.lights[0].dir;
    float li = s.lights.empty() ? 1.0f : s.lights[0].intensity;
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ img.at(x,y)={0,0,0}; continue; }
        Vec3 r = radianceAt(g, ao, x, y, ldir, li);
        img.at(x,y) = { std::clamp(r.x,0.0f,1.0f),
                        std::clamp(r.y,0.0f,1.0f),
                        std::clamp(r.z,0.0f,1.0f) };
    }
    return img;
}
Buffer<Vec3> shadeHDR(const GBuffer& g, const Buffer<float>& ao, const Scene& s, Arena* arena){
    Buffer<Vec3> img(arena); img.resize(g.w,g.h);
    Vec3 ldir = s.lights.empty() ? Vec3{0,-1,0} : s.lights[0].dir;
    float li = s.lights.empty() ? 1.0f : s.lights[0].intensity;
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ img.at(x,y)={0,0,0}; continue; }
        img.at(x,y) = radianceAt(g, ao, x, y, ldir, li); // unclamped, may exceed 1.0
    }
    return img;
}
float tonemapReinhard(float L){
    // Reinhard et al., "Photographic Tone Reproduction for Digital Images",
    // SIGGRAPH 2002. L_d = L / (1 + L). Negative input is clamped to 0 first so
    // the result is well-defined and monotonic on [0, inf).
    if (L < 0.0f) L = 0.0f;
    return L / (1.0f + L);
}
Buffer<Vec3> tonemapReinhard(const Buffer<Vec3>& hdr, Arena* arena){
    Buffer<Vec3> out(arena); out.resize(hdr.w, hdr.h);
    for (int y=0;y<hdr.h;++y) for (int x=0;x<hdr.w;++x){
        const Vec3& c = hdr.at(x,y);
        out.at(x,y) = { tonemapReinhard(c.x), tonemapReinhard(c.y), tonemapReinhard(c.z) };
    }
    return out;
}
float maxRadiance(const Buffer<Vec3>& hdr){
    float m = 0.0f;
    for (const Vec3& c : hdr.px){
        m = std::max(m, std::max(c.x, std::max(c.y, c.z)));
    }
    return m;
}
}
