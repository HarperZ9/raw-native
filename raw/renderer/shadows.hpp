#pragma once
// Cascaded shadow maps for a directional light, their filtering (hard, PCF, PCSS) and contact
// shadows, as the CPU reference of the GPU passes (ROADMAP M3; bounds in
// evidence/m3-shadows-bounds.json). World space; the light travels along `light`.
#include "raw/math/mat.hpp"
#include "raw/scene/scene.hpp"
#include "raw/core/image.hpp"
#include <array>
#include <cstdint>
#include <vector>
namespace raw::shadows {

struct D3 { double x{0}, y{0}, z{0}; };

inline constexpr int kCascades = 4;
struct Cascade {
    D3 centre;                     // light-space centre (x along right, y along up, z along the light)
    double radius{1};              // half the box's width
    double zNear{0}, zFar{1};      // light-space depth range the map covers
    double split{0};               // far end of this cascade, as view depth
};
struct CascadeSet {
    D3 light, right, up;           // light basis: `light` is the travel direction
    int size{1024};
    std::array<Cascade, kCascades> c;
    // Row-major 4x4: light-space clip of cascade k (x, y in [-1, 1], z in [0, 1], w = 1).
    std::array<float, 16> matrix(int k) const;
    D3 toLight(D3 p) const;
};
// stable: bounding-sphere boxes snapped to whole texels (the engine's mode); false gives the
// frustum-tight, unsnapped fit used as the stability control.
CascadeSet fitCascades(const Camera& cam, D3 lightDir, double shadowFar = 40.0, int size = 1024, bool stable = true);
// The cascade a point at this view depth falls in, or -1 past the last.
int cascadeOf(const CascadeSet& cs, double viewDepth);

// One cascade's map: normalised depth (1 where nothing is drawn) and 1 + triangle index.
struct ShadowMap { int size{0}; std::vector<float> depth; std::vector<uint32_t> tri; };
// The CPU rasterization of every scene triangle into cascade k, snapped to 1/2^bits px.
ShadowMap rasterizeCascade(const Scene& scene, const CascadeSet& cs, int k, int subpixelBits);

enum class Filter : uint8_t { Hard, Pcf, Pcss };
struct LookupParams {
    double sunAngle{0.0261799};    // PCSS: angular radius (1.5 degrees)
    double normalOffset{1.0};      // in texels
    double depthBias{1.5};         // in texels of world depth
};
// The tap tables PCSS uses: Vogel discs (golden-angle spirals) of 16 blocker taps and 25
// filter taps on the unit disc, as x, y pairs; the GPU reads the same table.
const std::vector<float>& poissonTaps();
// Visibility in [0, 1] of point p with unit normal n.
double lookup(const CascadeSet& cs, const std::array<const ShadowMap*, kCascades>& maps, D3 p, D3 n, double viewDepth, Filter f,
              const LookupParams& lp = {});

// Contact shadows over a view-space depth buffer (view distance per pixel, +inf where empty):
// 1 where the march finds no occluder, 0 where it does.
Buffer<float> contactShadows(const Buffer<float>& viewDistance, const Buffer<Vec3>& position, const Camera& cam, D3 lightDir,
                             int steps = 16, double length = 0.5, double thickness = 0.1);

}  // namespace raw::shadows
