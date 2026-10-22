#pragma once
// Split radiance cascades for diffuse indirect light (ROADMAP proposal P6; the design, the bounds
// and the checks are in evidence/p6-radiance-cascades-bounds.json, written before this code).
//
// Lighting model 3 = lighting model 2 + albedo x I, where I is the cosine-weighted mean radiance
// arriving from surfaces. Per frame: one cosine ray from each receiver pixel; at its first hit
// one sun ray and one bounce ray feed a world-space irradiance cache (multiple bounces through
// the cache, one more a frame); after warm-up each receiver ray is split by hit distance into four
// cascades of probes (fine cells with coarse angles near, coarse cells with fine angles far);
// the cascades merge from coarse to fine, cascade 0 is projected onto spherical harmonics, and
// every receiver gathers I from the eight probes around it.
//
// The CPU reference: geometry queries through the PLOC tree (float32, the arithmetic the GPU
// repeats), everything after the hit in float64. Sums are fixed point, so thread order does not
// change a bit. src/renderer/gpu/shaders/gi.wgsl is the GPU form.
#include "raw/math/primitives.hpp"
#include "raw/renderer/gbuffer.hpp"
#include "raw/renderer/rt_bvh.hpp"
#include "raw/scene/scene.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::gi {

inline constexpr int kCascades = 4;
inline constexpr double kFixed = 4096.0;            // fixed-point scale of every sum
inline constexpr std::uint32_t kNone = 0xFFFFFFFFu;  // no key, no slot

struct Params {
    float cacheCell{0.25f};                        // bounce cache cell (m)
    float cell0{0.25f};                            // cascade 0 cell; cascade c is cell0 x 2^c
    int bins0{8};                                  // octahedral bins a side at cascade 0; x 2^c
    float radius[kCascades + 1]{0.0f, 0.5f, 2.0f, 8.0f, 1e30f};   // interval c is [radius[c], radius[c+1])
    int frames{40}, warmup{8};                     // frames run, and frames before splitting starts
    int stride{1};                                 // one receiver ray per stride x stride block
    float sky{0.2f};                               // sky radiance (lighting model 2's ambient)
};

// The scene as the GI passes see it: triangles, per-vertex normals, per-triangle albedo, the
// tree, the sun and the box the dense tables cover.
struct Scene3 {
    std::vector<Tri> tris;
    std::vector<Vec3> nrm;                         // 3 a triangle
    std::vector<Vec3> albedo;                      // 1 a triangle
    rt::Tree tree;
    Vec3 toLight{0, 1, 0};
    float li{1.0f};
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};                 // the triangles' box, padded by 0.5 m
};
Scene3 sceneOf(const Scene& s);

// A dense table of (cell, normal bucket) keys over the box: key = ((z ny + y) nx + x) 6 + bucket.
struct Grid {
    Vec3 origin{0, 0, 0};
    float cell{1.0f};
    std::uint32_t nx{0}, ny{0}, nz{0};
    std::uint64_t keys() const { return std::uint64_t(nx) * ny * nz * 6u; }
    std::uint32_t key(Vec3 p, std::uint32_t bucket) const;
    Vec3 centre(std::uint32_t key) const;
};
Grid gridOf(const Scene3& s, float cell);
std::uint32_t bucketOf(Vec3 n);
// Octahedral bin of a unit direction on an n x n grid over the sphere, and the inverse of a point
// of the [-1, 1]^2 square (a unit direction).
std::uint32_t octBin(Vec3 d, std::uint32_t n);
Vec3 octDir(float u, float v);
// The cosine-weighted direction around n from two uniforms, exactly as src/renderer/ray_ao.cpp.
Vec3 cosineDir(Vec3 n, float u1, float u2);
// The deterministic hash of ray_ao.cpp, and the seed of purpose k (0 receiver, 1 bounce) at frame f.
float hash01(std::uint32_t x, std::uint32_t y, std::uint32_t s);
inline std::uint32_t seedOf(int frame, int k, int axis){ return std::uint32_t(2 * (frame * 2 + k) + axis); }

// Cascade c's probes: the receivers' occupied keys, in key order, and each key's slot.
struct Cascade {
    Grid grid;
    std::uint32_t bins{0};                         // a side; bins x bins directions
    std::vector<std::uint32_t> slotOfKey;          // kNone where unoccupied
    std::vector<std::uint32_t> keyOfSlot;
    std::vector<std::uint32_t> acc;                // slot, bin: enter, hits, r, g, b
    std::vector<double> merged;                    // slot, bin: r, g, b
};
std::vector<Cascade> probesOf(const Scene3& s, const GBuffer& g, const Params& p);

struct Result {
    std::vector<Vec3> indirect;                    // I a pixel (0 off the mask), float32 of the float64 value
    std::vector<double> indirect64;                // r, g, b a pixel
    std::uint64_t cacheEntries{0}, raysTraced{0};
    std::uint32_t slots[kCascades]{};
    std::string error;
};
// The reference, frames 0 .. p.frames - 1, for the receivers of g.
Result cascadesCpu(const Scene3& s, const GBuffer& g, const Params& p, int threads = 1);

// The model 3 frame: model 2's radiance plus albedo x I (HDR, unclamped).
Buffer<Vec3> shadeModel3(const GBuffer& g, const Buffer<float>& sky, const Buffer<float>& sun, const raw::Scene& s,
                         const std::vector<Vec3>& indirect);

}  // namespace raw::gi
