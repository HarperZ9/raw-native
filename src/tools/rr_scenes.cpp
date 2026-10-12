// HW H2 dataset scenes and camera paths (rr_scenes.hpp). Every scene is owned: built in code by
// raw-native (RT's, M3's and HW's generators), lit here with a sun and a constant sky.
#include "rr_scenes.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "raw/tools/hw_scenes.hpp"
#include "raw/tools/owned_assets.hpp"
#include "raw/scene/scene.hpp"
#include <algorithm>
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
    return {"retro_room", "dense_block", "iso_street", "clip_stress", "raw_hall", "raw_hero", "builtin", "hall_b",
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

void materialAovs(const rt::PtScene& s, const std::vector<int>& tri, std::vector<float>& rough, std::vector<float>& metal){
    rough.assign(tri.size(), 0.0f); metal.assign(tri.size(), 0.0f);
    for (std::size_t p = 0; p < tri.size(); ++p){
        if (tri[p] < 0) continue;
        const rt::PtMaterial& m = s.materials[s.geo.triTexture[(std::size_t)tri[p]]];
        rough[p] = m.roughness; metal[p] = m.metallic;
    }
}
}  // namespace raw::rr
