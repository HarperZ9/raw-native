#pragma once
// The material gallery (ROADMAP M3 showcase): a grid of spheres, one material family a row
// and one parameter swept along the columns, lit by three lights in physical units and the
// procedural sky. Every pixel is a lighting::Sample, so the float64 reference and the GPU
// shading pass (pbr_shade) draw the same picture from the same inputs.
//
// Rows, top to bottom: gold (roughness), red plastic (roughness), car paint (clearcoat
// roughness), velvet (sheen roughness), brushed steel (anisotropy), thin film (iridescence
// thickness), glass (roughness, thin-walled on the left half, volume on the right).
#include "raw/renderer/lighting.hpp"
#include <string>
#include <vector>
namespace raw::gallery {

using lighting::Rgb;

inline constexpr int kRows = 7, kCols = 7;
inline constexpr double kEv100 = 11.0;
// The layout: the camera at the origin looking along -z, +y up, vertical field of view kFovy;
// sphere (row, col) of radius kRadius at sphereCentre (the path-traced comparison rebuilds it).
inline constexpr double kRadius = 0.47, kSpacing = 1.05, kDepth = 11.0, kFovy = 0.66;
inline lighting::D3 sphereCentre(int row, int col) { return {(col - 3) * kSpacing, (3 - row) * kSpacing, -kDepth}; }

struct Gallery {
    int width{0}, height{0}, spp{1};               // spp: samples per pixel along each axis (spp x spp)
    lighting::ClusterGrid grid;
    std::vector<lighting::Light> lights;
    lighting::Cube env;                             // level 0 filled, the rest left to a prefilter
    std::vector<Rgb> sh;
    std::vector<lighting::Sample> samples;          // the sphere hits
    std::vector<int> pixel;                         // pixel index of each sample
    std::vector<Rgb> background;                    // exposed sky radiance per pixel (misses)
    std::vector<int> hits;                          // samples that landed in each pixel
    double exposure{1};
    double fovy{kFovy};
    std::string name{"material-gallery"};
    // The geometry again, for the path-traced comparison (raw/tools/gallery_pt.hpp): the spheres,
    // an optional ground plane, and each sample's group (the gallery: its row; the sphere field:
    // its look; the ground is the last group).
    struct Sphere { lighting::D3 centre; double radius{1}; pbr::Material m; int group{0}; };
    std::vector<Sphere> spheres;
    bool ground{false};
    lighting::D3 groundQuad[4];                    // view space, counter-clockwise seen from its front
    lighting::D3 groundNormal{0, 1, 0};
    pbr::Material groundMaterial;
    std::vector<int> group;                         // per sample
    std::vector<std::string> groupLabels;
};
Gallery build(int width, int height, int spp);
// The sphere field (ROADMAP P10, evidence/p10-sphere-field-bounds.json): 24 spheres in twelve looks
// on a ground plane under the sky cube and a low sun, seen from above and in front. Everything a
// sample carries (and the sky cube, the light, the spheres and the ground) is in view space, the
// space lighting.hpp works in; the tangent rule is normalize(cross(n, +Y)) there, as in path tracer v3.
Gallery buildField(int width, int height, int spp);
// The material gallery with its spheres `spacing` apart (src/renderer/gallery_spaced.cpp), a diagnostic.
Gallery buildSpaced(int size, int spp, double spacing);
// The sphere field's view-to-world rotation (its environment is in view space; a caller that bakes
// a world-space sky for it rotates directions with this).
lighting::D3 fieldViewToWorld(lighting::D3 v);
// Material of the sphere at row, column (also what the media pages label).
pbr::Material sphereMaterial(int row, int col, std::string* label = nullptr);
// Exposed linear radiance per pixel from per-sample exposed radiance (sphere hits and the
// sky averaged over the pixel's sub-samples).
std::vector<Rgb> resolve(const Gallery& g, const std::vector<Rgb>& perSample);
// The float64 reference: exposed radiance per sample.
std::vector<Rgb> shadeCpu(const Gallery& g, const pbr::Tables& t, const lighting::Cube& prefiltered, int threads);

}  // namespace raw::gallery
