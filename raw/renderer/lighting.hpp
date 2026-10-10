#pragma once
// Lighting in physical units, clustered light assignment and image-based lighting, as a
// float64 reference (ROADMAP M3, lighting; bounds in evidence/m3-lighting-bounds.json).
//
// Space: view space, right-handed, the camera at the origin looking along -z, +y up.
// Units (KHR_lights_punctual): directional lights in lux, point and spot lights in candela,
// emission and environments in nits. Shading returns radiance in nits; multiply by
// exposure() for display-referred values.
#include "raw/renderer/pbr.hpp"
#include <cstdint>
#include <vector>
namespace raw::lighting {

using pbr::D3;
using pbr::Rgb;

// ---- Punctual lights --------------------------------------------------------------------
enum class LightType : std::uint8_t { Directional, Point, Spot };
struct Light {
    LightType type{LightType::Point};
    D3 position{0, 0, 0};          // point and spot
    D3 direction{0, 0, -1};        // the way the light points (directional and spot), unit
    Rgb color{1, 1, 1};
    double intensity{1};           // lux (directional) or candela (point, spot)
    double range{0};               // 0: unlimited
    double innerCone{0}, outerCone{0.7853981633974483};   // spot, radians
};
// Illuminance arriving at p from the light, per channel (lux), and the unit direction from
// p toward the light. Range window clamp(1 - (d/range)^4, 0, 1)^2 / d^2; spot falloff per
// KHR_lights_punctual (a squared linear ramp in the cosine between the cones).
Rgb illuminance(const Light& l, D3 p, D3& toLight);
// EV100 and exposure of a physical camera (aperture N, shutter t seconds, ISO S).
double ev100(double aperture, double shutter, double iso);
double exposure(double ev100);                 // 1 / (1.2 * 2^EV100)

// A surface sample: view-space position, unit normal and tangent, unit direction to the eye.
struct Sample { D3 p, n{0, 0, 1}, t{1, 0, 0}, v{0, 0, 1}; pbr::Material m; };
// Direct lighting from the listed lights (indices into `lights`), plus emission.
Rgb shadePunctual(const Sample& s, const pbr::Tables& t, const std::vector<Light>& lights, const std::vector<int>& list);

// ---- Clusters --------------------------------------------------------------------------
struct ClusterGrid {
    static constexpr int kX = 16, kY = 9, kZ = 24, kMaxPerCluster = 256;
    double fovy{1.0}, aspect{16.0 / 9.0}, nearZ{0.1}, farZ{200.0};
    int count() const { return kX * kY * kZ; }
    // The cluster holding a view-space point, or -1 outside the frustum.
    int clusterOf(D3 p) const;
    // The view-space bounding box of a cluster.
    void bounds(int cluster, D3& lo, D3& hi) const;
};
// A light's reach as a sphere (directional lights reach every cluster).
bool lightTouchesBox(const Light& l, D3 lo, D3 hi);
// Brute force: every cluster's list of lights in index order, capped at kMaxPerCluster
// (overflow counts the cut entries).
std::vector<std::vector<int>> assignClusters(const ClusterGrid& g, const std::vector<Light>& lights, int* overflow = nullptr);

// ---- Image-based lighting ---------------------------------------------------------------
// A cube map in a flat array: level, face (+x -x +y -y +z -z), row, column, RGB. Level k is
// (size >> k) square; roughness of level k is k / (levels - 1). Directions are view space.
struct Cube {
    int size{32}, levels{6};
    std::vector<double> rgb;            // every level, contiguous
    std::size_t levelOffset(int level) const;
    Rgb texel(int level, int face, int y, int x) const;
    Rgb sample(int level, D3 dir) const;          // bilinear within the face, clamped at its edges
    Rgb sampleRough(double roughness, D3 dir) const;   // trilinear across levels
};
// The test environment: a sky gradient, a darker ground and a sun disk (nits).
Rgb proceduralSky(D3 dir);
// Level 0 from a function of direction (texel centres), levels 1.. left at zero.
Cube cubeFrom(Rgb (*radiance)(D3), int size, int levels);
// GGX prefilter (N = V = R): levels 1.. from level 0, `samples` Hammersley samples per texel.
void prefilter(Cube& c, int samples);
inline constexpr int kPrefilterSamples = 512;
// Order-2 spherical harmonics of level 0 (9 RGB coefficients), and the cosine-convolved
// irradiance / pi they give for a normal (Ramamoorthi and Hanrahan 2001).
std::vector<Rgb> shProject(const Cube& c);
Rgb shIrradiance(const std::vector<Rgb>& sh, D3 n);
// Split-sum image lighting of a sample (raw::pbr energy model; see the bounds file).
Rgb shadeIbl(const Sample& s, const pbr::Tables& t, const Cube& c, const std::vector<Rgb>& sh);
// The reference it approximates: the full material integrated against level 0 by multiple
// importance sampling (GGX VNDF and cosine, balance heuristic), n samples of each.
Rgb shadeIblReference(const Sample& s, const pbr::Tables& t, const Cube& c, int n);

// Shading frame helpers.
D3 toLocal(const Sample& s, D3 w);

}  // namespace raw::lighting
