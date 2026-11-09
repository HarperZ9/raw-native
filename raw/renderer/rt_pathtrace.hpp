#pragma once
// raw-native's path tracer, interface version 4 (RT stage R2, evidence/rt-r2-bounds.json; v3,
// docs/rt-interface-v3.md and evidence/rt-v3-bounds.json; interface v4 in docs/rt-interface-v4.md and evidence/rt-v4-energy.json).
//
// Stable contract for its consumers: the hardware session (reference and training data for
// learned ray reconstruction) and the shader lab (stylisation from its buffers). Any change
// bumps kPathTraceVersion and is announced before it merges: in the RT session's PROGRESS.md
// when one is running, otherwise in a dated design note under docs/ (v3: docs/rt-interface-v3.md; v4: docs/rt-interface-v4.md).
// v4 moved the bytes of every scene the v2 lobe or the triangle test touches; v3 fields keep their meaning.
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
//
// Split radiance, added in v2 (addendum 5 of the bounds file; every v1 output is unchanged):
//   emission  RGB, light gathered on the primary ray's own segment: the first hit's emission or
//             the sky on a primary miss
//   diffuse   RGB, the rest of the radiance through the first hit's Lambert term: next-event
//             estimation there by fd cos, and every later contribution by the weight fd cos / pdf
//   specular  RGB, the same through the GGX term ((f - fd) cos); emission + diffuse + specular
//             equals radiance up to float rounding
//   diffuseAlbedo   RGB, the mean over primary samples of (1 - metallic) base, 0 on a miss
//   specularAlbedo  RGB, the mean over primary samples of F0 + (1 - F0)(1 - cos_o)^5 where the
//             specular lobe exists, else 0: a demodulation estimate, not a directional albedo
//   diffuseVariance, specularVariance  as variance, of each component's luminance
//
// Added in v3 (docs/rt-interface-v3.md; CPU only, the GPU tracer refuses a scene that sets any):
//   lights        punctual lights in KHR_lights_punctual units, by next-event estimation with a
//                 shadow ray (lighting::illuminance)
//   environment   a radiance cube read on escape in place of sky, importance-sampled by luminance
//                 and MIS-weighted (power heuristic) against BSDF sampling
//   fullMaterials the glTF model of raw/renderer/pbr.hpp (clearcoat, sheen, anisotropy,
//                 iridescence, transmission thin and volume), one per texture id, in float64;
//                 tangent normalize(cross(n, +Y)); volumes traced with Beer-Lambert inside
#include "raw/renderer/lighting.hpp"
#include "raw/renderer/rt_bvh.hpp"
#include "raw/renderer/swr.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace raw::rt {

inline constexpr int kPathTraceVersion = 4;

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
    // v3 (all empty in a v2 scene).
    std::vector<lighting::Light> lights;      // punctual, world space
    lighting::Cube environment;               // level 0 used; empty rgb: none (sky as in v2)
    std::vector<pbr::Material> fullMaterials; // one per texture id; empty: materials as in v2
    // Made by finishPtScene: the environment's texel CDF (luminance x solid angle) and the tables.
    std::vector<double> envCdf;
    double envTotal{0};
    std::shared_ptr<const pbr::Tables> tables;
};
// True when the scene sets any v3 field.
bool usesV3(const PtScene& s);
struct PtCamera { Vec3 eye{0, 0, 5}, target{0, 0, 0}, up{0, 1, 0}; float fovy{1.0f}; };

// Debug and control switches; all false for a normal render.
struct PtControls {
    bool dropLambertCosine{false};   // P1 control
    bool misWeightOne{false};        // P2 control: both strategies weighted 1 (double counting)
    bool aovDropSpecularIndirect{false};   // P5a and P5c control: specular loses its paths after the first hit
    bool aovEmissionToDiffuse{false};      // P5b control: primary emission and sky go to diffuse
    bool envMisWeightOne{false};           // v3 c3 control: both environment strategies weighted 1
    bool envNoNee{false};                  // v3 c4: the environment only on escape (unbiased, noisier)
    bool legacyLobeV2{false};              // v4 e1 and e2 control: interface v3's lobe (Lambert added with no (1 - F), single-scattering GGX)
};

struct PathTraceDesc {
    std::uint64_t seed{1};
    std::uint32_t spp{16};
    std::uint32_t sppBegin{0};       // first sample index: slices [sppBegin, sppBegin + spp) add up to a longer render
    std::uint32_t maxBounces{64};    // v4: 64 (was 8); Russian roulette ends most paths long before (docs/rt-interface-v4.md)
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
    // v2: split radiance (3 a pixel) and its variances (1 a pixel).
    std::vector<float> diffuse, specular, emission, diffuseAlbedo, specularAlbedo;
    std::vector<float> diffuseVariance, specularVariance;
    std::string error;
};

// Build the tree (CPU PLOC), the emitter list and its CDF; for v3, the environment CDF and tables.
void finishPtScene(PtScene& s);
// Owned scenes, lit: retro_room (a ceiling lamp and the sun through the open end), iso_street
// (sun and sky), dense_block (sun and sky), and the furnace box (emission E and albedo a on
// every face, pure Lambert, no sun, no sky).
PtScene ptRetroRoom();
PtScene ptIsoStreet();
PtScene ptDenseBlock();
PtScene ptFurnace(float emission, float albedo);
// The white-plane furnace (v4): one large quad facing +y with base colour 1 and material m in a
// constant sky, no sun. Every ray leaving the plane escapes, so a pixel's radiance is the BSDF's
// directional albedo at that view times sky; an energy-conserving white BSDF gives sky everywhere.
// Its camera sees only the plane, from mu about 0.09 at the top row to 1 at the bottom.
PtScene ptWhitePlane(const PtMaterial& m, Vec3 sky);
PtCamera whitePlaneCamera();
PtCamera cameraOf(const swr::Scene& s);

// The CPU reference, row-parallel; identical output for any thread count.
PathTraceOutput pathTraceCpu(const PtScene& s, const PathTraceDesc& d, int threads = 0);

}  // namespace raw::rt
