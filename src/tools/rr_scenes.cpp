// HW H2 dataset scenes and camera paths (rr_scenes.hpp). Every scene is owned: built in code by
// raw-native (RT's, M3's and HW's generators), lit here with a sun and a constant sky.
#include "rr_scenes.hpp"
#include "raw/renderer/rt_bvh.hpp"
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/tools/hw_scenes.hpp"
#include "raw/tools/owned_assets.hpp"
#include "raw/scene/scene.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace raw::rr {
namespace {
constexpr float kPi = 3.14159265358979f;
const Vec3 kSun{3.0f, 2.85f, 2.6f}, kSky{0.35f, 0.42f, 0.55f};
std::uint32_t rgba(float r, float g, float b){
    auto c = [](float v){ return (std::uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | 0xff000000u;
}
// A material with a 1 x 1 base-colour texture; returns its texture id.
std::uint32_t addMaterial(rt::PtScene& s, Vec3 base, rt::PtMaterial m){
    const std::uint32_t id = s.tex.add(1, 1, {rgba(base.x, base.y, base.z)});
    if (s.materials.size() <= id) s.materials.resize(id + 1);
    s.materials[id] = m;
    return id;
}
// Triangles with flat normals, one material id each.
void addTris(rt::PtScene& s, const std::vector<Tri>& tris, const std::vector<std::uint32_t>& mat){
    for (std::size_t i = 0; i < tris.size(); ++i){
        const Tri& t = tris[i];
        Vec3 n = cross(t.b - t.a, t.c - t.a);
        const float l = length(n);
        n = l > 0 ? n * (1.0f / l) : Vec3{0, 1, 0};
        const auto base = (std::uint32_t)s.geo.pos.size();
        for (Vec3 p : {t.a, t.b, t.c}){ s.geo.pos.push_back(p); s.geo.nrm.push_back(n); s.geo.uv.push_back({0, 0}); }
        for (std::uint32_t k = 0; k < 3; ++k) s.geo.idx.push_back(base + k);
        s.geo.triTexture.push_back(mat[i]);
    }
}
struct Rng {
    std::uint64_t s;
    float uni(){ s = mix(s + 0x9E3779B97F4A7C15ull); return (float)((s >> 40) * (1.0 / 16777216.0)); }
    float range(float a, float b){ return a + (b - a) * uni(); }
};
// A seeded draw over M3's material families: rough dielectric, glossy dielectric, metal.
std::vector<std::uint32_t> family(rt::PtScene& s, Rng& r, int count){
    std::vector<std::uint32_t> ids;
    for (int i = 0; i < count; ++i){
        const int f = i % 3;
        const Vec3 base{r.range(0.15f, 0.85f), r.range(0.15f, 0.85f), r.range(0.15f, 0.85f)};
        rt::PtMaterial m;
        if (f == 0) m = {1.0f, r.range(0.6f, 1.0f), 0.0f, 0.5f, {}};
        else if (f == 1) m = {1.0f, r.range(0.2f, 0.45f), 0.0f, 1.0f, {}};
        else m = {1.0f, r.range(0.25f, 0.6f), 1.0f, 1.0f, {}};
        ids.push_back(addMaterial(s, base, m));
    }
    return ids;
}
// Materials per patch of `patch` consecutive triangles, drawn from the family.
std::vector<std::uint32_t> patches(std::size_t tris, const std::vector<std::uint32_t>& fam, std::size_t patch, std::uint64_t seed){
    std::vector<std::uint32_t> m(tris);
    for (std::size_t i = 0; i < tris; ++i) m[i] = fam[mix(seed ^ (i / patch)) % fam.size()];
    return m;
}
std::vector<Tri> terrain(std::uint64_t seed){
    Rng r{seed * 7919 + 1};
    const float p0 = r.range(0, 2 * kPi), p1 = r.range(0, 2 * kPi), f0 = r.range(0.5f, 0.9f), f1 = r.range(1.6f, 2.8f);
    auto h = [&](float x, float z){ return 1.5f * std::sin(f0 * x + p0) * std::cos(0.5f * z + p1) + 0.3f * std::sin(f1 * x + 1.1f * z + p1); };
    std::vector<Tri> t;
    constexpr int N = 160;
    auto p = [&](int i, int j){ const float x = -8 + 16.0f * i / N, z = -8 + 16.0f * j / N; return Vec3{x, h(x, z), z}; };
    for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i){
        t.push_back({p(i, j), p(i, j + 1), p(i + 1, j + 1)});
        t.push_back({p(i, j), p(i + 1, j + 1), p(i + 1, j)});
    }
    return t;
}
std::vector<Tri> debris(std::uint64_t seed){
    Rng r{seed * 104729 + 3};
    std::vector<Tri> t;
    auto sph = [&](){ const float z = r.range(-1, 1), ph = r.range(0, 2 * kPi), s = std::sqrt(std::fmax(0.0f, 1 - z * z)); return Vec3{s * std::cos(ph), s * std::sin(ph), z}; };
    for (int i = 0; i < 30000; ++i){
        const Vec3 a{r.range(-2, 2), r.range(-2, 2), r.range(-2, 2)};
        t.push_back({a, a + sph() * r.range(0.02f, 0.2f), a + sph() * r.range(0.02f, 0.2f)});
    }
    t.push_back({{-6, -2.2f, -6}, {6, -2.2f, 6}, {6, -2.2f, -6}});   // a ground under the cloud
    t.push_back({{-6, -2.2f, -6}, {-6, -2.2f, 6}, {6, -2.2f, 6}});
    return t;
}
// cloister (validation only; proposed in evidence/rr-diag-raw-hall-2026-10-15.json): a closed interior, densely tessellated (about 0.25-unit triangles), with
// two rows of columns, ceiling ribs and four high windows in one long wall; the sun reaches the floor only through
// them, so most of the room is lit indirectly. Built for the denoiser's validation split; not a variant of raw-hall.
void quadGrid(std::vector<Tri>& t, Vec3 o, Vec3 du, Vec3 dv, int nu, int nv, const std::vector<std::array<float, 4>>& holes = {}){
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i){
        const float u0 = (float)i / nu, v0 = (float)j / nv, u1 = (float)(i + 1) / nu, v1 = (float)(j + 1) / nv;
        bool hole = false;
        for (const auto& h : holes) hole = hole || (u0 >= h[0] && u1 <= h[1] && v0 >= h[2] && v1 <= h[3]);
        if (hole) continue;
        const Vec3 a = o + du * u0 + dv * v0, b = o + du * u1 + dv * v0, c = o + du * u1 + dv * v1, d = o + du * u0 + dv * v1;
        t.push_back({a, b, c});
        t.push_back({a, c, d});
    }
}
void cylinder(std::vector<Tri>& t, Vec3 base, float r, float h, int seg, int rings){
    for (int j = 0; j < rings; ++j) for (int i = 0; i < seg; ++i){
        const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg, y0 = h * j / rings, y1 = h * (j + 1) / rings;
        const Vec3 p00 = base + Vec3{r * std::cos(a0), y0, r * std::sin(a0)}, p10 = base + Vec3{r * std::cos(a1), y0, r * std::sin(a1)};
        const Vec3 p01 = base + Vec3{r * std::cos(a0), y1, r * std::sin(a0)}, p11 = base + Vec3{r * std::cos(a1), y1, r * std::sin(a1)};
        t.push_back({p00, p11, p10});
        t.push_back({p00, p01, p11});
    }
}
const Vec3 kCloisterLamp{70.0f, 66.0f, 58.0f};   // lamp radiance (calibrated by scratch renders of the input, not of any metric)
std::vector<Tri> cloister(){
    std::vector<Tri> t;
    const float X0 = -12, X1 = 12, H = 9, Z0 = -6, Z1 = 6;
    quadGrid(t, {X0, 0, Z0}, {X1 - X0, 0, 0}, {0, 0, Z1 - Z0}, 96, 48);                       // floor
    quadGrid(t, {X0, H, Z0}, {0, 0, Z1 - Z0}, {X1 - X0, 0, 0}, 48, 96);                       // ceiling
    quadGrid(t, {X0, 0, Z0}, {0, H, 0}, {X1 - X0, 0, 0}, 36, 96);                             // the window-less long wall
    std::vector<std::array<float, 4>> windows;                                                // u along x, v along y
    for (int w = 0; w < 4; ++w){ const float c = 0.2f + 0.2f * w; windows.push_back({c - 0.03125f, c + 0.03125f, 0.7222f, 0.9444f}); }
    quadGrid(t, {X0, 0, Z1}, {X1 - X0, 0, 0}, {0, H, 0}, 96, 36, windows);                   // the long wall with four windows
    quadGrid(t, {X0, 0, Z0}, {0, 0, Z1 - Z0}, {0, H, 0}, 48, 36);                             // end walls
    quadGrid(t, {X1, 0, Z0}, {0, H, 0}, {0, 0, Z1 - Z0}, 36, 48);
    for (int c = 0; c < 8; ++c) for (float z : {-3.0f, 3.0f}) cylinder(t, {-6.0f + 2.3f * c, 0, z}, 0.35f, H, 24, 36);   // columns
    for (int k = 0; k < 9; ++k){                                                              // ribs under the ceiling
        const float x = X0 + 1.5f + 2.6f * k;
        quadGrid(t, {x - 0.15f, H - 0.6f, Z0}, {0.3f, 0, 0}, {0, 0, Z1 - Z0}, 2, 48);
        quadGrid(t, {x - 0.15f, H - 0.6f, Z0}, {0, 0, Z1 - Z0}, {0, 0.6f, 0}, 48, 3);
        quadGrid(t, {x + 0.15f, H - 0.6f, Z0}, {0, 0.6f, 0}, {0, 0, Z1 - Z0}, 3, 48);
    }
    return t;
}
// True when the camera is sealed inside geometry: a 32 x 18, 4 spp path-traced probe from it receives no light at all
// (the symptom of hall_b's frames 56 to 61, whose references are black).
bool sealed(const rt::PtScene& s, const rt::PtCamera& c){
    rt::PathTraceDesc d;
    d.width = 32; d.height = 18; d.spp = 4; d.seed = 1; d.camera = d.previous = c;
    const rt::PathTraceOutput o = rt::pathTraceCpu(s, d, 1);
    for (float v : o.radiance) if (v > 0.0f) return false;
    return true;
}
// Meshes of the core scene type, one material each (mats[i] for mesh i).
void addMeshes(rt::PtScene& s, const Scene& src, const std::vector<std::uint32_t>& mats){
    for (std::size_t mi = 0; mi < src.meshes.size(); ++mi){
        const Mesh& m = src.meshes[mi];
        const auto base = (std::uint32_t)s.geo.pos.size();
        for (std::size_t v = 0; v < m.positions.size(); ++v){
            s.geo.pos.push_back(m.positions[v]);
            s.geo.nrm.push_back(v < m.normals.size() ? m.normals[v] : Vec3{0, 1, 0});
            s.geo.uv.push_back({0, 0});
        }
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3){
            for (int k = 0; k < 3; ++k) s.geo.idx.push_back(base + (std::uint32_t)m.indices[i + k]);
            s.geo.triTexture.push_back(mats[mi]);
        }
    }
}
rt::PtMaterial fromPbr(const pbr::Material& m){
    const float e = (float)m.emissiveStrength;
    return {1.0f, (float)std::clamp(m.roughness, 0.05, 1.0), (float)m.metallic, (float)std::clamp(m.specular, 0.0, 1.0),
            {(float)m.emissive.r * e, (float)m.emissive.g * e, (float)m.emissive.b * e}};
}
Vec3 rgbOf(const pbr::Rgb& c){ return {(float)c.r, (float)c.g, (float)c.b}; }
rt::PtCamera camOf(const Camera& c){ return {c.eye, c.center, c.up, c.fovy}; }
void outdoor(rt::PtScene& s, Vec3 sunDir){ s.sunDir = sunDir; s.sunIrradiance = kSun; s.sky = kSky; }
// raw-hall and raw-hero (M3's owned assets) with their materials as the path tracer models them.
bool owned(const std::string& name, rt::PtScene& s, rt::PtCamera& cam){
    if (name == "raw_hall"){
        const owned::Asset a = owned::hall();
        const Scene sc = owned::hallScene(a);
        std::vector<std::uint32_t> mats;
        for (const auto& p : a.parts) mats.push_back(addMaterial(s, rgbOf(p.material.baseColor), fromPbr(p.material)));
        addMeshes(s, sc, mats);
        cam = camOf(sc.camera);
        outdoor(s, normalize(Vec3{-0.55f, 0.45f, -0.35f}));
        return true;
    }
    if (name == "raw_hero"){
        const owned::Asset a = owned::hero();
        const Scene sc = owned::inTestScene(a, 480, 270);
        std::vector<std::uint32_t> mats;
        mats.push_back(addMaterial(s, {0.7f, 0.7f, 0.7f}, {1.0f, 0.8f, 0.0f, 0.5f, {}}));   // the test scene's ground
        for (const auto& p : a.parts) mats.push_back(addMaterial(s, rgbOf(p.material.baseColor), fromPbr(p.material)));
        addMeshes(s, sc, mats);
        cam = {{2.2f, 1.7f, 3.0f}, {0.0f, 0.9f, 0.0f}, {0, 1, 0}, 0.9f};   // closer than the test scene's camera
        outdoor(s, normalize(Vec3{0.4f, 1.0f, 0.3f}));
        return true;
    }
    return false;
}
}  // namespace

