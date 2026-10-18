// The raster scene as the path tracer's scene: see raw/renderer/rt_pt_convert.hpp.
#include "raw/renderer/rt_pt_convert.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace raw::rt {
namespace {
std::uint32_t byteOf(float v){ return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }
}

PtScene ptFromScene(const Scene& s, float ambient, bool sunPi){
    PtScene p;
    p.name = "raster scene";
    for (const Mesh& m : s.meshes){
        const Vec3 a = m.material.albedo;
        const std::uint32_t tex = p.tex.add(1, 1, {byteOf(a.x) | (byteOf(a.y) << 8) | (byteOf(a.z) << 16) | (255u << 24)});
        p.materials.resize(std::size_t(tex) + 1);
        p.materials[tex] = {1.0f, 1.0f, 0.0f, 0.0f, {0, 0, 0}};   // pure Lambert: specular 0, metallic 0
        const std::uint32_t base = std::uint32_t(p.geo.pos.size());
        for (std::size_t v = 0; v < m.positions.size(); ++v){
            p.geo.pos.push_back(m.positions[v]);
            p.geo.nrm.push_back(v < m.normals.size() ? m.normals[v] : Vec3{0, 1, 0});
            p.geo.uv.push_back({0.5f, 0.5f});
        }
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3){
            for (int k = 0; k < 3; ++k) p.geo.idx.push_back(base + std::uint32_t(m.indices[i + std::size_t(k)]));
            p.geo.triTexture.push_back(tex);
        }
    }
    const Vec3 dir = s.lights.empty() ? Vec3{0, -1, 0} : s.lights[0].dir;
    const float li = s.lights.empty() ? 1.0f : s.lights[0].intensity;
    const float e = li * (sunPi ? 3.14159265f : 1.0f);
    p.sunDir = dir * -1.0f;
    p.sunIrradiance = {e, e, e};
    p.sky = {ambient, ambient, ambient};
    finishPtScene(p);
    return p;
}
PtCamera ptCameraOf(const Camera& c){ return {c.eye, c.center, c.up, c.fovy}; }

}  // namespace raw::rt
