// Lit owned scenes for the path tracer: see raw/renderer/rt_pathtrace.hpp.
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "swr_builder.hpp"
#include <cmath>
namespace raw::rt {
namespace {
// Materials of the procedural texture set (swr::Tex order), then any added textures.
std::vector<PtMaterial> defaults() {
    std::vector<PtMaterial> m(swr::kTexCount);
    m[swr::kBrick] = {1.0f, 0.9f, 0.0f, 0.4f, {}};
    m[swr::kTiles] = {1.0f, 0.35f, 0.0f, 1.0f, {}};
    m[swr::kPlanks] = {1.0f, 0.6f, 0.0f, 0.6f, {}};
    m[swr::kChecker] = {1.0f, 0.5f, 0.0f, 0.8f, {}};
    m[swr::kStone] = {1.0f, 0.8f, 0.0f, 0.5f, {}};
    m[swr::kPlaster] = {1.0f, 0.95f, 0.0f, 0.3f, {}};
    m[swr::kCloth] = {1.0f, 1.0f, 0.0f, 0.2f, {}};
    m[swr::kSkin] = {1.0f, 0.6f, 0.0f, 0.5f, {}};
    m[swr::kBrass] = {1.0f, 0.3f, 1.0f, 1.0f, {}};
    return m;
}
PtScene from(const swr::Scene& s) {
    PtScene p;
    p.name = s.name; p.geo = s.geo; p.tex = s.tex; p.materials = defaults();
    p.sunDir = s.light;
    return p;
}
}  // namespace

PtCamera cameraOf(const swr::Scene& s) { return {s.eye, s.target, s.up, s.fovy}; }

void finishPtScene(PtScene& s) {
    const std::vector<Tri> tris = trianglesOf(s.geo);
    s.tree = buildPloc(tris);
    s.emitters.clear(); s.emitterCdf.clear(); s.emitterArea = 0.0f;
    for (std::size_t t = 0; t < tris.size(); ++t) {
        const Vec3 e = s.materials[s.geo.triTexture[t]].emission;
        if (e.x <= 0.0f && e.y <= 0.0f && e.z <= 0.0f) continue;
        s.emitterArea += 0.5f * length(cross(tris[t].b - tris[t].a, tris[t].c - tris[t].a));
        s.emitters.push_back(std::uint32_t(t));
        s.emitterCdf.push_back(s.emitterArea);
    }
}

PtScene ptRetroRoom() {
    const swr::Scene base = swr::retroRoom();
    PtScene p = from(base);
    const std::uint32_t lamp = p.tex.add(4, 4, std::vector<std::uint32_t>(16, 0xffffffffu));
    p.materials.push_back({1.0f, 1.0f, 0.0f, 0.0f, {6.0f, 5.5f, 4.5f}});
    swr::Builder b{p.geo};
    b.quad({-1.0f, 3.45f, -8.0f}, {1.0f, 3.45f, -8.0f}, {1.0f, 3.45f, -6.0f}, {-1.0f, 3.45f, -6.0f}, 1, 1, lamp);   // facing down
    p.sunDir = {0.2f, 0.5f, 0.85f}; p.sunIrradiance = {3.0f, 2.8f, 2.5f}; p.sky = {0.25f, 0.3f, 0.4f};
    finishPtScene(p);
    return p;
}
PtScene ptIsoStreet() {
    PtScene p = from(swr::isoStreet());
    p.sunDir = {-0.5f, 0.8f, 0.3f}; p.sunIrradiance = {3.0f, 2.85f, 2.6f}; p.sky = {0.35f, 0.42f, 0.55f};
    finishPtScene(p);
    return p;
}
PtScene ptDenseBlock() {
    PtScene p = from(denseBlock());
    p.sunIrradiance = {3.0f, 2.85f, 2.6f}; p.sky = {0.35f, 0.42f, 0.55f};
    finishPtScene(p);
    return p;
}
PtScene ptFurnace(float emission, float albedo) {
    PtScene p = from(furnaceBox());
    p.materials.assign(1, {albedo, 1.0f, 0.0f, 0.0f, {emission, emission, emission}});
    finishPtScene(p);
    return p;
}

}  // namespace raw::rt