std::uint64_t mix(std::uint64_t z){
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31);
}

std::vector<std::string> knownScenes(){
    return {"retro_room", "dense_block", "iso_street", "clip_stress", "raw_hall", "raw_hero", "builtin", "hall_b", "cloister",
            "terrain_<seed>", "debris_<seed>"};
}

bool scene(const std::string& name, rt::PtScene& s, rt::PtCamera& cam, std::string& lighting){
    s = {};
    s.name = name;
    if (name == "retro_room"){ s = rt::ptRetroRoom(); cam = rt::cameraOf(swr::retroRoom()); lighting = "RT's own"; return true; }
    if (name == "iso_street"){ s = rt::ptIsoStreet(); cam = rt::cameraOf(swr::isoStreet()); lighting = "RT's own"; return true; }
    if (name == "dense_block"){ s = rt::ptDenseBlock(); cam = rt::cameraOf(rt::denseBlock()); lighting = "RT's own"; return true; }
    lighting = "sun irradiance (3, 2.85, 2.6), sky radiance (0.35, 0.42, 0.55)";
    if (name == "clip_stress"){
        // RT's material table comes with its lit scenes; clip_stress uses the same procedural textures.
        const swr::Scene base = swr::clipStress();
        const rt::PtScene ref = rt::ptIsoStreet();
        s.geo = base.geo; s.tex = base.tex;
        s.materials.assign(ref.materials.begin(), ref.materials.begin() + swr::kTexCount);
        outdoor(s, base.light);
        cam = rt::cameraOf(base);
    } else if (owned(name, s, cam)){
    } else if (name == "builtin"){
        const Scene sc = buildTestScene(480, 270);
        std::vector<std::uint32_t> mats;
        for (const Mesh& m : sc.meshes) mats.push_back(addMaterial(s, m.material.albedo, {1.0f, 0.6f, 0.0f, 0.5f, {}}));
        addMeshes(s, sc, mats);
        cam = camOf(sc.camera);
        outdoor(s, normalize(Vec3{0.4f, 1.0f, 0.3f}));
    } else if (name == "hall_b"){
        const std::vector<Tri> t = hwScenes(0x5EEDu, true)[1].tris;
        Rng r{11};
        addTris(s, t, patches(t.size(), family(s, r, 6), 64, 11));
        cam = {{-9, 3, -5}, {5, 1.5f, 3}, {0, 1, 0}, 1.0f};
        outdoor(s, normalize(Vec3{0.3f, 1.0f, 0.2f}));
    } else if (name == "cloister"){
        const std::vector<Tri> t = cloister();
        Rng r{0xC10157E7u};
        addTris(s, t, patches(t.size(), family(s, r, 9), 96, 0xC10157E7u));
        // Eight hanging lamps (small emissive cubes), so the room is lit mostly by emitters and their bounces.
        std::vector<Tri> lamps;
        for (int k = 0; k < 8; ++k){
            const Vec3 c{-9.0f + 2.6f * k, 6.2f, (k % 2) ? 1.2f : -1.2f};
            const float h = 0.18f;
            for (int ax = 0; ax < 3; ++ax) for (float sg : {-1.0f, 1.0f}){
                Vec3 n{0, 0, 0}, u{0, 0, 0}, v{0, 0, 0};
                (ax == 0 ? n.x : ax == 1 ? n.y : n.z) = sg * h;
                (ax == 0 ? u.y : ax == 1 ? u.z : u.x) = h;
                (ax == 0 ? v.z : ax == 1 ? v.x : v.y) = h;
                const Vec3 o = c + n;
                lamps.push_back({o - u - v, o + u - v, o + u + v});
                lamps.push_back({o - u - v, o + u + v, o - u + v});
            }
        }
        const std::uint32_t lampMat = addMaterial(s, {1, 1, 1}, {1.0f, 1.0f, 0.0f, 0.0f, kCloisterLamp});
        addTris(s, lamps, std::vector<std::uint32_t>(lamps.size(), lampMat));
        cam = {{-9.5f, 2.2f, -1.0f}, {8.0f, 3.2f, 1.5f}, {0, 1, 0}, 1.0f};
        outdoor(s, normalize(Vec3{0.25f, 0.55f, 0.8f}));   // through the windows of the z = 6 wall, steeply onto the floor
    } else if (name.rfind("terrain_", 0) == 0 || name.rfind("debris_", 0) == 0){
        const bool ter = name[0] == 't';
        const std::uint64_t seed = std::strtoull(name.c_str() + (ter ? 8 : 7), nullptr, 10);
        const std::vector<Tri> t = ter ? terrain(seed) : debris(seed);
        Rng r{seed * 31 + (ter ? 5u : 9u)};
        addTris(s, t, patches(t.size(), family(s, r, 6), ter ? 320 : 1, seed));
        cam = ter ? rt::PtCamera{{-9, 5, -9}, {0, 0, 0}, {0, 1, 0}, 1.0f} : rt::PtCamera{{0, 0.5f, -6}, {0, 0, 0}, {0, 1, 0}, 1.0f};
        outdoor(s, normalize(Vec3{0.4f, 0.8f, 0.45f}));
    } else return false;
    rt::finishPtScene(s);
    return true;
}

