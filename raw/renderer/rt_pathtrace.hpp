#pragma once
// raw-native's path tracer, interface version 1 (RT stage R2, evidence/rt-r2-bounds.json).
//
// Stable contract for its consumers: the hardware session (reference and training data for
// learned ray reconstruction) and the shader lab (stylisation from its buffers). Any change
// bumps kPathTraceVersion and is announced in the RT session's PROGRESS.md before it merges.
//
// Determinism: the same scene, tree, seed, spp, sppBegin and size give the same bytes on one
// backend. Random numbers are counter-based (PCG2D, Jarzynski and Olano, JCGT 2020) keyed by
// (seed, pixel, sample, dimension), so a frame can be rendered in spp slices that add up.
//
// Light transport: unidirectional; next-event estimation for the sun (a delta directional
// light) and for emissive triangles (area sampling), multiple importance sampling (power
// heuristic) between BSDF and emitter sampling; a constant sky on escape; Russian roulette
// after bounce 3. Materials: Lambert plus GGX (VNDF sampling, Heitz 2018), metal-roughness,
// one material per texture id; base colour from the nearest texel. Units: linear, relative.
//
// AOVs (addendum 1 of the bounds file):
//   radiance  RGB, the mean of all samples, unclamped
//   albedo    RGB, the mean over primary samples of the first hit's base colour (0 on a miss)
//   normal    XYZ world, the mean over primary samples of the first hit's shading normal
//   depth     the first hit's distance along the camera's forward axis, pixel-centre ray, 0 on a miss
//   motion    pixel offset (previous - current) of the pixel-centre hit under the previous camera
//   variance  the sample variance of radiance luminance (0.2126 R + 0.7152 G + 0.0722 B) / spp
//   triangle  the pixel-centre ray's first-hit triangle index in Geometry order, -1 on a miss
//             (added to v1 before its first merge; ids for outlines and checks)
#include "raw/renderer/rt_bvh.hpp"
#include "raw/renderer/swr.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::rt {

inline constexpr int kPathTraceVersion = 1;

struct PtMaterial {
    float baseFactor{1.0f};   // base colour = texel / 255 * baseFactor
    float roughness{0.8f};    // GGX alpha = roughness^2
    float metallic{0.0f};
    float specular{0.5f};     // scales the dielectric F0 of 0.04; 0 makes the material pure Lambert
    Vec3 emission{0, 0, 0};   // two-sided, radiance
};
struct PtScene {
    std::string name;
    swr::Geometry geo;
    swr::TextureSet tex;
    std::vector<PtMaterial> materials;        // one per texture id
    Vec3 sunDir{0.4f, 0.8f, 0.45f};           // towards the sun; normalised by the tracer
    Vec3 sunIrradiance{0, 0, 0};              // 0: no sun
    Vec3 sky{0, 0, 0};                        // constant radiance on escape
    Tree tree;                                // over trianglesOf(geo), built by buildPloc or the GPU
    // Emissive triangles and their area CDF (made by finishPtScene).
    std::vector<std::uint32_t> emitters;
    std::vector<float> emitterCdf;            // inclusive running area
    float emitterArea{0};
};
struct PtCamera { Vec3 eye{0, 0, 5}, target{0, 0, 0}, up{0, 1, 0}; float fovy{1.0f}; };

// Debug and control switches; all false for a normal render.
struct PtControls {
    bool dropLambertCosine{false};   // P1 control
    bool misWeightOne{false};        // P2 control: both strategies weighted 1 (double counting)
};

struct PathTraceDesc {
    std::uint64_t seed{1};
    std::uint32_t spp{16};
    std::uint32_t sppBegin{0};       // first sample index: slices [sppBegin, sppBegin + spp) add up to a longer render
    std::uint32_t maxBounces{8};
    int width{256}, height{256};
    PtCamera camera, previous;       // previous: for motion; equal to camera for a still frame
    bool jitter{true};               // sub-pixel jitter of primary samples
    bool russianRoulette{true};
    PtControls controls;
};

struct PathTraceOutput {
    int version{kPathTraceVersion};
    int width{0}, height{0};
    std::uint32_t spp{0};
    std::vector<float> radiance, albedo, normal;   // 3 a pixel
    std::vector<float> depth, variance;            // 1 a pixel
    std::vector<int> triangle;                     // 1 a pixel
    std::vector<float> motion;                     // 2 a pixel
    std::string error;
};

// Build the tree (CPU PLOC), the emitter list and its CDF.
void finishPtScene(PtScene& s);
// Owned scenes, lit: retro_room (a ceiling lamp and the sun through the open end), iso_street
// (sun and sky), dense_block (sun and sky), and the furnace box (emission E and albedo a on
// every face, pure Lambert, no sun, no sky).
PtScene ptRetroRoom();
PtScene ptIsoStreet();
PtScene ptDenseBlock();
PtScene ptFurnace(float emission, float albedo);
PtCamera cameraOf(const swr::Scene& s);

// The CPU reference, row-parallel; identical output for any thread count.
PathTraceOutput pathTraceCpu(const PtScene& s, const PathTraceDesc& d, int threads = 0);

}  // namespace raw::rt