rt::PtCamera pathCamera(const rt::PtCamera& base, std::uint64_t pathSeed, int k, int n){
    const Vec3 fwd = base.target - base.eye;
    const float dist = length(fwd);
    const Vec3 f = fwd * (1.0f / dist), s = normalize(cross(f, base.up)), u = cross(s, f);
    const float ph = (float)(mix(pathSeed) % 6283) * 1e-3f, a = kPi * (float)k / (float)n;
    rt::PtCamera c = base;
    c.eye = base.eye + s * (0.12f * dist * std::sin(a + ph)) + u * (0.04f * dist * std::sin(2 * a + ph)) + f * (0.10f * dist * (float)k / (float)n);
    c.target = base.target + s * (0.06f * dist * std::sin(0.7f * a + ph));
    return c;
}

// On hall_b, a camera sealed inside geometry (a small path-traced probe receives no light) steps back
// along the view direction (0.1 a step, at most 60 steps) until it is not. hall_b's
// path otherwise passes inside geometry at frames 56 to 61 (black references); every other frame and scene is unchanged.
rt::PtCamera clearCamera(const std::string& name, const rt::PtScene& s, rt::PtCamera c){
    if (name != "hall_b") return c;
    const Vec3 back = normalize(c.eye - c.target) * 0.1f;
    for (int i = 0; i < 60 && sealed(s, c); ++i){ c.eye = c.eye + back; c.target = c.target + back; }
    return c;
}

void materialAovs(const rt::PtScene& s, const std::vector<int>& tri, std::vector<float>& rough, std::vector<float>& metal){
    rough.assign(tri.size(), 0.0f); metal.assign(tri.size(), 0.0f);
    for (std::size_t p = 0; p < tri.size(); ++p){
        if (tri[p] < 0) continue;
        const rt::PtMaterial& m = s.materials[s.geo.triTexture[(std::size_t)tri[p]]];
        rough[p] = m.roughness; metal[p] = m.metallic;
    }
}
}  // namespace raw::rr
